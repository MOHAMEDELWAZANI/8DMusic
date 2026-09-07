#include "Engine.h"
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>
#include <chrono>
#include <cstring>
#include <cstdio>

namespace eightd {

Engine::Engine() { processor_.init(float(kRate), 2048); }
Engine::~Engine() { shutdown(); }

static const pw_stream_events kCaptureEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .process = Engine::onCaptureProcess,
};
static const pw_stream_events kPlaybackEvents = {
    .version = PW_VERSION_STREAM_EVENTS,
    .state_changed = Engine::onPlaybackState,
    .process = Engine::onPlaybackProcess,
};

static const spa_pod* buildFormat(spa_pod_builder* b) {
    spa_audio_info_raw info = {};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = kRate;
    info.channels = kChannels;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    return spa_format_audio_raw_build(b, SPA_PARAM_EnumFormat, &info);
}

bool Engine::init(std::string& error) {
    loop_ = pw_thread_loop_new("8d-audio", nullptr);
    if (!loop_) { error = "could not create the audio thread loop"; return false; }
    pw_thread_loop_lock(loop_);
    context_ = pw_context_new(pw_thread_loop_get_loop(loop_), nullptr, 0);
    if (!context_) {
        pw_thread_loop_unlock(loop_);
        error = "could not create a PipeWire context"; return false;
    }
    core_ = pw_context_connect(context_, nullptr, 0);
    if (!core_) {
        pw_thread_loop_unlock(loop_);
        error = "could not connect to PipeWire -- is the audio service running?";
        return false;
    }
    graph_.attach(core_);
    pw_thread_loop_unlock(loop_);
    if (pw_thread_loop_start(loop_) < 0) { error = "could not start the audio thread"; return false; }

    // Give the registry a moment to enumerate what already exists.
    for (int i = 0; i < 40 && graph_.sinks().empty(); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    return true;
}

void Engine::shutdown() {
    if (!loop_) return;
    stop();
    pw_thread_loop_lock(loop_);
    graph_.detach();
    if (core_)    { pw_core_disconnect(core_); core_ = nullptr; }
    if (context_) { pw_context_destroy(context_); context_ = nullptr; }
    pw_thread_loop_unlock(loop_);
    pw_thread_loop_stop(loop_);
    pw_thread_loop_destroy(loop_);
    loop_ = nullptr;
}

void Engine::setParams(const Params& p) {
    std::lock_guard<std::mutex> lock(paramMutex_);
    params_ = p;
}
Params Engine::params() const {
    std::lock_guard<std::mutex> lock(paramMutex_);
    return params_;
}

EngineStatus Engine::status() const {
    EngineStatus s;
    s.running = running_.load(std::memory_order_acquire);
    s.underruns = underruns_.load(std::memory_order_relaxed);
    s.blocks = blocks_.load(std::memory_order_relaxed);
    s.load = load_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        s.error = error_;
    }
    s.message = s.running ? "Running" : "Stopped";
    return s;
}

std::string Engine::currentOutput() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return outputSink_;
}

// --- realtime -------------------------------------------------------------

void Engine::onCaptureProcess(void* data) {
    auto* e = static_cast<Engine*>(data);
    pw_buffer* b = pw_stream_dequeue_buffer(e->capture_);
    if (!b) return;
    spa_buffer* sb = b->buffer;
    const float* src = static_cast<const float*>(sb->datas[0].data);
    if (src) {
        const uint32_t n = sb->datas[0].chunk->size / (sizeof(float) * kChannels);
        if (n) {
            const auto t0 = std::chrono::steady_clock::now();
            Params p;
            {   // A try-lock keeps the audio thread from ever waiting on the UI;
                // one block of slightly stale settings is inaudible.
                std::unique_lock<std::mutex> lock(e->paramMutex_, std::try_to_lock);
                p = lock.owns_lock() ? e->params_ : e->lastParams_;
                if (lock.owns_lock()) e->lastParams_ = p;
            }
            float* dst = e->scratch_.data();
            e->processor_.process(src, dst, n, p);
            e->ring_.push(dst, n);

            const double ms = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - t0).count();
            const double budget = double(n) / kRate;
            const double load = e->load_.load(std::memory_order_relaxed);
            e->load_.store(load + (ms / budget - load) * 0.05,
                           std::memory_order_relaxed);
            if (ms > budget) e->underruns_.fetch_add(1, std::memory_order_relaxed);
            e->blocks_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    pw_stream_queue_buffer(e->capture_, b);
}

void Engine::onPlaybackProcess(void* data) {
    auto* e = static_cast<Engine*>(data);
    pw_buffer* b = pw_stream_dequeue_buffer(e->playback_);
    if (!b) return;
    spa_buffer* sb = b->buffer;
    float* dst = static_cast<float*>(sb->datas[0].data);
    if (dst) {
        uint32_t want = sb->datas[0].maxsize / (sizeof(float) * kChannels);
        if (b->requested) want = std::min<uint32_t>(want, uint32_t(b->requested));
        e->ring_.pop(dst, want);
        sb->datas[0].chunk->offset = 0;
        sb->datas[0].chunk->stride = sizeof(float) * kChannels;
        sb->datas[0].chunk->size   = want * sizeof(float) * kChannels;
    }
    pw_stream_queue_buffer(e->playback_, b);
}

void Engine::onPlaybackState(void* data, pw_stream_state, pw_stream_state state,
                             const char* error) {
    auto* e = static_cast<Engine*>(data);
    if (state == PW_STREAM_STATE_ERROR && error) {
        std::lock_guard<std::mutex> lock(e->statusMutex_);
        e->error_ = error;
    }
}

// --- lifecycle -------------------------------------------------------------

bool Engine::connectStreams(uint32_t quantum, std::string& error) {
    char latency[32];
    std::snprintf(latency, sizeof latency, "%u/%u", quantum, kRate);

    uint8_t cbuf[1024], pbuf[1024];
    spa_pod_builder cb = SPA_POD_BUILDER_INIT(cbuf, sizeof cbuf);
    spa_pod_builder pb = SPA_POD_BUILDER_INIT(pbuf, sizeof pbuf);
    const spa_pod* cparams = buildFormat(&cb);
    const spa_pod* pparams = buildFormat(&pb);

    // The virtual sink: a capture stream that advertises itself as a device, so
    // every application can pick it (and the default sink can point at it).
    capture_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_), "8D Music",
        pw_properties_new(PW_KEY_MEDIA_CLASS, "Audio/Sink",
                          PW_KEY_NODE_NAME, kSinkName,
                          PW_KEY_NODE_DESCRIPTION, kSinkDesc,
                          PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_NODE_VIRTUAL, "true",
                          PW_KEY_AUDIO_CHANNELS, "2",
                          "audio.position", "[ FL FR ]",
                          PW_KEY_NODE_LATENCY, latency,
                          nullptr),
        &kCaptureEvents, this);
    if (!capture_) { error = "could not create the virtual sink"; return false; }
    pw_stream_add_listener(capture_, &captureHook_, &kCaptureEvents, this);
    if (pw_stream_connect(capture_, PW_DIRECTION_INPUT, PW_ID_ANY,
            pw_stream_flags(PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_RT_PROCESS),
            &cparams, 1) < 0) {
        error = "could not publish the virtual sink"; return false;
    }

    playback_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_), "8D Music output",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_ROLE, "Music",
                          PW_KEY_NODE_NAME, kPlaybackName,
                          PW_KEY_NODE_DESCRIPTION, "8D Music output",
                          PW_KEY_TARGET_OBJECT, outputSink_.c_str(),
                          PW_KEY_NODE_LATENCY, latency,
                          nullptr),
        &kPlaybackEvents, this);
    if (!playback_) { error = "could not create the output stream"; return false; }
    pw_stream_add_listener(playback_, &playbackHook_, &kPlaybackEvents, this);
    if (pw_stream_connect(playback_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                            PW_STREAM_FLAG_RT_PROCESS),
            &pparams, 1) < 0) {
        error = "could not connect to the output device"; return false;
    }
    return true;
}

void Engine::destroyStreams() {
    if (capture_)  { spa_hook_remove(&captureHook_);  pw_stream_destroy(capture_);  capture_ = nullptr; }
    if (playback_) { spa_hook_remove(&playbackHook_); pw_stream_destroy(playback_); playback_ = nullptr; }
}

bool Engine::start(const std::string& outputSink, uint32_t quantum, std::string& error,
                   bool takeOverDefault) {
    if (running_.load()) return true;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        outputSink_ = outputSink;
        error_.clear();
    }
    quantum_ = quantum;
    takeOver_ = takeOverDefault;
    scratch_.assign(size_t(quantum) * 4 * kChannels, 0.f);
    processor_.init(float(kRate), quantum * 4);
    ring_.init(size_t(quantum) * 16);
    underruns_.store(0); blocks_.store(0); load_.store(0.0);
    lastParams_ = params();

    pw_thread_loop_lock(loop_);
    const bool ok = connectStreams(quantum, error);
    if (ok) {
        previousDefault_ = graph_.defaultSinkName();
        engageRouting();
    } else {
        destroyStreams();
    }
    pw_thread_loop_unlock(loop_);

    running_.store(ok, std::memory_order_release);
    return ok;
}

void Engine::stop() {
    if (!running_.exchange(false)) return;
    pw_thread_loop_lock(loop_);
    releaseRouting();
    destroyStreams();
    pw_thread_loop_unlock(loop_);
    ring_.clear();
    processor_.reset();
}

bool Engine::retarget(const std::string& outputSink) {
    if (!running_.load() || outputSink.empty()) return false;
    if (outputSink == currentOutput()) return false;
    {
        std::lock_guard<std::mutex> lock(statusMutex_);
        outputSink_ = outputSink;
    }
    // pw_stream takes its target at connect time, so following the desktop to a
    // new device means replacing the playback stream.  Capture is untouched, so
    // audio keeps arriving while the swap happens.
    pw_thread_loop_lock(loop_);
    if (playback_) { spa_hook_remove(&playbackHook_); pw_stream_destroy(playback_); playback_ = nullptr; }
    std::string err;
    uint8_t pbuf[1024];
    spa_pod_builder pb = SPA_POD_BUILDER_INIT(pbuf, sizeof pbuf);
    const spa_pod* pparams = buildFormat(&pb);
    char latency[32];
    std::snprintf(latency, sizeof latency, "%u/%u", quantum_, kRate);
    playback_ = pw_stream_new_simple(
        pw_thread_loop_get_loop(loop_), "8D Music output",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_ROLE, "Music",
                          PW_KEY_NODE_NAME, kPlaybackName,
                          PW_KEY_TARGET_OBJECT, outputSink_.c_str(),
                          PW_KEY_NODE_LATENCY, latency,
                          nullptr),
        &kPlaybackEvents, this);
    bool ok = false;
    if (playback_) {
        pw_stream_add_listener(playback_, &playbackHook_, &kPlaybackEvents, this);
        ok = pw_stream_connect(playback_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                PW_STREAM_FLAG_RT_PROCESS), &pparams, 1) >= 0;
    }
    pw_thread_loop_unlock(loop_);
    return ok;
}

// --- routing ---------------------------------------------------------------

void Engine::engageRouting() {
    if (!takeOver_) return;
    if (previousDefault_ != kSinkName) graph_.setDefaultSink(kSinkName);
    captureStreamsLocked();
}

// Caller must hold the thread loop.  New applications follow the default sink
// on their own; anything that pins its own target needs moving explicitly.
void Engine::captureStreamsLocked() {
    const uint32_t serial = graph_.serialOf(kSinkName);
    if (!serial) return;
    for (const auto& s : graph_.outputStreams()) {
        if (moved_.count(s.id)) continue;
        graph_.moveStream(s.id, serial);
        moved_.insert(s.id);
    }
}

void Engine::releaseRouting() {
    if (!takeOver_) return;
    // Restore the default first: clearing a stream's target makes it follow
    // whatever the default happens to be at that moment.
    if (!previousDefault_.empty() && previousDefault_ != kSinkName)
        graph_.setDefaultSink(previousDefault_);
    previousDefault_.clear();
    for (uint32_t id : moved_) graph_.clearStreamTarget(id);
    moved_.clear();
}

void Engine::captureExistingStreams() {
    if (!running_.load() || !takeOver_) return;
    pw_thread_loop_lock(loop_);
    captureStreamsLocked();
    pw_thread_loop_unlock(loop_);
}

} // namespace eightd
