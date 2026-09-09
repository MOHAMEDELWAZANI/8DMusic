// Effect settings, and the presets that drive them.
#pragma once
#include <cstdint>
#include <cmath>

namespace eightd {

inline constexpr float kTwoPi   = 6.283185307179586f;
inline constexpr float kPi      = 3.14159265358979f;
inline constexpr float kItdMaxS = 0.00070f;   // head radius / speed of sound

// Silence gate: two thresholds so a signal sitting on the boundary cannot
// chatter, and a hold long enough to ride out the gap between tracks.
inline constexpr float kGateOpen     = 3.0e-4f;   // about -70 dBFS peak
inline constexpr float kGateClose    = 1.0e-4f;   // about -80 dBFS peak
inline constexpr float kGateHoldS    = 0.7f;
inline constexpr float kMotionAttack = 0.08f;     // picks up almost at once
inline constexpr float kMotionRelease= 0.25f;     // but coasts to a stop

enum class Mode : uint8_t {
    Circular, PingPong, Pendulum, Linear, Figure8, Spiral, Random, Static, Count
};
enum class Character : uint8_t { Clean, Slowed, Radio, Count };

inline const char* modeLabel(Mode m) {
    switch (m) {
        case Mode::Circular: return "Circular orbit";
        case Mode::PingPong: return "Ping-pong";
        case Mode::Pendulum: return "Pendulum";
        case Mode::Linear:   return "Linear sweep";
        case Mode::Figure8:  return "Figure eight";
        case Mode::Spiral:   return "Spiral";
        case Mode::Random:   return "Random drift";
        default:             return "Static position";
    }
}
inline const char* characterLabel(Character c) {
    switch (c) {
        case Character::Slowed: return "Slowed & sad";
        case Character::Radio:  return "Old radio";
        default:                return "Clean";
    }
}
inline const char* characterHint(Character c) {
    switch (c) {
        case Character::Slowed: return "Lower and heavier, the way slowed edits sound";
        case Character::Radio:  return "Mono, band-limited, softly saturated";
        default:                return "The source passes through untouched";
    }
}

// Plain trivially-copyable data: the audio thread takes a snapshot of this by
// value, so there is nothing to lock and nothing to allocate.
struct Params {
    bool  enabled          = true;
    Mode  mode             = Mode::Circular;
    float speed            = 0.12f;   // orbits per second
    float radius           = 1.0f;    // virtual distance, metres
    float depth            = 0.85f;   // travel through the stereo field, 0..1
    float smoothness       = 0.35f;   // 0 snappy .. 1 very gradual
    float width            = 1.0f;    // stereo width of the source, 0..2
    int   direction        = 1;       // +1 clockwise, -1 counter-clockwise
    float manualAngle      = 0.0f;    // radians, used by Static
    bool  pauseWhenSilent  = true;

    Character character    = Character::Clean;
    float characterAmount  = 1.0f;

    float delayMix         = 0.0f;
    float delayTime        = 0.28f;   // seconds
    float delayFeedback    = 0.35f;

    float reverbMix        = 0.18f;
    float reverbSize       = 0.6f;
    float reverbDamp       = 0.45f;

    float outputGain       = 0.9f;

    // Three-band tone control, in dB.  All three at 0 is a true bypass.
    float eqBass           = 0.0f;
    float eqMid            = 0.0f;
    float eqTreble         = 0.0f;
};

struct Preset { const char* name; Params p; };

// Built from the defaults, then overridden -- same values as the Python build,
// so the two versions sound identical.
inline Params preset(Mode m, float speed, float radius, float depth, float smooth,
                     float width, float dMix, float dTime, float dFb,
                     float rMix, float rSize, float rDamp,
                     Character ch = Character::Clean, float chAmt = 1.0f) {
    Params p;
    p.mode = m; p.speed = speed; p.radius = radius; p.depth = depth;
    p.smoothness = smooth; p.width = width;
    p.delayMix = dMix; p.delayTime = dTime; p.delayFeedback = dFb;
    p.reverbMix = rMix; p.reverbSize = rSize; p.reverbDamp = rDamp;
    p.character = ch; p.characterAmount = chAmt;
    return p;
}

inline const Preset* presets(int& count) {
    static const Preset kPresets[] = {
        {"Classic 8D",   preset(Mode::Circular, 0.12f, 1.0f,  0.9f,  0.35f, 1.15f, 0.0f,  0.28f, 0.35f, 0.18f, 0.6f,  0.45f)},
        {"Slow Orbit",   preset(Mode::Circular, 0.05f, 1.4f,  0.8f,  0.6f,  1.1f,  0.0f,  0.28f, 0.35f, 0.28f, 0.72f, 0.45f)},
        {"Ping-Pong",    preset(Mode::PingPong, 0.35f, 0.8f,  1.0f,  0.25f, 1.0f,  0.22f, 0.22f, 0.4f,  0.12f, 0.6f,  0.45f)},
        {"Wide Cinema",  preset(Mode::Pendulum, 0.07f, 1.8f,  0.65f, 0.75f, 1.5f,  0.12f, 0.4f,  0.3f,  0.4f,  0.82f, 0.3f)},
        {"Subtle Motion",preset(Mode::Pendulum, 0.06f, 1.1f,  0.35f, 0.8f,  1.05f, 0.0f,  0.28f, 0.35f, 0.08f, 0.6f,  0.45f)},
        {"Extreme Spin", preset(Mode::Circular, 0.65f, 0.5f,  1.0f,  0.1f,  1.3f,  0.1f,  0.15f, 0.45f, 0.2f,  0.6f,  0.45f)},
        {"Deep Space",   preset(Mode::Spiral,   0.09f, 2.4f,  0.9f,  0.65f, 1.4f,  0.3f,  0.5f,  0.5f,  0.55f, 0.88f, 0.25f)},
        {"Figure Eight", preset(Mode::Figure8,  0.15f, 1.2f,  0.95f, 0.4f,  1.2f,  0.08f, 0.28f, 0.35f, 0.2f,  0.6f,  0.45f)},
        {"Slowed & Sad", preset(Mode::Circular, 0.045f,1.7f,  0.85f, 0.82f, 1.3f,  0.2f,  0.6f,  0.44f, 0.52f, 0.86f, 0.35f, Character::Slowed, 0.85f)},
        {"Old Radio",    preset(Mode::Pendulum, 0.05f, 1.15f, 0.4f,  0.7f,  0.4f,  0.0f,  0.28f, 0.35f, 0.24f, 0.5f,  0.62f, Character::Radio,  1.0f)},
    };
    count = int(sizeof(kPresets) / sizeof(kPresets[0]));
    return kPresets;
}

} // namespace eightd
