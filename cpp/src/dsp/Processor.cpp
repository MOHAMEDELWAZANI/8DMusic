#include "Processor.h"

namespace eightd {

void Processor::init(float rate, uint32_t maxBlock) {
    rate_ = rate;
    maxBlock_ = maxBlock;
    orbit_.init(rate);
    itd_.init(maxBlock + uint32_t(kItdMaxS * rate) + 64);
    echoLine_.init(uint32_t(1.5f * rate) + maxBlock);
    reverb_.init(rate);
    shadowLp_.init(2200.f, rate);   // far-ear head shadow
    rearLp_.init(5200.f, rate);     // front/back pinna cue
    airLp_.init(18000.f, rate);     // distance / air absorption
    pitch_.init(rate, maxBlock);
    radio_.init(rate, maxBlock);
    radio_.prepare(maxBlock);

    wet_.assign(maxBlock * 2, 0.f);
    scratch_.assign(maxBlock * 2, 0.f);
    tail_.assign(maxBlock * 2, 0.f);
    shadow_.assign(maxBlock * 2, 0.f);
    mono_.assign(maxBlock + 8, 0.f);
    acc_.assign(maxBlock + 8, 0.f);
    reset();
}

void Processor::reset() {
    orbit_.reset(); itd_.reset(); echoLine_.reset(); reverb_.reset();
    shadowLp_.reset(); rearLp_.reset(); airLp_.reset();
    pitch_.reset(); radio_.reset();
    limiterGain_ = 1.f; echoTime_ = 0.28f; lastAirCut_ = -1.f;
    quietFor_ = 0.f; playing_ = false; motionLevel_ = 0.f;
}

// How much the orbit should travel this block, 0..1.
//
// Nothing playing means nothing to place, so the source parks where it is
// rather than circling an empty room.  Two thresholds stop a signal sitting on
// the boundary from chattering, and the hold rides out the gap between tracks.
float Processor::gate(const float* x, uint32_t n, const Params& p) {
    const float dt = float(n) / rate_;
    float level = 0.f;
    for (uint32_t i = 0; i < n * 2; ++i) level = std::max(level, std::fabs(x[i]));

    if (level >= kGateOpen) { quietFor_ = 0.f; playing_ = true; }
    else if (level < kGateClose) {
        quietFor_ += dt;
        if (quietFor_ >= kGateHoldS) playing_ = false;
    }
    // between the two thresholds the previous verdict stands

    const float target = (playing_ || !p.pauseWhenSilent) ? 1.f : 0.f;
    const float tau = target > motionLevel_ ? kMotionAttack : kMotionRelease;
    motionLevel_ += (target - motionLevel_) * (1.f - std::exp(-dt / tau));
    if (motionLevel_ < 1e-3f) motionLevel_ = 0.f;   // else it creeps forever
    motion_.store(motionLevel_, std::memory_order_relaxed);
    return motionLevel_;
}

void Processor::process(const float* in, float* out, uint32_t n, const Params& p) {
    if (n == 0) return;
    if (n > maxBlock_) init(rate_, n);   // host handed us more than advertised

    const Trajectory tr = orbit_.step(n, p, gate(in, n, p));
    angle_.store(tr.theta1, std::memory_order_relaxed);
    distance_.store(tr.r1, std::memory_order_relaxed);

    if (!p.enabled) {
        float pl = 0.f, pr = 0.f;
        for (uint32_t i = 0; i < n; ++i) {
            const float l = std::clamp(in[2*i]     * p.outputGain, -1.f, 1.f);
            const float r = std::clamp(in[2*i + 1] * p.outputGain, -1.f, 1.f);
            out[2*i] = l; out[2*i + 1] = r;
            pl = std::max(pl, std::fabs(l)); pr = std::max(pr, std::fabs(r));
        }
        peakL_.store(pl, std::memory_order_relaxed);
        peakR_.store(pr, std::memory_order_relaxed);
        return;
    }

    float* wet = wet_.data();

    // --- stereo width on the source material ------------------------------
    for (uint32_t i = 0; i < n; ++i) {
        const float mid  = (in[2*i] + in[2*i + 1]) * 0.5f;
        const float side = (in[2*i] - in[2*i + 1]) * 0.5f * p.width;
        wet[2*i] = mid + side; wet[2*i + 1] = mid - side;
    }

    // --- tone character, applied to the source ----------------------------
    const float amount = std::clamp(p.characterAmount, 0.f, 1.f);
    if (amount > 0.002f) {
        if (p.character == Character::Slowed) {
            pitch_.process(wet, scratch_.data(), n, 1.f - 0.18f * amount);
            std::memcpy(wet, scratch_.data(), n * 2 * sizeof(float));
        } else if (p.character == Character::Radio) {
            radio_.process(wet, scratch_.data(), n, amount, tail_.data());
            std::memcpy(wet, scratch_.data(), n * 2 * sizeof(float));
        }
    }

    // --- position ----------------------------------------------------------
    // theta ramps linearly across the block, so instead of a sin/cos pair per
    // sample we seed the pair once and advance it by a fixed rotation.
    const float dTheta = (tr.theta1 - tr.theta0) / float(n);
    const float rotC = std::cos(dTheta), rotS = std::sin(dTheta);
    float cosT = std::cos(tr.theta0), sinT = std::sin(tr.theta0);

    const float depth = std::clamp(p.depth, 0.f, 1.f);
    const float dR = (tr.r1 - tr.r0) / float(n);
    float radius = tr.r0;
    float radiusSum = 0.f;

    itd_.write(wet, n);
    const double base = double(itd_.t()) - n;

    for (uint32_t i = 0; i < n; ++i) {
        const float r = std::clamp(radius, 0.25f, 4.f);
        radiusSum += r;

        const float lateral   = sinT * depth;   // -1 hard left .. +1 hard right
        const float frontness = cosT;           // +1 in front, -1 behind

        // Close sources push the ears further apart perceptually.
        const float near = std::clamp(1.f + 0.35f / r, 1.f, 1.6f);

        // interaural time difference
        const float itd = kItdMaxS * rate_ * 0.5f * near;
        const float dl = 1.f + itd * (1.f + lateral);
        const float dr = 1.f + itd * (1.f - lateral);
        const double at = base + i;
        float l = itd_.readAt(at, dl, 0);
        float rr = itd_.readAt(at, dr, 1);

        // interaural level difference, constant power
        const float ild = std::clamp(1.15f / r, 0.35f, 1.8f);
        const float pan = std::clamp(lateral * ild, -1.f, 1.f);
        const float phi = (kPi * 0.25f) * (1.f + pan);
        const float distGain = std::pow(1.f / r, 0.6f);
        l  *= std::cos(phi) * 1.41421356f * distGain;
        rr *= std::sin(phi) * 1.41421356f * distGain;

        wet[2*i] = l; wet[2*i + 1] = rr;
        scratch_[2*i]     = std::clamp(lateral, 0.f, 1.f) * 0.8f;   // source right
        scratch_[2*i + 1] = std::clamp(-lateral, 0.f, 1.f) * 0.8f;  // source left
        tail_[2*i] = tail_[2*i + 1] =
            std::clamp(-frontness, 0.f, 1.f) * 0.35f * depth;

        // advance the rotation
        const float nc = cosT * rotC - sinT * rotS;
        sinT = sinT * rotC + cosT * rotS;
        cosT = nc;
        radius += dR;
    }

    // --- head shadow: the far ear loses highs -------------------------------
    float* shadow = shadow_.data();
    shadowLp_.process(wet, shadow, n);
    for (uint32_t i = 0; i < n; ++i) {
        wet[2*i]     += (shadow[2*i]     - wet[2*i])     * scratch_[2*i];
        wet[2*i + 1] += (shadow[2*i + 1] - wet[2*i + 1]) * scratch_[2*i + 1];
    }

    // --- front/back cue ------------------------------------------------------
    rearLp_.process(wet, shadow, n);
    for (uint32_t i = 0; i < n; ++i) {
        wet[2*i]     += (shadow[2*i]     - wet[2*i])     * tail_[2*i];
        wet[2*i + 1] += (shadow[2*i + 1] - wet[2*i + 1]) * tail_[2*i + 1];
    }

    // --- air absorption grows with distance ----------------------------------
    const float meanR = radiusSum / float(n);
    const float cut = std::clamp(19000.f * std::exp(-0.30f * (meanR - 1.f)),
                                 2500.f, 20000.f);
    if (std::fabs(cut - lastAirCut_) > 50.f) { airLp_.setCutoff(cut); lastAirCut_ = cut; }
    if (meanR > 1.05f) airLp_.process(wet, wet, n);
    else               airLp_.process(wet, shadow, n);          // keep state warm

    // --- ping-pong echo -------------------------------------------------------
    if (p.delayMix > 0.001f) echo(wet, n, p);

    // --- reverb ----------------------------------------------------------------
    // Distance is part of the illusion: further away means more room in the mix.
    float wetAmount = std::clamp(p.reverbMix, 0.f, 1.f)
                    * std::clamp(0.55f + 0.45f * (meanR - 0.25f) / 2.75f, 0.4f, 1.3f);
    if (wetAmount > 0.002f) {
        reverb_.process(wet, tail_.data(), n, p.reverbSize, p.reverbDamp,
                        mono_.data(), acc_.data());
        const float dry = 1.f - 0.5f * wetAmount, w = wetAmount * 3.f;
        for (uint32_t i = 0; i < n * 2; ++i) wet[i] = wet[i] * dry + tail_[i] * w;
    }

    // --- output stage ------------------------------------------------------------
    for (uint32_t i = 0; i < n * 2; ++i) wet[i] *= p.outputGain;
    limit(wet, n);

    float pl = 0.f, pr = 0.f;
    for (uint32_t i = 0; i < n; ++i) {
        out[2*i] = wet[2*i]; out[2*i + 1] = wet[2*i + 1];
        pl = std::max(pl, std::fabs(wet[2*i]));
        pr = std::max(pr, std::fabs(wet[2*i + 1]));
    }
    peakL_.store(pl, std::memory_order_relaxed);
    peakR_.store(pr, std::memory_order_relaxed);
}

void Processor::echo(float* io, uint32_t n, const Params& p) {
    const float target = std::clamp(p.delayTime, 0.04f, 1.4f);
    // Glide the delay time so changes bend tape-style instead of clicking.
    const float prev = echoTime_;
    echoTime_ += (target - echoTime_) * 0.25f;
    const float d0 = std::max(prev * rate_, float(n) + 2.f);
    const float d1 = std::max(echoTime_ * rate_, float(n) + 2.f);
    const float step = (d1 - d0) / float(n);

    const float fb  = std::clamp(p.delayFeedback, 0.f, 0.85f);
    const float mix = std::clamp(p.delayMix, 0.f, 1.f);
    const double base = double(echoLine_.t());

    for (uint32_t i = 0; i < n; ++i) {
        const float d = d0 + step * float(i);
        const double at = base + i;
        const float tapL = echoLine_.readAt(at, d, 0);
        const float tapR = echoLine_.readAt(at, d, 1);
        const float inL = io[2*i], inR = io[2*i + 1];
        // cross-fed: taps bounce ear to ear
        scratch_[2*i]     = inL + tapR * fb;
        scratch_[2*i + 1] = inR + tapL * fb;
        io[2*i]     = inL + tapL * mix;
        io[2*i + 1] = inR + tapR * mix;
    }
    echoLine_.write(scratch_.data(), n);
}

void Processor::limit(float* io, uint32_t n) {
    constexpr float ceiling = 0.98f;
    float peak = 0.f;
    for (uint32_t i = 0; i < n * 2; ++i) peak = std::max(peak, std::fabs(io[i]));
    const float target = peak > ceiling ? ceiling / peak : 1.f;
    const float start = limiterGain_;
    if (target < start) limiterGain_ = target;                    // fast attack
    else                limiterGain_ += (target - start) * 0.05f; // slow release
    const float step = (limiterGain_ - start) / float(n);
    for (uint32_t i = 0; i < n; ++i) {
        const float g = start + step * float(i);
        io[2*i]     = std::clamp(io[2*i]     * g, -1.f, 1.f);
        io[2*i + 1] = std::clamp(io[2*i + 1] * g, -1.f, 1.f);
    }
}

} // namespace eightd
