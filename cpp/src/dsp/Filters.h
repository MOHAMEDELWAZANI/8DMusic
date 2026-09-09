// Building blocks.  Everything preallocates: nothing here touches the heap
// once the processor is constructed, so the audio callback never blocks.
#pragma once
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <cstdint>

namespace eightd {

// One-pole low-pass, stereo, state carried across blocks.
class OnePole {
public:
    void init(float cutoff, float rate) { rate_ = rate; setCutoff(cutoff); reset(); }
    void reset() { z_[0] = z_[1] = 0.f; }

    void setCutoff(float fc) {
        fc = std::clamp(fc, 20.f, rate_ * 0.45f);
        pole_ = std::exp(-6.283185307179586f * fc / rate_);
        gain_ = 1.f - pole_;
    }
    float cutoff() const { return cutoff_; }

    // Filters interleaved stereo in place into `dst` (may alias `src`).
    void process(const float* src, float* dst, uint32_t n) {
        float a = z_[0], b = z_[1], g = gain_, p = pole_;
        for (uint32_t i = 0; i < n; ++i) {
            a = g * src[2*i]     + p * a;
            b = g * src[2*i + 1] + p * b;
            dst[2*i] = a; dst[2*i + 1] = b;
        }
        z_[0] = a; z_[1] = b;
    }
private:
    float rate_ = 48000.f, pole_ = 0.f, gain_ = 1.f, cutoff_ = 0.f;
    float z_[2] = {0.f, 0.f};
};

// Ring buffer with per-sample fractional reads.
//
// `write` advances an absolute sample counter; `read` takes the delays relative
// to a caller-supplied origin, so a block can be read either before or after it
// is written as long as every delay is at least one block long.
class DelayLine {
public:
    void init(uint32_t maxDelay) {
        size_t s = 1;
        while (s < size_t(maxDelay) + 8) s <<= 1;
        size_ = s; mask_ = s - 1;
        buf_.assign(s * 2, 0.f);
        t_ = 0;
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); t_ = 0; }
    uint64_t t() const { return t_; }

    void write(const float* src, uint32_t n) {
        for (uint32_t i = 0; i < n; ++i) {
            size_t s = ((t_ + i) & mask_) * 2;
            buf_[s] = src[2*i]; buf_[s + 1] = src[2*i + 1];
        }
        t_ += n;
    }

    // One sample, channel `ch`, `delay` samples back from absolute time `at`.
    inline float readAt(double at, float delay, int ch) const {
        double pos = at - double(std::max(delay, 1.f));
        double fl  = std::floor(pos);
        float  fr  = float(pos - fl);
        int64_t i0 = int64_t(fl);
        const float a = buf_[((size_t)(i0)     & mask_) * 2 + ch];
        const float b = buf_[((size_t)(i0 + 1) & mask_) * 2 + ch];
        return a + (b - a) * fr;
    }
private:
    std::vector<float> buf_;
    size_t size_ = 0, mask_ = 0;
    uint64_t t_ = 0;
};

// Freeverb comb: a delay whose feedback path is low-passed.
class Comb {
public:
    void init(size_t size) { buf_.assign(std::max<size_t>(size, 32), 0.f); i_ = 0; store_ = 0.f; }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); store_ = 0.f; }

    void process(const float* in, float* acc, uint32_t n, float feedback, float damp) {
        const size_t size = buf_.size();
        size_t i = i_;
        float store = store_;
        for (uint32_t s = 0; s < n; ++s) {
            const float out = buf_[i];
            acc[s] += out;
            store = out * (1.f - damp) + store * damp;
            buf_[i] = in[s] + store * feedback;
            if (++i >= size) i = 0;
        }
        i_ = i; store_ = store;
    }
private:
    std::vector<float> buf_;
    size_t i_ = 0;
    float store_ = 0.f;
};

// Freeverb all-pass diffuser (fixed 0.5 feedback).
class AllPass {
public:
    void init(size_t size) { buf_.assign(std::max<size_t>(size, 16), 0.f); i_ = 0; }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.f); }
    size_t size() const { return buf_.size(); }

    void process(float* io, uint32_t n) {
        const size_t size = buf_.size();
        size_t i = i_;
        for (uint32_t s = 0; s < n; ++s) {
            const float buffered = buf_[i];
            const float in = io[s];
            io[s]   = buffered - in;
            buf_[i] = in + buffered * 0.5f;
            if (++i >= size) i = 0;
        }
        i_ = i;
    }
private:
    std::vector<float> buf_;
    size_t i_ = 0;
};


// Three-band tone control: bass shelf, mid peak, treble shelf.
//
// Flat is the default and is a true no-op -- with all three gains at 0 dB the
// samples pass through untouched, so a build that never shows the equaliser
// still produces bit-identical output to one that does.
class ToneStack {
public:
    void init(float rate) {
        rate_ = rate;
        set(0.f, 0.f, 0.f);
        reset();
    }
    void reset() { for (auto& b : band_) b.reset(); }

    // Gains in dB, each roughly -12 .. +12.
    void set(float bassDb, float midDb, float trebleDb) {
        if (bassDb == bass_ && midDb == mid_ && treble_ == trebleDb) return;
        bass_ = bassDb; mid_ = midDb; treble_ = trebleDb;
        flat_ = (bassDb == 0.f && midDb == 0.f && trebleDb == 0.f);
        band_[0].lowShelf(120.f, bassDb, rate_);
        band_[1].peak(1000.f, 0.9f, midDb, rate_);
        band_[2].highShelf(4500.f, trebleDb, rate_);
    }

    void process(float* io, uint32_t n) {
        if (flat_) return;
        for (auto& b : band_) b.process(io, n);
    }

private:
    // Stereo biquad, direct form I, coefficients from the RBJ cookbook.
    struct Biquad {
        void reset() { std::memset(x1, 0, sizeof x1); std::memset(x2, 0, sizeof x2);
                       std::memset(y1, 0, sizeof y1); std::memset(y2, 0, sizeof y2); }

        void lowShelf(float f, float dB, float rate) {
            const double A = std::pow(10.0, dB / 40.0);
            const double w = 6.283185307179586 * f / rate;
            const double cw = std::cos(w), sw = std::sin(w);
            const double alpha = sw / 2.0 * std::sqrt((A + 1 / A) * (1 / 0.9 - 1) + 2);
            const double t = 2 * std::sqrt(A) * alpha;
            set((A + 1) - (A - 1) * cw + t, 2 * ((A - 1) - (A + 1) * cw),
                (A + 1) - (A - 1) * cw - t,
                A * ((A + 1) + (A - 1) * cw + t), -2 * A * ((A - 1) + (A + 1) * cw),
                A * ((A + 1) + (A - 1) * cw - t));
        }
        void highShelf(float f, float dB, float rate) {
            const double A = std::pow(10.0, dB / 40.0);
            const double w = 6.283185307179586 * f / rate;
            const double cw = std::cos(w), sw = std::sin(w);
            const double alpha = sw / 2.0 * std::sqrt((A + 1 / A) * (1 / 0.9 - 1) + 2);
            const double t = 2 * std::sqrt(A) * alpha;
            set((A + 1) + (A - 1) * cw + t, -2 * ((A - 1) + (A + 1) * cw),
                (A + 1) + (A - 1) * cw - t,
                A * ((A + 1) - (A - 1) * cw + t), 2 * A * ((A - 1) - (A + 1) * cw),
                A * ((A + 1) - (A - 1) * cw - t));
        }
        void peak(float f, float q, float dB, float rate) {
            const double A = std::pow(10.0, dB / 40.0);
            const double w = 6.283185307179586 * f / rate;
            const double alpha = std::sin(w) / (2 * q);
            set(1 + alpha / A, -2 * std::cos(w), 1 - alpha / A,
                1 + alpha * A, -2 * std::cos(w), 1 - alpha * A);
        }

        void process(float* io, uint32_t n) {
            for (int c = 0; c < 2; ++c) {
                float X1 = x1[c], X2 = x2[c], Y1 = y1[c], Y2 = y2[c];
                for (uint32_t i = 0; i < n; ++i) {
                    const float x = io[2 * i + c];
                    const float y = b0 * x + b1 * X1 + b2 * X2 - a1 * Y1 - a2 * Y2;
                    X2 = X1; X1 = x; Y2 = Y1; Y1 = y;
                    io[2 * i + c] = y;
                }
                x1[c] = X1; x2[c] = X2; y1[c] = Y1; y2[c] = Y2;
            }
        }

        void set(double a0, double A1, double A2, double B0, double B1, double B2) {
            b0 = float(B0 / a0); b1 = float(B1 / a0); b2 = float(B2 / a0);
            a1 = float(A1 / a0); a2 = float(A2 / a0);
        }

        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float x1[2]{}, x2[2]{}, y1[2]{}, y2[2]{};
    };

    float rate_ = 48000.f;
    float bass_ = 1e9f, mid_ = 1e9f, treble_ = 1e9f;
    bool  flat_ = true;
    Biquad band_[3];
};

} // namespace eightd
