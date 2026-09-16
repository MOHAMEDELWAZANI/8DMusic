// Colour tokens for the v2 interface.
//
// The names come straight from the design: a ground the window sits on, cards
// raised off it, wells sunk into the cards, and one accent plus one motion
// colour used sparingly.  Both themes fill the same slots, so nothing below
// this file ever asks which theme is running.
#pragma once
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
    bool dark = true;

    // surfaces, darkest to lightest
    Rgb ground, card, well, raised, line;
    // type, strongest to weakest
    Rgb text, dim, faint, ghost;
    // the two colours that carry meaning
    Rgb accent, deep, tint, onAccent;
    Rgb motion, onMotion;
    // states
    Rgb good, warn;

    static Theme darkTheme() {
        Theme t;
        t.dark   = true;
        t.ground = Rgb::hex(0x161514);
        t.card   = Rgb::hex(0x211F1E);
        t.well   = Rgb::hex(0x2B2927);
        t.raised = Rgb::hex(0x46423F);
        t.line   = Rgb::hex(0x3A3735);
        t.text   = Rgb::hex(0xF3F2F2);
        t.dim    = Rgb::hex(0xBAB6B6);
        t.faint  = Rgb::hex(0x9B9797);
        t.ghost  = Rgb::hex(0x6B6766);
        t.accent = Rgb::hex(0x62C5EE);
        t.deep   = Rgb::hex(0x99E0FF);
        t.tint   = Rgb::hex(0x0E3342);
        t.onAccent = Rgb::hex(0x08222D);
        t.motion   = Rgb::hex(0xFF458E);
        t.onMotion = Rgb::hex(0x3D0A1F);
        t.good = Rgb::hex(0x35D08A);
        t.warn = Rgb::hex(0xE8A73C);
        return t;
    }

    // The same interface in daylight.  The ladder is inverted -- cards sit
    // above the ground rather than below it -- so every card, well and tile in
    // the layout keeps working without a single special case.
    static Theme light() {
        Theme t;
        t.dark   = false;
        t.ground = Rgb::hex(0xEFEDEA);
        t.card   = Rgb::hex(0xFFFFFF);
        t.well   = Rgb::hex(0xF1EEEB);
        t.raised = Rgb::hex(0xDFDAD5);
        t.line   = Rgb::hex(0xDDD8D3);
        t.text   = Rgb::hex(0x1A1817);
        t.dim    = Rgb::hex(0x56514D);
        t.faint  = Rgb::hex(0x7C7671);
        t.ghost  = Rgb::hex(0xA8A29C);
        t.accent = Rgb::hex(0x0E7FA8);
        t.deep   = Rgb::hex(0x0A5E7D);
        t.tint   = Rgb::hex(0xDCEEF6);
        t.onAccent = Rgb::hex(0xFFFFFF);
        t.motion   = Rgb::hex(0xD81B60);
        t.onMotion = Rgb::hex(0xFFFFFF);
        t.good = Rgb::hex(0x1C9E63);
        t.warn = Rgb::hex(0xB97A0F);
        return t;
    }
};

} // namespace eightd
