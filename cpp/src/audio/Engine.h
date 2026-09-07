// The realtime audio engine.
//
//     apps -> [ 8D Music virtual sink ] -> DSP -> your speakers
//
// Both ends are native pw_streams in this process: the virtual sink apps play
// into, and the playback stream feeding the real device.  There are no helper
// processes and no pipes, so audio never leaves the address space and the DSP
// runs directly in PipeWire's realtime callback.
#pragma once
#include "Graph.h"
#include "../dsp/Processor.h"
#include <pipewire/pipewire.h>
#include <atomic>
#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <thread>

namespace eightd {

inline constexpr uint32_t kRate = 48000, kChannels = 2;

struct LatencyProfile { const char* label; uint32_t quantum; };
inline const LatencyProfile kLatencies[] = {
    {"Low (snappiest)",   256},
    {"Balanced",          512},
    {"Safe (most stable)",1024},
};
inline constexpr int kDefaultLatency = 1;

// Single producer, single consumer.  Capture writes, playback reads; neither
// blocks, so a late cycle on one side never stalls the other.
class Ring {
public:
    void init(size_t frames) {
        size_t s = 1;
        while (s < frames) s <<= 1;
        buf_.assign(s * kChannels, 0.f);
        mask_ = s - 1;
        w_.store(0); r_.store(0);
    }
    void clear() {
        std::fill(buf_.begin(), buf_.end(), 0.f);
        w_.store(0); r_.store(0);
    }
    size_t available() const {
        return size_t(w_.load(std::memory_order_acquire) -
                      r_.load(std::memory_order_relaxed));
    }
    void push(const float* src, size_t n) {
        const uint64_t w = w_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            const size_t s = ((w + i) & mask_) * kChannels;
            buf_[s] = src[i * 2]; buf_[s + 1] = src[i * 2 + 1];
        }
        w_.store(w + n, std::memory_order_release);
    }
    size_t pop(float* dst, size_t n) {
        const uint64_t r = r_.load(std::memory_order_relaxed);
        const size_t have = std::min(n, available());
        for (size_t i = 0; i < have; ++i) {
            const size_t s = ((r + i) & mask_) * kChannels;
            dst[i * 2] = buf_[s]; dst[i * 2 + 1] = buf_[s + 1];
        }
        for (size_t i = have; i < n; ++i) { dst[i * 2] = 0.f; dst[i * 2 + 1] = 0.f; }
        r_.store(r + have, std::memory_order_release);
        return have;
    }
private:
    std::vector<float> buf_;
    size_t mask_ = 0;
    std::atomic<uint64_t> w_{0}, r_{0};
};

struct EngineStatus {
    bool running = false;
    std::string message = "Stopped";
    std::string error;
    uint64_t underruns = 0;
    double load = 0.0;          // fraction of the block period spent in the DSP
    uint64_t blocks = 0;
};

class Engine {
public:
    Engine();
    ~Engine();

    bool init(std::string& error);        // connect to PipeWire
    void shutdown();

    // `takeOverDefault` makes the virtual sink the system default so every
    // application follows it.  Turning it off leaves routing to the user (and
    // lets the tests run without touching real playback).
    bool start(const std::string& outputSink, uint32_t quantum, std::string& error,
               bool takeOverDefault = true);
    void stop();
    bool retarget(const std::string& outputSink);

    Graph& graph() { return graph_; }
    Processor& processor() { return processor_; }

    // Parameters: the UI writes, the audio thread reads a snapshot per block.
    void setParams(const Params& p);
    Params params() const;

    bool running() const { return running_.load(std::memory_order_acquire); }
    EngineStatus status() const;
    std::string currentOutput() const;

    // Pull any stray playback streams into the virtual sink.
    void captureExistingStreams();

    // PipeWire event tables take plain function pointers, so these are public.
    static void onCaptureProcess(void* data);
    static void onPlaybackProcess(void* data);
    static void onPlaybackState(void* data, pw_stream_state old,
                                pw_stream_state state, const char* error);

private:
    bool connectStreams(uint32_t quantum, std::string& error);
    void destroyStreams();
    void engageRouting();
    void captureStreamsLocked();
    void releaseRouting();

    pw_thread_loop* loop_ = nullptr;
    pw_context* context_ = nullptr;
    pw_core* core_ = nullptr;
    pw_stream* capture_ = nullptr;
    pw_stream* playback_ = nullptr;
    spa_hook captureHook_{}, playbackHook_{};

    Graph graph_;
    Processor processor_;
    Ring ring_;

    std::vector<float> scratch_;
    std::atomic<bool> running_{false};

    mutable std::mutex paramMutex_;
    Params params_;
    Params lastParams_;      // audio thread's copy, used if the lock is busy

    bool takeOver_ = true;
    std::string outputSink_;
    std::string previousDefault_;
    std::set<uint32_t> moved_;

    std::atomic<uint64_t> underruns_{0}, blocks_{0};
    std::atomic<double> load_{0.0};
    uint32_t quantum_ = 512;
    mutable std::mutex statusMutex_;
    std::string error_;
};

} // namespace eightd
