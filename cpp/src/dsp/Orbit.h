// Turns a movement mode into a continuous (angle, radius) trajectory.
//
// The angle stays unwrapped so it can be low-pass smoothed without the
// discontinuity a wrapped angle would hit at +/-pi.
#pragma once
#include "Params.h"
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace eightd {

// Small, fast, RT-safe normal deviate for the Random mode.
class Rng {
public:
    float normal() {
        // Box-Muller, one value cached.
        if (has_) { has_ = false; return spare_; }
        float u1 = std::max(uniform(), 1e-7f), u2 = uniform();
        float r = std::sqrt(-2.f * std::log(u1)), th = kTwoPi * u2;
        spare_ = r * std::sin(th); has_ = true;
        return r * std::cos(th);
    }
private:
    float uniform() {
        s_ ^= s_ << 13; s_ ^= s_ >> 17; s_ ^= s_ << 5;
        return float(s_ & 0xFFFFFFu) / float(0x1000000u);
    }
    uint32_t s_ = 0x9E3779B9u;
    float spare_ = 0.f;
    bool has_ = false;
};

struct Trajectory { float theta0, theta1, r0, r1; };

class Orbit {
public:
    void init(float rate) { rate_ = rate; reset(); }
    void reset() {
        phase_ = 0.f; theta_ = thetaSmooth_ = 0.f;
        radius_ = radiusSmooth_ = 1.f; omega_ = 0.f;
    }

    float angle() const  { return thetaSmooth_; }
    float radius() const { return radiusSmooth_; }

    // `motion` scales how much of the elapsed time the trajectory actually
    // travels, so the caller can park the source while nothing plays.  The
    // smoothing still runs on real time, so a parked orbit settles onto its
    // target instead of freezing part-way through an interpolation.
    Trajectory step(uint32_t frames, const Params& p, float motion) {
        const float dt   = float(frames) / rate_;
        const float move = dt * std::clamp(motion, 0.f, 1.f);
        const float thetaPrev  = thetaSmooth_;
        const float radiusPrev = radiusSmooth_;
        const float dir = p.direction >= 0 ? 1.f : -1.f;

        phase_ = std::fmod(phase_ + p.speed * move, 1.f);
        if (phase_ < 0.f) phase_ += 1.f;
        const float ph = phase_ * kTwoPi;
        float radius = p.radius;

        switch (p.mode) {
        case Mode::Circular:
            theta_ += dir * kTwoPi * p.speed * move;
            break;
        case Mode::PingPong: {
            // Triangle wave: constant speed across the field, hard turnarounds.
            const float tri = 4.f * std::fabs(phase_ - 0.5f) - 1.f;
            theta_ = dir * (kPi * 0.5f) * tri;
            break;
        }
        case Mode::Pendulum:
            theta_ = dir * (kPi * 0.5f) * std::sin(ph);
            break;
        case Mode::Linear:
            // Sweeps one way then restarts; smoothing rounds off the jump.
            theta_ = dir * kPi * (phase_ - 0.5f);
            break;
        case Mode::Figure8:
            theta_ = dir * (kPi * 0.5f) * std::sin(ph);
            radius = p.radius * (0.45f + 0.55f * std::fabs(std::cos(ph)));
            break;
        case Mode::Spiral:
            theta_ += dir * kTwoPi * p.speed * move;
            radius = p.radius * (0.4f + 0.6f * (0.5f + 0.5f * std::sin(ph / 3.f)));
            break;
        case Mode::Random: {
            // Ornstein-Uhlenbeck angular velocity: wanders, never jumps.
            const float target = rng_.normal() * kTwoPi * p.speed;
            const float k = 1.f - std::exp(-move / 0.9f);
            omega_ += (target - omega_) * k;
            theta_ += dir * omega_ * move;
            break;
        }
        default:
            theta_ = p.manualAngle;
            break;
        }
        radius_ = radius;

        // Smoothness sets the time constant the renderer chases the target with.
        const float tau   = 0.004f + p.smoothness * p.smoothness * 0.9f;
        const float alpha = 1.f - std::exp(-dt / tau);
        thetaSmooth_  += (theta_ - thetaSmooth_) * alpha;
        radiusSmooth_ += (radius_ - radiusSmooth_) * std::min(alpha * 2.f, 1.f);

        return {thetaPrev, thetaSmooth_, radiusPrev, radiusSmooth_};
    }
private:
    float rate_ = 48000.f;
    float phase_ = 0.f, theta_ = 0.f, thetaSmooth_ = 0.f;
    float radius_ = 1.f, radiusSmooth_ = 1.f, omega_ = 0.f;
    Rng rng_;
};

} // namespace eightd
