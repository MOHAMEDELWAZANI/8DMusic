// The glyphs, as path data lifted from the design.
//
// Each icon carries the viewBox it was drawn in, an outline to stroke and a
// shape to fill; either may be empty.  Drawing one is then a matter of
// choosing a colour and a weight -- the geometry is already agreed with the
// design file.
//
// Data only, and deliberately: this file has no drawing library behind it, so
// the Windows build compiles the same glyphs against GDI+ rather than keeping
// a second set that can drift.
#pragma once

namespace eightd {

struct Icon {
    double box = 24;
    const char* stroke = nullptr;
    const char* fill = nullptr;
    double weight = 1.8;      // stroke width, in viewBox units
};

namespace ico {

// -- circles, written out as arcs so the parser can take them ---------------
#define CIRC(cx, r) "M" #cx "," #r

// tabs -----------------------------------------------------------------------
inline const Icon kStudio{24,
    "M20.5 12a8.5 8.5 0 1 1-17 0a8.5 8.5 0 1 1 17 0"
    "M14.2 12a2.2 2.2 0 1 1-4.4 0a2.2 2.2 0 1 1 4.4 0",
    "M20.4 6a2.4 2.4 0 1 1-4.8 0a2.4 2.4 0 1 1 4.8 0", 1.8};

inline const Icon kAbout{24,
    "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0 M12 11v6 M12 7.4v.2", nullptr, 1.8};

inline const Icon kAccount{24,
    "M16 8a4 4 0 1 1-8 0a4 4 0 1 1 8 0 M4 21a8 8 0 0 1 16 0", nullptr, 1.8};

// movement modes (28-unit grid, as in the tiles) ------------------------------
inline const Icon kCircular{28,
    "M23 14a9 9 0 1 1-18 0a9 9 0 1 1 18 0",
    "M16 5a2 2 0 1 1-4 0a2 2 0 1 1 4 0", 1.7};
inline const Icon kPingPong{28,
    "M4 14h20 M8 10l-4 4 4 4 M20 10l4 4-4 4", nullptr, 1.7};
inline const Icon kPendulum{28,
    "M4 9q10 16 20 0", "M26 9a2 2 0 1 1-4 0a2 2 0 1 1 4 0", 1.7};
inline const Icon kLinear{28,
    "M4 21L24 7", "M26 7a2 2 0 1 1-4 0a2 2 0 1 1 4 0", 1.7};
inline const Icon kFigure8{28,
    "M14.5 14a5.5 5.5 0 1 1-11 0a5.5 5.5 0 1 1 11 0"
    "M24.5 14a5.5 5.5 0 1 1-11 0a5.5 5.5 0 1 1 11 0", nullptr, 1.7};
inline const Icon kSpiral{28,
    "M23 14a9 9 0 1 1-18 0a9 9 0 1 1 18 0"
    "M19.5 14a5.5 5.5 0 1 1-11 0a5.5 5.5 0 1 1 11 0"
    "M16.5 14a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0", nullptr, 1.7};
inline const Icon kRandom{28,
    "M4 19l5-8 4 6 5-10 3 6 3-3", nullptr, 1.7};
inline const Icon kStaticPos{28,
    nullptr, "M23 7.5a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0", 1.7};
// The dashed ring of the Static tile, drawn separately so it can carry a dash.
inline const Icon kStaticRing{28,
    "M23 14a9 9 0 1 1-18 0a9 9 0 1 1 18 0", nullptr, 1.7};

// character -------------------------------------------------------------------
inline const Icon kClean{28,  "M3 14h22", nullptr, 1.7};
inline const Icon kSlowed{28,
    "M3 11c3.5-5 7-5 11 0s7.5 5 11 0 M3 18c3.5-4 7-4 11 0s7.5 4 11 0", nullptr, 1.7};
inline const Icon kRadio{28,
    "M8 9h12a4 4 0 0 1 4 4v6a4 4 0 0 1-4 4H8a4 4 0 0 1-4-4v-6a4 4 0 0 1 4-4z"
    "M9 9l9-5 M21 16a3 3 0 1 1-6 0a3 3 0 1 1 6 0 M8 14h3 M8 18h3", nullptr, 1.7};

// transport and chrome ---------------------------------------------------------
inline const Icon kPlay{24, nullptr,
    "M6 4.5v15a1 1 0 0 0 1.5.87l12-7.5a1 1 0 0 0 0-1.74l-12-7.5A1 1 0 0 0 6 4.5z"};
inline const Icon kPause{24, nullptr,
    "M5 5a2 2 0 0 1 2-2h1a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2z"
    "M14 5a2 2 0 0 1 2-2h1a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2h-1a2 2 0 0 1-2-2z"};
inline const Icon kPrev{24, nullptr,
    "M20 5.5v13a1 1 0 0 1-1.5.9L9 13.1a1.3 1.3 0 0 1 0-2.2l9.5-6.3a1 1 0 0 1 1.5.9z"
    "M5.5 5h1A1.5 1.5 0 0 1 8 6.5v11A1.5 1.5 0 0 1 6.5 19h-1A1.5 1.5 0 0 1 4 17.5v-11"
    "A1.5 1.5 0 0 1 5.5 5z"};
inline const Icon kNext{24, nullptr,
    "M4 5.5v13a1 1 0 0 0 1.5.9L15 13.1a1.3 1.3 0 0 0 0-2.2L5.5 4.6A1 1 0 0 0 4 5.5z"
    "M17.5 5h1A1.5 1.5 0 0 1 20 6.5v11a1.5 1.5 0 0 1-1.5 1.5h-1a1.5 1.5 0 0 1-1.5-1.5"
    "v-11A1.5 1.5 0 0 1 17.5 5z"};
inline const Icon kPlus{24, "M12 5v14 M5 12h14", nullptr, 1.8};
inline const Icon kChevron{24, "M9 5l7 7-7 7", nullptr, 2.0};
inline const Icon kExternal{24, "M7 17L17 7 M8 7h9v9", nullptr, 2.0};
inline const Icon kArrowUp{24, "M12 19V5 M6 11l6-6 6 6", nullptr, 2.4};
inline const Icon kArrowDown{24, "M12 5v14 M6 13l6 6 6-6", nullptr, 2.4};

// about page -------------------------------------------------------------------
inline const Icon kMonitor{24,
    "M5.5 4h13A2.5 2.5 0 0 1 21 6.5v8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 14.5"
    "v-8A2.5 2.5 0 0 1 5.5 4z M8 21h8 M12 17v4", nullptr, 1.8};
inline const Icon kHeadphones{24,
    "M4 14v-2a8 8 0 0 1 16 0v2"
    "M4.7 13.5h.1a2.2 2.2 0 0 1 2.2 2.2v2.6a2.2 2.2 0 0 1-2.2 2.2h-.1a2.2 2.2 0 0 1-2.2-2.2"
    "v-2.6a2.2 2.2 0 0 1 2.2-2.2z"
    "M19.2 13.5h.1a2.2 2.2 0 0 1 2.2 2.2v2.6a2.2 2.2 0 0 1-2.2 2.2h-.1a2.2 2.2 0 0 1-2.2-2.2"
    "v-2.6a2.2 2.2 0 0 1 2.2-2.2z", nullptr, 1.8};
inline const Icon kClock{24, "M12 3.5a8.5 8.5 0 1 0 8.5 8.5 M12 8v4.5l3 2", nullptr, 1.8};
inline const Icon kGlobe{24,
    "M21 12a9 9 0 1 1-18 0a9 9 0 1 1 18 0"
    "M16 12a4 9 0 1 1-8 0a4 9 0 1 1 8 0 M3.2 9h17.6 M3.2 15h17.6", nullptr, 1.7};
inline const Icon kUser{24,
    "M15.6 8a3.6 3.6 0 1 1-7.2 0a3.6 3.6 0 1 1 7.2 0 M5 20a7 7 0 0 1 14 0", nullptr, 1.8};
inline const Icon kLock{24,
    "M7.5 10.5h9a3 3 0 0 1 3 3v4a3 3 0 0 1-3 3h-9a3 3 0 0 1-3-3v-4a3 3 0 0 1 3-3z"
    "M8 10.5V7.5a4 4 0 0 1 8 0v3", nullptr, 1.9};
inline const Icon kHeart{24, nullptr,
    "M12 20.5s-7.6-4.6-9.3-9A5 5 0 0 1 12 6.6a5 5 0 0 1 9.3 4.9c-1.7 4.4-9.3 9-9.3 9z"};
inline const Icon kGithub{24, nullptr,
    "M12 2C6.5 2 2 6.6 2 12.2c0 4.5 2.9 8.3 6.8 9.7.5.1.7-.2.7-.5v-1.8c-2.8.6-3.4-1.4-3.4-1.4"
    "-.5-1.2-1.1-1.5-1.1-1.5-.9-.6.1-.6.1-.6 1 .1 1.5 1 1.5 1 .9 1.6 2.4 1.1 3 .9.1-.7.4-1.1.6-1.4"
    "-2.2-.3-4.6-1.1-4.6-5 0-1.1.4-2 1-2.8-.1-.3-.4-1.3.1-2.7 0 0 .8-.3 2.7 1a9.2 9.2 0 0 1 5 0"
    "c1.9-1.3 2.7-1 2.7-1 .5 1.4.2 2.4.1 2.7.6.8 1 1.7 1 2.8 0 3.9-2.3 4.7-4.6 5 .4.3.7.9.7 1.9"
    "v2.8c0 .3.2.6.7.5A10.2 10.2 0 0 0 22 12.2C22 6.6 17.5 2 12 2z"};
inline const Icon kSpark{28, nullptr, "M14 4l3 7 7 3-7 3-3 7-3-7-7-3 7-3z"};
inline const Icon kWide{28,
    "M9 10h10a4 4 0 0 1 4 4a4 4 0 0 1-4 4H9a4 4 0 0 1-4-4a4 4 0 0 1 4-4z M9 14h10",
    nullptr, 1.7};
inline const Icon kNote{24,
    "M9 18V5l11-2v13 M9 18a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0"
    "M20 16a2.5 2.5 0 1 1-5 0a2.5 2.5 0 1 1 5 0", nullptr, 2.0};

#undef CIRC

} // namespace ico
} // namespace eightd
