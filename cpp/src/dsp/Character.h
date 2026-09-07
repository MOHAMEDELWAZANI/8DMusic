// Tone characters applied to the source before it enters the orbit.
#pragma once
#include "Filters.h"
#include "Params.h"
#include "Orbit.h"   // Rng

namespace eightd {

// Pitch shifted down by crossfading two delay taps that slide continuously.
//
// This lowers pitch, which is the audible signature of "slowed" audio.  It
// cannot slow the tempo: the stream arrives in real time, so there is no way
// to stretch it without falling permanently behind.
class PitchDown {
public:
    void init(float rate, uint32_t maxBlock, float windowMs = 62.f) {
        window_ = std::max(uint32_t(rate * windowMs / 1000.f), 256u);
        line_.init(maxBlock + window_ + 64);
        phase_ = 0.f;
    }
    void reset() { line_.reset(); phase_ = 0.f; }

    void process(const float* in, float* out, uint32_t n, float ratio) {
        const float step = (1.f - std::clamp(ratio, 0.5f, 1.f)) / float(window_);
        line_.write(in, n);
        const double base = double(line_.t()) - n;
        float ph = phase_;
        for (uint32_t i = 0; i < n; ++i) {
            float u1 = ph - std::floor(ph);
            float u2 = ph + 0.5f; u2 -= std::floor(u2);
            const float d1 = kMinDelay + u1 * float(window_);
            const float d2 = kMinDelay + u2 * float(window_);
            const float w1 = 0.5f - 0.5f * std::cos(kTwoPi * u1);
            const float w2 = 0.5f - 0.5f * std::cos(kTwoPi * u2);
            const double at = base + i;
            for (int ch = 0; ch < 2; ++ch)
                out[2*i + ch] = line_.readAt(at, d1, ch) * w1
                              + line_.readAt(at, d2, ch) * w2;
            ph += step;
        }
        phase_ = ph - std::floor(ph);
    }
private:
    static constexpr float kMinDelay = 2.f;
    DelayLine line_;
    uint32_t window_ = 256;
    float phase_ = 0.f;
};

// An old take heard through an AM set: mono, band-limited to roughly
// 450 Hz - 3 kHz, softly saturated, with the slow pitch drift of a worn tape
// and a bed of hiss that follows the signal so silence stays silent.
class RadioTone {
public:
    void init(float rate, uint32_t maxBlock) {
        rate_ = rate;
        for (auto& s : hp_) s.init(kLowCut, rate);
        for (auto& s : lp_) s.init(kHighCut, rate);
        hissHp_.init(1100.f, rate);
        wow_.init(maxBlock + uint32_t(0.05f * rate) + 64);
        phase_ = 0.f; envelope_ = 0.f;
    }
    void reset() {
        for (auto& s : hp_) s.reset();
        for (auto& s : lp_) s.reset();
        hissHp_.reset(); wow_.reset(); phase_ = 0.f; envelope_ = 0.f;
    }

    // `scratch` is caller-owned, at least 2*n floats.
    void process(const float* in, float* out, uint32_t n, float amount, float* scratch) {
        amount = std::clamp(amount, 0.f, 1.f);
        float* wet = scratch;

        // a broadcast is mono
        float level = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            const float m = (in[2*i] + in[2*i + 1]) * 0.5f;
            wet[2*i] = wet[2*i + 1] = m;
            level += std::fabs(m);
        }
        level = n ? level / float(n) : 0.f;

        // wow and flutter: two slow, mutually prime LFOs so it never loops
        const float base  = kWowBaseMs  * rate_ / 1000.f;
        const float depth = kWowDepthMs * rate_ / 1000.f;
        wow_.write(wet, n);
        const double origin = double(wow_.t()) - n;
        float t = phase_;
        const float inv = 1.f / rate_;
        for (uint32_t i = 0; i < n; ++i) {
            const float drift = std::sin(kTwoPi * 0.6f * t) * 0.7f
                              + std::sin(kTwoPi * 0.17f * t) * 0.3f;
            const float d = base + drift * depth;
            const double at = origin + i;
            wet[2*i]     = wow_.readAt(at, d, 0);
            wet[2*i + 1] = wow_.readAt(at, d, 1);
            t += inv;
        }
        phase_ = t;

        // The valve stage comes first: its harmonics have to be band-limited
        // too, or the set would somehow reproduce what it cannot pass.
        constexpr float drive = 2.4f;
        const float norm = 1.f / std::tanh(drive);
        for (uint32_t i = 0; i < n * 2; ++i) wet[i] = std::tanh(wet[i] * drive) * norm;

        for (auto& s : hp_) {
            s.process(wet, hpScratch_.data(), n);
            for (uint32_t i = 0; i < n * 2; ++i) wet[i] -= hpScratch_[i];
        }

        // hiss, gated so silence stays silent, shaped by the same speaker
        envelope_ += (level - envelope_) * 0.15f;
        const float hissGain = std::clamp(envelope_ * 12.f, 0.f, 1.f);
        for (uint32_t i = 0; i < n * 2; ++i) hissScratch_[i] = rng_.normal() * 0.05f;
        hissHp_.process(hissScratch_.data(), hpScratch_.data(), n);
        for (uint32_t i = 0; i < n * 2; ++i)
            wet[i] += (hissScratch_[i] - hpScratch_[i]) * hissGain;

        for (auto& s : lp_) s.process(wet, wet, n);

        for (uint32_t i = 0; i < n * 2; ++i)
            out[i] = in[i] * (1.f - amount) + wet[i] * kMakeup * amount;
    }

    void prepare(uint32_t maxBlock) {
        hpScratch_.assign(maxBlock * 2, 0.f);
        hissScratch_.assign(maxBlock * 2, 0.f);
    }
private:
    static constexpr float kLowCut = 450.f, kHighCut = 3000.f;
    static constexpr float kMakeup = 1.15f;      // make up the band loss
    static constexpr float kWowBaseMs = 12.f, kWowDepthMs = 1.3f;

    OnePole hp_[3], lp_[3], hissHp_;             // 18 dB/oct on each skirt
    DelayLine wow_;
    std::vector<float> hpScratch_, hissScratch_;
    Rng rng_;
    float rate_ = 48000.f, phase_ = 0.f, envelope_ = 0.f;
};

} // namespace eightd
