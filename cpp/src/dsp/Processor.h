// The full 8D chain.
//
// The signal is treated as a virtual sound source orbiting the listener's head.
// Position is turned into audible cues the same way a real source would be:
//
//     ITD   interaural time difference  -- the far ear hears it a fraction later
//     ILD   interaural level difference -- constant-power panning
//     head shadow                       -- the far ear loses high frequencies
//     front/back cue                    -- rear positions get a gentle HF dip
//     distance                          -- gain, air absorption, reverb send
//
// Every buffer is sized once in `init`, so `process` runs without allocating
// and is safe to call straight from the PipeWire realtime callback.
#pragma once
#include "Params.h"
#include "Filters.h"
#include "Reverb.h"
#include "Orbit.h"
#include "Character.h"
#include <atomic>

namespace eightd {

class Processor {
public:
    void init(float rate, uint32_t maxBlock);
    void reset();

    // Interleaved stereo, `in` and `out` may alias.
    void process(const float* in, float* out, uint32_t n, const Params& p);

    // Telemetry for the UI.  Written by the audio thread, read by the UI, so
    // relaxed atomics: a torn read of a meter is not worth a lock.
    float angle()    const { return angle_.load(std::memory_order_relaxed); }
    float distance() const { return distance_.load(std::memory_order_relaxed); }
    float peakL()    const { return peakL_.load(std::memory_order_relaxed); }
    float peakR()    const { return peakR_.load(std::memory_order_relaxed); }
    float motion()   const { return motion_.load(std::memory_order_relaxed); }

private:
    float gate(const float* x, uint32_t n, const Params& p);
    void  echo(float* io, uint32_t n, const Params& p);
    void  limit(float* io, uint32_t n);

    float rate_ = 48000.f;
    uint32_t maxBlock_ = 2048;

    Orbit orbit_;
    DelayLine itd_, echoLine_;
    Reverb reverb_;
    OnePole shadowLp_, rearLp_, airLp_;
    PitchDown pitch_;
    RadioTone radio_;

    std::vector<float> wet_, scratch_, tail_, shadow_, mono_, acc_;

    float echoTime_ = 0.28f;
    float limiterGain_ = 1.f;
    float lastAirCut_ = -1.f;
    float quietFor_ = 0.f;
    bool  playing_ = false;
    float motionLevel_ = 0.f;

    std::atomic<float> angle_{0.f}, distance_{1.f};
    std::atomic<float> peakL_{0.f}, peakR_{0.f}, motion_{0.f};
};

} // namespace eightd
