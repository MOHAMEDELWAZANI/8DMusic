// Stereo Freeverb.
#pragma once
#include "Filters.h"

namespace eightd {

class Reverb {
public:
    void init(float rate) {
        static const int kComb[8]   = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
        static const int kAllPass[4]= {556, 441, 341, 225};
        const float scale = rate / 44100.f;
        const int spread  = 23;
        size_t shortest = SIZE_MAX;
        for (int side = 0; side < 2; ++side) {
            const int off = side ? spread : 0;
            for (int i = 0; i < 8; ++i)
                combs_[side][i].init(size_t(std::max(int(kComb[i] * scale) + off, 32)));
            for (int i = 0; i < 4; ++i) {
                allpass_[side][i].init(size_t(std::max(int(kAllPass[i] * scale) + off, 16)));
                shortest = std::min(shortest, allpass_[side][i].size());
            }
        }
        // Sub-block short enough that every feedback path stays causal.
        sub_ = std::max<uint32_t>(32, uint32_t(shortest) - 1);
    }

    void reset() {
        for (int s = 0; s < 2; ++s) {
            for (auto& c : combs_[s]) c.reset();
            for (auto& a : allpass_[s]) a.reset();
        }
    }

    // `in` interleaved stereo, `out` interleaved stereo (must not alias).
    // `mono` and `acc` are caller-owned scratch of at least `n` floats.
    void process(const float* in, float* out, uint32_t n, float size, float damp,
                 float* mono, float* acc) {
        const float feedback = 0.7f + 0.28f * std::clamp(size, 0.f, 1.f);
        const float damping  = std::clamp(damp, 0.f, 0.95f) * 0.4f;
        for (uint32_t start = 0; start < n; start += sub_) {
            const uint32_t len = std::min(sub_, n - start);
            const float* src = in + start * 2;
            for (uint32_t i = 0; i < len; ++i)
                mono[i] = (src[2*i] + src[2*i + 1]) * 0.015f;
            for (int ch = 0; ch < 2; ++ch) {
                std::memset(acc, 0, len * sizeof(float));
                for (auto& c : combs_[ch]) c.process(mono, acc, len, feedback, damping);
                for (auto& a : allpass_[ch]) a.process(acc, len);
                float* dst = out + start * 2 + ch;
                for (uint32_t i = 0; i < len; ++i) dst[2*i] = acc[i];
            }
        }
    }
private:
    Comb combs_[2][8];
    AllPass allpass_[2][4];
    uint32_t sub_ = 32;
};

} // namespace eightd
