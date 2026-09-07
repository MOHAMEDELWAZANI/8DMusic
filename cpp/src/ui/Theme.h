// Colour tokens.  Both themes are built from the handful of colours a theme
// really has, so a change to one ground colour ripples through consistently.
#pragma once
#include <string>
#include <cstdint>
#include <cmath>

namespace eightd {

struct Rgb {
    double r = 0, g = 0, b = 0;
    static Rgb hex(uint32_t v) {
        return {((v >> 16) & 0xFF) / 255.0, ((v >> 8) & 0xFF) / 255.0, (v & 0xFF) / 255.0};
    }
};

inline Rgb mix(Rgb a, Rgb b, double t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

struct Theme {
    bool dark = false;
    Rgb ground, chrome, rail, field, line, lineSoft;
    Rgb ink, inkSoft, inkFaint, inkGhost;
    Rgb accent, accentText, accentSoft, onAccent;
    Rgb motion, motionText, onMotion;
    Rgb good, warn;

    static Theme light() {
        Theme t;
        t.dark = false;
        t.ground   = Rgb::hex(0xF2F2F0);
        t.chrome   = Rgb::hex(0xFFFFFF);
        t.rail     = Rgb::hex(0xF7F7F5);
        t.field    = Rgb::hex(0xFFFFFF);
        t.line     = Rgb::hex(0xD8D8D4);
        t.lineSoft = Rgb::hex(0xE7E7E3);
        t.ink      = Rgb::hex(0x16181C);
        t.inkSoft  = Rgb::hex(0x5B6068);
        t.inkFaint = Rgb::hex(0x8A8F98);
        t.inkGhost = Rgb::hex(0xB4B8BF);
        t.accent     = Rgb::hex(0x0E7FA8);
        t.accentText = Rgb::hex(0x0E7FA8);
        t.accentSoft = Rgb::hex(0xE3F0F6);
        t.onAccent   = Rgb::hex(0xFFFFFF);
        t.motion     = Rgb::hex(0xE8225F);
        t.motionText = Rgb::hex(0xC01A4E);
        t.onMotion   = Rgb::hex(0xFFFFFF);
        t.good = Rgb::hex(0x1C9E63);
        t.warn = Rgb::hex(0xD08A16);
        return t;
    }
    static Theme darkTheme() {
        Theme t;
        t.dark = true;
        t.ground   = Rgb::hex(0x0B0C0E);
        t.chrome   = Rgb::hex(0x121417);
        t.rail     = Rgb::hex(0x101215);
        t.field    = Rgb::hex(0x1A1D22);
        t.line     = Rgb::hex(0x2A2E35);
        t.lineSoft = Rgb::hex(0x1E2228);
        t.ink      = Rgb::hex(0xEDEFF2);
        t.inkSoft  = Rgb::hex(0x9AA1AC);
        t.inkFaint = Rgb::hex(0x6F7681);
        t.inkGhost = Rgb::hex(0x4A505A);
        t.accent     = Rgb::hex(0x4FC3F7);
        t.accentText = Rgb::hex(0x4FC3F7);
        t.accentSoft = Rgb::hex(0x16303C);
        t.onAccent   = Rgb::hex(0x04202B);
        t.motion     = Rgb::hex(0xFF4D8D);
        t.motionText = Rgb::hex(0xFF6FA3);
        t.onMotion   = Rgb::hex(0x2B0715);
        t.good = Rgb::hex(0x35D08A);
        t.warn = Rgb::hex(0xE8A73C);
        return t;
    }
};

} // namespace eightd
