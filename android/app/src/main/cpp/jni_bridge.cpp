// The Android end of the 8D engine.
//
// This file is the whole platform layer.  Everything musical lives in
// cpp/src/dsp, shared with the desktop build; all that happens here is:
//
//     AAudio callback -> Processor::process -> the same callback's buffer
//
// which is the same shape as the desktop Engine, with AAudio standing in for
// PipeWire.  The DSP runs on the audio thread and allocates nothing, so there
// is no ring buffer and no helper thread in the signal path.
#include <jni.h>
#include <aaudio/AAudio.h>
#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "Params.h"
#include "Processor.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "8dmusic", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "8dmusic", __VA_ARGS__)

using eightd::Character;
using eightd::Mode;
using eightd::Params;
using eightd::Processor;

namespace {

// The DSP sizes its scratch buffers once, in init().  A callback larger than
// this is processed in chunks rather than made to reallocate.
constexpr uint32_t kMaxBlock = 2048;

// Catmull-Rom, used once at load time to move the file onto the device's rate.
inline float hermite(float a, float b, float c, float d, float t) {
    const float c1 = 0.5f * (c - a);
    const float c2 = a - 2.5f * b + 2.f * c - 0.5f * d;
    const float c3 = 0.5f * (d - a) + 1.5f * (b - c);
    return ((c3 * t + c2) * t + c1) * t + b;
}


// Single-producer / single-consumer ring.
//
// Direct capture arrives on a Java thread from AudioRecord; the DSP runs on
// AAudio's callback.  This is the only place the two meet, and neither side
// ever blocks the other -- the audio thread would rather output a little
// silence than wait for a reader that is late.
class Ring {
public:
    void init(size_t frames) {
        size_t s = 1;
        while (s < frames) s <<= 1;
        buf_.assign(s * 2, 0.f);
        mask_ = s - 1;
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
    }

    void clear() {
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
        std::fill(buf_.begin(), buf_.end(), 0.f);
    }

    size_t available() const {
        return write_.load(std::memory_order_acquire) - read_.load(std::memory_order_relaxed);
    }
    size_t capacity() const { return mask_ + 1; }

    void write(const float* src, size_t frames) {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        const size_t room = capacity() - (w - r);
        // Overrun means the consumer stalled; drop the oldest rather than the
        // newest so what you hear stays in step with what is playing.
        if (frames > room) {
            const size_t drop = frames - room;
            read_.store(r + drop, std::memory_order_release);
        }
        for (size_t i = 0; i < frames; ++i) {
            const size_t s = ((w + i) & mask_) * 2;
            buf_[s]     = src[2 * i];
            buf_[s + 1] = src[2 * i + 1];
        }
        write_.store(w + frames, std::memory_order_release);
    }

    // Fills `frames`, padding with silence when the producer is behind.
    size_t read(float* dst, size_t frames) {
        const size_t r = read_.load(std::memory_order_relaxed);
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t have = std::min(frames, w - r);
        for (size_t i = 0; i < have; ++i) {
            const size_t s = ((r + i) & mask_) * 2;
            dst[2 * i]     = buf_[s];
            dst[2 * i + 1] = buf_[s + 1];
        }
        if (have < frames)
            std::memset(dst + have * 2, 0, (frames - have) * 2 * sizeof(float));
        read_.store(r + have, std::memory_order_release);
        return have;
    }

private:
    std::vector<float> buf_;
    size_t mask_ = 0;
    std::atomic<size_t> read_{0}, write_{0};
};

class Engine {
public:
    ~Engine() { close(); }

    bool open() {
        AAudioStreamBuilder* b = nullptr;
        if (AAudio_createStreamBuilder(&b) != AAUDIO_OK) return false;

        AAudioStreamBuilder_setDirection(b, AAUDIO_DIRECTION_OUTPUT);
        AAudioStreamBuilder_setFormat(b, AAUDIO_FORMAT_PCM_FLOAT);
        AAudioStreamBuilder_setChannelCount(b, 2);
        AAudioStreamBuilder_setPerformanceMode(b, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
        AAudioStreamBuilder_setUsage(b, AAUDIO_USAGE_MEDIA);
        AAudioStreamBuilder_setContentType(b, AAUDIO_CONTENT_TYPE_MUSIC);
        AAudioStreamBuilder_setDataCallback(b, &Engine::onData, this);
        AAudioStreamBuilder_setErrorCallback(b, &Engine::onError, this);

        const aaudio_result_t r = AAudioStreamBuilder_openStream(b, &stream_);
        AAudioStreamBuilder_delete(b);
        if (r != AAUDIO_OK) {
            LOGE("openStream failed: %s", AAudio_convertResultToText(r));
            return false;
        }

        rate_ = AAudioStream_getSampleRate(stream_);
        const int32_t burst = AAudioStream_getFramesPerBurst(stream_);
        AAudioStream_setBufferSizeInFrames(stream_, burst * 2);
        // Roughly a quarter second of slack for the capture thread.
        ring_.init(size_t(rate_ / 4));

        proc_.init(float(rate_), kMaxBlock);
        LOGI("stream open: %d Hz, burst %d frames", rate_, burst);
        return true;
    }

    void close() {
        if (!stream_) return;
        AAudioStream_requestStop(stream_);
        AAudioStream_close(stream_);
        stream_ = nullptr;
    }

    // Idempotent: asking a running stream to start is not a failure, and the
    // caller uses the return value to drive its play/pause state.
    bool start() {
        if (!stream_) return false;
        const aaudio_stream_state_t st = AAudioStream_getState(stream_);
        if (st == AAUDIO_STREAM_STATE_STARTED || st == AAUDIO_STREAM_STATE_STARTING)
            return true;
        const aaudio_result_t r = AAudioStream_requestStart(stream_);
        if (r != AAUDIO_OK) LOGE("requestStart: %s", AAudio_convertResultToText(r));
        return r == AAUDIO_OK;
    }

    void pause() {
        if (!stream_) return;
        const aaudio_stream_state_t st = AAudioStream_getState(stream_);
        if (st == AAUDIO_STREAM_STATE_STARTED || st == AAUDIO_STREAM_STATE_STARTING)
            AAudioStream_requestPause(stream_);
    }

    bool isRunning() const {
        if (!stream_) return false;
        const aaudio_stream_state_t st = AAudioStream_getState(stream_);
        return st == AAUDIO_STREAM_STATE_STARTED || st == AAUDIO_STREAM_STATE_STARTING;
    }

    int32_t rate() const { return rate_; }
    uint32_t frames() const { return frames_; }

    // Decoded PCM arrives here as interleaved 16-bit at the file's own rate.
    //
    // Streamed, not all at once: a four-minute track is ten million frames and
    // decoding it before the first sample plays is the difference between
    // "instant" and "several seconds".  The audio thread reads up to `ready_`,
    // the decoder fills ahead of it.
    void beginSource(uint32_t estFrames, int32_t srcRate, int32_t channels) {
        std::lock_guard<std::mutex> lk(srcMutex_);
        srcRate_ = srcRate > 0 ? srcRate : rate_;
        srcChannels_ = channels < 1 ? 1 : channels;
        ratio_ = double(rate_) / double(srcRate_);
        const size_t cap = size_t(double(estFrames) * ratio_) + 4096;
        pcm_.assign(cap * 2, 0.f);
        frames_ = 0;
        ready_ = 0;
        srcPhase_ = 0.0;
        carry_.clear();
        pos_.store(0, std::memory_order_relaxed);
        complete_ = false;
        proc_.reset();
    }

    void appendSource(const int16_t* pcm, uint32_t frames) {
        if (frames == 0) return;
        std::lock_guard<std::mutex> lk(srcMutex_);

        // Interleaved stereo float, with whatever tail the last chunk could not
        // resample yet in front of it.
        std::vector<float> in(carry_);
        in.reserve(carry_.size() + size_t(frames) * 2);
        for (uint32_t i = 0; i < frames; ++i) {
            const int16_t l = pcm[size_t(i) * srcChannels_];
            const int16_t r = (srcChannels_ == 1) ? l : pcm[size_t(i) * srcChannels_ + 1];
            in.push_back(float(l) * (1.f / 32768.f));
            in.push_back(float(r) * (1.f / 32768.f));
        }
        const size_t have = in.size() / 2;

        if (srcRate_ == rate_) {
            appendFrames(in.data(), have);
            carry_.clear();
            return;
        }

        // Catmull-Rom needs one sample behind and two ahead, so stop three
        // short and carry the remainder into the next chunk.
        if (have < 4) { carry_ = in; return; }
        const double last = double(have) - 3.0;
        size_t produced = 0;
        double sp = srcPhase_;
        std::vector<float> out;
        out.reserve(size_t((last - sp) * ratio_ + 4) * 2);
        while (sp < last) {
            const size_t k = size_t(sp);
            const float t = float(sp - double(k));
            for (int ch = 0; ch < 2; ++ch) {
                const float a = in[(k - (k > 0 ? 1 : 0)) * 2 + ch];
                const float b = in[k * 2 + ch];
                const float c = in[(k + 1) * 2 + ch];
                const float d = in[(k + 2) * 2 + ch];
                out.push_back(hermite(a, b, c, d, t));
            }
            ++produced;
            sp += 1.0 / ratio_;
        }
        appendFrames(out.data(), produced);

        // Keep everything from floor(sp) - 1 onwards for the next call.
        const size_t keepFrom = size_t(sp) > 0 ? size_t(sp) - 1 : 0;
        srcPhase_ = sp - double(keepFrom);
        carry_.assign(in.begin() + long(keepFrom * 2), in.end());
    }

    void endSource() {
        std::lock_guard<std::mutex> lk(srcMutex_);
        complete_ = true;
    }

    uint32_t readyFrames() const { return ready_; }
    bool sourceComplete() const { return complete_; }

    // Direct capture: the source is a live stream, not a decoded file.
    void setLive(bool on) {
        live_.store(on, std::memory_order_release);
        if (on) ring_.clear();
        proc_.reset();
    }
    bool live() const { return live_.load(std::memory_order_acquire); }

    void pushCapture(const float* pcm, uint32_t frames) {
        if (frames) ring_.write(pcm, frames);
    }
    uint32_t queued() const { return uint32_t(ring_.available()); }

    void setParams(const Params& p) {
        std::lock_guard<std::mutex> lk(paramMutex_);
        pending_ = p;
        dirty_.store(true, std::memory_order_release);
    }

    void seek(uint32_t frame) {
        pos_.store(std::min(frame, frames_), std::memory_order_relaxed);
    }
    uint32_t position() const { return pos_.load(std::memory_order_relaxed); }
    void setLoop(bool on) { loop_.store(on, std::memory_order_relaxed); }

    void telemetry(float* out) const {
        out[0] = proc_.angle();
        out[1] = proc_.distance();
        out[2] = proc_.peakL();
        out[3] = proc_.peakR();
        out[4] = proc_.motion();
    }

private:
    // Caller holds srcMutex_.
    void appendFrames(const float* src, size_t n) {
        if (n == 0) return;
        const size_t cap = pcm_.size() / 2;
        if (ready_ + n > cap) pcm_.resize((ready_ + n + 4096) * 2, 0.f);
        std::memcpy(&pcm_[ready_ * 2], src, n * 2 * sizeof(float));
        ready_ += n;
        frames_ = uint32_t(ready_);
    }

    static aaudio_data_callback_result_t onData(AAudioStream*, void* user, void* audio, int32_t n) {
        return static_cast<Engine*>(user)->render(static_cast<float*>(audio), uint32_t(n));
    }
    static void onError(AAudioStream*, void*, aaudio_result_t err) {
        LOGE("stream error: %s", AAudio_convertResultToText(err));
    }

    aaudio_data_callback_result_t render(float* out, uint32_t n) {
        // The UI thread publishes parameters; try_lock so the audio thread
        // never waits on it.  A skipped update lands on the next block.
        if (dirty_.load(std::memory_order_acquire)) {
            std::unique_lock<std::mutex> lk(paramMutex_, std::try_to_lock);
            if (lk.owns_lock()) {
                current_ = pending_;
                dirty_.store(false, std::memory_order_release);
            }
        }

        if (live_.load(std::memory_order_acquire)) {
            for (uint32_t done = 0; done < n; ) {
                const uint32_t block = std::min(n - done, kMaxBlock);
                float* dst = out + size_t(done) * 2;
                ring_.read(dst, block);
                proc_.process(dst, dst, block, current_);
                done += block;
            }
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }

        std::unique_lock<std::mutex> src(srcMutex_, std::try_to_lock);
        if (!src.owns_lock() || frames_ == 0) {
            std::memset(out, 0, size_t(n) * 2 * sizeof(float));
            return AAUDIO_CALLBACK_RESULT_CONTINUE;
        }

        for (uint32_t done = 0; done < n; ) {
            const uint32_t block = std::min(n - done, kMaxBlock);
            float* dst = out + size_t(done) * 2;

            uint32_t pos = pos_.load(std::memory_order_relaxed);
            const uint32_t avail = (pos < frames_) ? std::min(block, frames_ - pos) : 0;

            if (avail)
                std::memcpy(dst, &pcm_[size_t(pos) * 2], size_t(avail) * 2 * sizeof(float));
            if (avail < block)
                std::memset(dst + size_t(avail) * 2, 0,
                            size_t(block - avail) * 2 * sizeof(float));

            // in and out alias, which Processor::process explicitly allows.
            proc_.process(dst, dst, block, current_);

            pos += avail;
            if (complete_ && pos >= frames_ && loop_.load(std::memory_order_relaxed))
                pos = 0;
            pos_.store(pos, std::memory_order_relaxed);

            done += block;
        }
        return AAUDIO_CALLBACK_RESULT_CONTINUE;
    }

    AAudioStream* stream_ = nullptr;
    int32_t rate_ = 48000;

    Processor proc_;

    mutable std::mutex srcMutex_;
    std::vector<float> pcm_;
    std::vector<float> carry_;
    size_t ready_ = 0;
    double ratio_ = 1.0, srcPhase_ = 0.0;
    int32_t srcRate_ = 48000, srcChannels_ = 2;
    bool complete_ = false;
    uint32_t frames_ = 0;
    std::atomic<uint32_t> pos_{0};
    std::atomic<bool> loop_{true};

    Ring ring_;
    std::atomic<bool> live_{false};

    std::mutex paramMutex_;
    Params pending_, current_;
    std::atomic<bool> dirty_{false};
};

// Layout of the float[] the Kotlin side sends.  Kept in one place so the two
// halves cannot disagree; NativeEngine.kt mirrors it.
enum ParamIndex {
    kSpeed = 0, kRadius, kDepth, kSmoothness, kWidth, kManualAngle,
    kCharacterAmount, kDelayMix, kDelayTime, kDelayFeedback,
    kReverbMix, kReverbSize, kReverbDamp, kOutputGain,
    kMode, kCharacter, kDirection, kEnabled, kPauseWhenSilent,
    kEqBass, kEqMid, kEqTreble,
    kParamCount
};

inline Engine* self(jlong h) { return reinterpret_cast<Engine*>(h); }

} // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_eightd_music_NativeEngine_nativeCreate(JNIEnv*, jobject) {
    auto* e = new Engine();
    if (!e->open()) { delete e; return 0; }
    return reinterpret_cast<jlong>(e);
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeDestroy(JNIEnv*, jobject, jlong h) {
    delete self(h);
}

JNIEXPORT jint JNICALL
Java_com_eightd_music_NativeEngine_nativeSampleRate(JNIEnv*, jobject, jlong h) {
    return h ? self(h)->rate() : 0;
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeBeginSource(JNIEnv*, jobject, jlong h,
                                                     jint estFrames, jint srcRate, jint channels) {
    if (h) self(h)->beginSource(uint32_t(std::max(0, estFrames)), srcRate, channels);
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeAppendSource(JNIEnv* env, jobject, jlong h,
                                                      jshortArray pcm, jint frames) {
    if (!h || frames <= 0) return;
    jshort* data = env->GetShortArrayElements(pcm, nullptr);
    if (!data) return;
    self(h)->appendSource(reinterpret_cast<const int16_t*>(data), uint32_t(frames));
    env->ReleaseShortArrayElements(pcm, data, JNI_ABORT);
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeEndSource(JNIEnv*, jobject, jlong h) {
    if (h) self(h)->endSource();
}

JNIEXPORT jboolean JNICALL
Java_com_eightd_music_NativeEngine_nativeStart(JNIEnv*, jobject, jlong h) {
    return (h && self(h)->start()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_eightd_music_NativeEngine_nativeIsRunning(JNIEnv*, jobject, jlong h) {
    return (h && self(h)->isRunning()) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativePause(JNIEnv*, jobject, jlong h) {
    if (h) self(h)->pause();
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeSetParams(JNIEnv* env, jobject, jlong h, jfloatArray arr) {
    if (!h) return;
    if (env->GetArrayLength(arr) < kParamCount) return;

    jfloat v[kParamCount];
    env->GetFloatArrayRegion(arr, 0, kParamCount, v);

    Params p;
    p.speed           = v[kSpeed];
    p.radius          = v[kRadius];
    p.depth           = v[kDepth];
    p.smoothness      = v[kSmoothness];
    p.width           = v[kWidth];
    p.manualAngle     = v[kManualAngle];
    p.characterAmount = v[kCharacterAmount];
    p.delayMix        = v[kDelayMix];
    p.delayTime       = v[kDelayTime];
    p.delayFeedback   = v[kDelayFeedback];
    p.reverbMix       = v[kReverbMix];
    p.reverbSize      = v[kReverbSize];
    p.reverbDamp      = v[kReverbDamp];
    p.outputGain      = v[kOutputGain];

    const int mode = std::clamp(int(v[kMode]), 0, int(Mode::Count) - 1);
    const int chr  = std::clamp(int(v[kCharacter]), 0, int(Character::Count) - 1);
    p.mode      = Mode(mode);
    p.character = Character(chr);
    p.direction = (v[kDirection] < 0.f) ? -1 : 1;
    p.enabled         = v[kEnabled] > 0.5f;
    p.pauseWhenSilent = v[kPauseWhenSilent] > 0.5f;
    p.eqBass          = v[kEqBass];
    p.eqMid           = v[kEqMid];
    p.eqTreble        = v[kEqTreble];

    self(h)->setParams(p);
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeSeek(JNIEnv*, jobject, jlong h, jint frame) {
    if (h) self(h)->seek(uint32_t(std::max(0, frame)));
}

JNIEXPORT jint JNICALL
Java_com_eightd_music_NativeEngine_nativePosition(JNIEnv*, jobject, jlong h) {
    return h ? jint(self(h)->position()) : 0;
}

JNIEXPORT jint JNICALL
Java_com_eightd_music_NativeEngine_nativeTotalFrames(JNIEnv*, jobject, jlong h) {
    return h ? jint(self(h)->frames()) : 0;
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeSetLoop(JNIEnv*, jobject, jlong h, jboolean on) {
    if (h) self(h)->setLoop(on == JNI_TRUE);
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeTelemetry(JNIEnv* env, jobject, jlong h, jfloatArray out) {
    if (!h || env->GetArrayLength(out) < 5) return;
    float t[5];
    self(h)->telemetry(t);
    env->SetFloatArrayRegion(out, 0, 5, t);
}

// Mirrors cpp/tests/dsp_probe.cpp exactly -- same tone, same block size, same
// parameters -- so the file it writes here can be compared sample for sample
// with the desktop build's output.  This is how "the Android build runs the
// same effect" stops being a claim and becomes a measurement.
JNIEXPORT jstring JNICALL
Java_com_eightd_music_NativeEngine_nativeSelfTest(JNIEnv* env, jobject,
                                                  jstring jdir, jstring jmode) {
    const char* dir  = env->GetStringUTFChars(jdir, nullptr);
    const char* name = env->GetStringUTFChars(jmode, nullptr);
    const std::string m(name);

    constexpr uint32_t RATE = 48000, BLOCK = 512, SECONDS = 5;
    constexpr uint32_t N = RATE * SECONDS;

    Mode mode = Mode::Circular;
    if      (m == "pingpong") mode = Mode::PingPong;
    else if (m == "pendulum") mode = Mode::Pendulum;
    else if (m == "linear")   mode = Mode::Linear;
    else if (m == "figure8")  mode = Mode::Figure8;
    else if (m == "spiral")   mode = Mode::Spiral;
    else if (m == "static")   mode = Mode::Static;

    Params p;
    p.mode = mode; p.speed = 0.25f; p.radius = 1.2f; p.depth = 0.85f;
    p.smoothness = 0.35f; p.width = 1.0f;
    p.delayMix = 0.25f; p.delayTime = 0.28f; p.delayFeedback = 0.35f;
    p.reverbMix = 0.30f; p.reverbSize = 0.6f; p.reverbDamp = 0.45f;
    p.outputGain = 0.9f; p.pauseWhenSilent = false;   // deterministic: never park
    if (m == "radio")  { p.character = Character::Radio;  p.characterAmount = 1.0f; }
    if (m == "slowed") { p.character = Character::Slowed; p.characterAmount = 0.85f; }

    std::vector<float> in(size_t(N) * 2), out(size_t(N) * 2);
    for (uint32_t i = 0; i < N; ++i) {
        const double t = double(i) / RATE;
        const float s = float(0.25 * std::sin(2.0 * M_PI * 220.0 * t));
        in[2 * i] = s;
        in[2 * i + 1] = s * 0.9f;
    }

    Processor proc;
    proc.init(float(RATE), BLOCK);

    double worst = 0.0;
    uint32_t blocks = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i + BLOCK <= N; i += BLOCK) {
        const auto b0 = std::chrono::steady_clock::now();
        proc.process(in.data() + size_t(i) * 2, out.data() + size_t(i) * 2, BLOCK, p);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - b0).count();
        if (ms > worst) worst = ms;
        ++blocks;
    }
    const double wall  = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t0).count();
    const double audio = double(blocks) * BLOCK / RATE;

    const std::string path = std::string(dir) + "/" + m + ".f32";
    if (FILE* f = fopen(path.c_str(), "wb")) {
        fwrite(out.data(), sizeof(float), size_t(blocks) * BLOCK * 2, f);
        fclose(f);
    }

    char msg[192];
    snprintf(msg, sizeof(msg), "%-9s rt=%5.2f%%  worstblk=%.3f ms (budget %.1f)",
             m.c_str(), wall / audio * 100.0, worst, BLOCK * 1000.0 / RATE);
    LOGI("%s -> %s", msg, path.c_str());

    env->ReleaseStringUTFChars(jdir, dir);
    env->ReleaseStringUTFChars(jmode, name);
    return env->NewStringUTF(msg);
}

JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativeSetLive(JNIEnv*, jobject, jlong h, jboolean on) {
    if (h) self(h)->setLive(on == JNI_TRUE);
}

// Called from the capture thread with interleaved stereo float frames.
JNIEXPORT void JNICALL
Java_com_eightd_music_NativeEngine_nativePushCapture(JNIEnv* env, jobject, jlong h,
                                                     jfloatArray pcm, jint frames) {
    if (!h || frames <= 0) return;
    jfloat* data = env->GetFloatArrayElements(pcm, nullptr);
    if (!data) return;
    self(h)->pushCapture(data, uint32_t(frames));
    env->ReleaseFloatArrayElements(pcm, data, JNI_ABORT);
}

JNIEXPORT jint JNICALL
Java_com_eightd_music_NativeEngine_nativeQueued(JNIEnv*, jobject, jlong h) {
    return h ? jint(self(h)->queued()) : 0;
}

} // extern "C"
