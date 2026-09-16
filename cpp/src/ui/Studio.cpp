// Studio: the whole instrument on one screen.
//
// The orbit and its presets on the left, every control on the right, and the
// player floating across the foot of the window -- it belongs to whatever is
// playing, not to the effect.  Nothing here opens another page.
#include "App.h"
#include "Layout.h"
#include <cstdio>
#include <functional>

namespace eightd {

namespace {

// The design's measurements, in the 1180x820 the page is laid out at.
constexpr double kStagePadL = 24, kStagePadR = 20, kStagePadT = 18;
constexpr double kRailPadL = 8, kRailPadR = 20, kColGap = 12;
constexpr double kCardPad = 14, kSechH = 24, kKnobBlock = 82, kTileH = 58;
constexpr double kOrbitSide = 460;
constexpr double kDockPad = 22, kDockH = 84, kDockBottom = 48;

std::string pct(double v)     { return fmt("%.0f%%", v * 100); }
std::string metres(double v)  { return fmt("%.2f m", v); }

std::string dbText(double v) {
    if (std::fabs(v) < 0.05) return "0";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%s%.0f", v > 0 ? "+" : "−", std::fabs(v));
    return buf;
}

const char* shortMode(Mode m) {
    switch (m) {
        case Mode::Circular: return "Circle";
        case Mode::PingPong: return "Ping-pong";
        case Mode::Pendulum: return "Pendulum";
        case Mode::Linear:   return "Linear";
        case Mode::Figure8:  return "Figure 8";
        case Mode::Spiral:   return "Spiral";
        case Mode::Random:   return "Random";
        default:             return "Static";
    }
}
const Icon& modeGlyph(Mode m) {
    switch (m) {
        case Mode::Circular: return ico::kCircular;
        case Mode::PingPong: return ico::kPingPong;
        case Mode::Pendulum: return ico::kPendulum;
        case Mode::Linear:   return ico::kLinear;
        case Mode::Figure8:  return ico::kFigure8;
        case Mode::Spiral:   return ico::kSpiral;
        case Mode::Random:   return ico::kRandom;
        default:             return ico::kStaticRing;
    }
}
const char* shortCharacter(Character c) {
    switch (c) {
        case Character::Slowed: return "Slowed";
        case Character::Radio:  return "Old radio";
        default:                return "Clean";
    }
}
const Icon& characterGlyph(Character c) {
    switch (c) {
        case Character::Slowed: return ico::kSlowed;
        case Character::Radio:  return ico::kRadio;
        default:                return ico::kClean;
    }
}

// Where the sound is, in words.
std::string bearing(double deg) {
    const char* side = deg > 8 ? "right" : (deg < -8 ? "left" : "centre");
    const bool front = std::fabs(deg) <= 90;
    if (std::fabs(deg) <= 8)   return front ? "front" : "back";
    if (std::fabs(deg) >= 172) return "back";
    return std::string(front ? "front-" : "back-") + side;
}

} // namespace

void App::drawStudio(const Rect& body) {
    const Rect stage{body.x, body.y, kStageW, body.h};
    const Rect rail{body.x + kStageW, body.y, body.w - kStageW, body.h};
    drawStage(stage);
    drawRail(rail);
    drawNowPlaying({body.x + kDockPad,
                    body.y + body.h - kDockBottom - kDockH,
                    body.w - kDockPad * 2, kDockH});
}

// --- the stage ---------------------------------------------------------------

void App::drawStage(const Rect& r) {
    const double x = r.x + kStagePadL;
    const double w = r.w - kStagePadL - kStagePadR;
    double y = r.y + kStagePadT;

    int presetCount = 0;
    const Preset* presetList = presets(presetCount);

    // title, and the preset it is sitting on
    ui_.font(27, W600, true);
    ui_.text(x, y + 17, "Studio", ui_.theme.text);
    ui_.font(12.5, W400);
    ui_.text(x + w, y + 18, preset_ >= 0 ? presetList[preset_].name : "Custom",
             ui_.theme.faint, Align::Right);
    y += 34;

    // the orbit: the one thing on this page that moves
    const double side = std::min(kOrbitSide, w);
    const Rect orbit{x + (w - side) * 0.5, y + 8, side, side};
    orbitRect_ = orbit;
    drawOrbitStatic(orbit);
    y = orbit.y + side;

    // Static mode is the one the tour tells people to drag.
    if (params_.mode == Mode::Static) {
        ui_.noteHit(orbit);
        if (ui_.over(orbit) && ui_.mousePressed) ui_.active = 299;
        if (ui_.active == 299 && ui_.mouseDown) {
            const double dx = ui_.mouseX - orbit.cx(), dy = ui_.mouseY - orbit.cy();
            const double scale = orbit.w * 0.417;      // 196/470: the 3 m rim
            params_.manualAngle = float(std::atan2(dx, -dy));
            params_.radius = float(std::clamp(std::sqrt(dx * dx + dy * dy)
                                              / scale * 3.0, 0.25, 3.0));
            preset_ = -1;
            pushParams();
        }
    }

    // readout on the left, meters on the right, both live
    readoutRect_ = {x, y + 10, w - 144, 20};
    metersRect_  = {x + w - 130, y + 10, 130, 20};
    drawReadout(readoutRect_);
    drawMeters(metersRect_);
    y += 30;

    // everything that animates sits inside this strip
    dynamic_ = {orbit.x - 6, orbit.y - 6, orbit.w + 12, (y + 4) - (orbit.y - 6)};
    dynamic_.x = std::min(dynamic_.x, readoutRect_.x - 4);
    dynamic_.w = std::max(dynamic_.w,
                          metersRect_.x + metersRect_.w + 4 - dynamic_.x);

    // presets
    y += 14;
    const Rect plus{x, y, 32, 32};
    if (ui_.click(kIdSavePreset, plus)) ui_.openMenu = kIdSavePreset;
    ui_.fillRound(plus, kPill, ui_.over(plus)
                  ? mix(ui_.theme.well, ui_.theme.text, 0.08) : ui_.theme.well);
    ui_.icon(ico::kPlus, {plus.x + 9.5, plus.y + 9.5, 13, 13}, ui_.theme.dim);

    double cx = plus.x + plus.w + 8;
    for (int i = 0; i < presetCount; ++i) {
        ui_.font(12.5, W500);
        const double tw = ui_.textWidth(presetList[i].name) + 30;
        if (cx + tw > x + w) break;                 // the rest live under +
        if (preset_ == i) ui_.glow(cx + tw * 0.5, y + 16, 46, ui_.theme.accent, 0.28);
        if (ui_.chip(kIdPreset + i, {cx, y, tw, 32}, presetList[i].name,
                     preset_ == i, ui_.theme.card))
            applyPreset(i);
        cx += tw + 8;
    }

    // The preset list, under +, so every preset is reachable.
    std::vector<std::string> names;
    for (int i = 0; i < presetCount; ++i) names.push_back(presetList[i].name);
    const int pick = ui_.menuPopup(kIdSavePreset, plus, names, preset_, 190);
    if (pick >= 0) applyPreset(pick);
}

void App::drawReadout(const Rect& r) {
    if (r.w <= 0) return;
    const double ang = engine_.processor().angle();
    const double deg = std::fmod(ang * 180.0 / M_PI + 540.0, 360.0) - 180.0;

    ui_.font(13, W700);
    const std::string a = fmt("%+.0f°", deg);
    ui_.text(r.x, r.cy(), a, ui_.theme.motion);
    const double w1 = ui_.textWidth(a);
    ui_.font(13, W400);
    const std::string d = metres(engine_.processor().distance());
    ui_.text(r.x + w1 + 12, r.cy(), d, ui_.theme.dim);
    const double w2 = ui_.textWidth(d);
    ui_.text(r.x + w1 + 12 + w2 + 12, r.cy(), bearing(deg), ui_.theme.faint);
}

void App::drawMeters(const Rect& r) {
    if (r.w <= 0) return;
    const double lvlL = engine_.processor().peakL();
    const double lvlR = engine_.processor().peakR();
    meterL_ = std::max(double(lvlL), meterL_ * 0.82);
    meterR_ = std::max(double(lvlR), meterR_ * 0.82);

    const char* names[2] = {"L", "R"};
    const double vals[2] = {meterL_, meterR_};
    for (int i = 0; i < 2; ++i) {
        const double ly = r.y + 5 + i * 9.0;
        ui_.font(9, W700);
        ui_.text(r.x, ly, names[i], ui_.theme.ghost);
        const Rect track{r.x + 12, ly - 2, r.w - 12, 4};
        ui_.fillRound(track, kPill, ui_.theme.well);
        const double t = std::clamp(vals[i], 0.0, 1.0);
        if (t > 0.002)
            ui_.fillRound({track.x, track.y, track.w * t, track.h}, kPill,
                          t < 0.9 ? ui_.theme.accent : ui_.theme.motion);
    }
}

// The floor, the rings and the listener: everything in the orbit that stays
// where it is.  Drawn in the 470-unit grid the design uses.
void App::drawOrbitStatic(const Rect& r) {
    cairo_t* cr = ui_.cr;
    const double k = r.w / 470.0;
    auto X = [&](double v) { return r.x + v * k; };
    auto Y = [&](double v) { return r.y + v * k; };
    const double cx = X(235), cy = Y(235);

    // the lit floor: brightest just above the middle, so it reads as a stage
    cairo_pattern_t* p = cairo_pattern_create_radial(cx, Y(179), 0, cx, cy, 292 * k);
    const Rgb in  = mix(ui_.theme.ground, ui_.theme.text, ui_.theme.dark ? 0.17 : 0.07);
    const Rgb mid = mix(ui_.theme.ground, ui_.theme.text, ui_.theme.dark ? 0.07 : 0.03);
    const Rgb out = mix(ui_.theme.ground, ui_.theme.text, ui_.theme.dark ? 0.04 : 0.02);
    cairo_pattern_add_color_stop_rgba(p, 0.0,  in.r,  in.g,  in.b,  1);
    cairo_pattern_add_color_stop_rgba(p, 0.75, mid.r, mid.g, mid.b, 1);
    cairo_pattern_add_color_stop_rgba(p, 1.0,  out.r, out.g, out.b, 1);
    cairo_set_source(cr, p);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, 196 * k, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(p);

    // the rim, then the light just inside it
    ui_.setColour(mix(ui_.theme.line, ui_.theme.text, 0.12));
    cairo_set_line_width(cr, 1);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, 196 * k, 0, 2 * M_PI);
    cairo_stroke(cr);
    ui_.setColour(ui_.theme.accent, 0.10);
    cairo_set_line_width(cr, 3);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, 192 * k, 0, 2 * M_PI);
    cairo_stroke(cr);

    ui_.setColour(mix(ui_.theme.line, ui_.theme.text, 0.10));
    cairo_set_line_width(cr, 1);
    for (double ring : {131.0, 65.0}) {
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, ring * k, 0, 2 * M_PI);
        cairo_stroke(cr);
    }
    ui_.setColour(ui_.theme.line, 0.7);
    cairo_new_path(cr);
    cairo_move_to(cr, cx, Y(39)); cairo_line_to(cr, cx, Y(431));
    cairo_move_to(cr, X(39), cy); cairo_line_to(cr, X(431), cy);
    cairo_stroke(cr);

    ui_.font(9, W400);
    ui_.text(X(241), Y(93), "2 m", ui_.theme.ghost);
    ui_.text(X(241), Y(158), "1 m", ui_.theme.ghost);

    // the listener, sitting in a dip so it reads against the lit floor
    const Rgb head = mix(ui_.theme.raised, ui_.theme.text, ui_.theme.dark ? 0.16 : 0.0);
    ui_.circle(cx, cy, 30 * k, Rgb{0, 0, 0}, 0.22);
    ui_.circle(X(216), Y(235), 5 * k, head);
    ui_.circle(X(254), Y(235), 5 * k, head);
    ui_.setColour(head);
    cairo_new_path(cr);
    SvgPath::add(cr, "M235 209c1.6 0 7 8 7 9s-14 1-14 0 5.4-9 7-9z", r.x, r.y, r.w, 470);
    cairo_fill(cr);
    ui_.circle(cx, cy, 18 * k, head);

    // the compass, tucked between the rim and the edge of the box
    ui_.font(9.5, W700);
    ui_.tracked(cx, r.y + 20, "FRONT", ui_.theme.ghost, 2.0, Align::Centre);
    ui_.tracked(cx, r.y + r.h - 20, "BACK", ui_.theme.ghost, 2.0, Align::Centre);
    ui_.tracked(r.x + 14, cy, "L", ui_.theme.ghost, 2.0);
    ui_.tracked(r.x + r.w - 14, cy, "R", ui_.theme.ghost, 2.0, Align::Right);
}

// Everything below moves, so it is never baked into the cached chrome: the
// cache would otherwise keep a stale dot and the new frame would draw over it.
void App::drawOrbitLive(const Rect& r) {
    if (r.w <= 0) return;
    cairo_t* cr = ui_.cr;
    const double k = r.w / 470.0;
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
    const double scale = r.w * 0.417;                  // 196/470: the 3 m rim
    // The dot is the effect, not the capture: it goes grey only when the 8D
    // switch is off.  How brightly it burns says whether audio is flowing.
    const bool on = params_.enabled;
    const bool running = engine_.running() && params_.enabled;

    const double angle = engine_.processor().angle();
    const double dist  = std::clamp(double(engine_.processor().distance()), 0.2, 3.0);
    const double rad   = dist / 3.0 * scale;
    const double sx = cx + std::sin(angle) * rad;
    const double sy = cy - std::cos(angle) * rad;

    // the path it is travelling
    const double dash[2] = {1.5 * k, 10.5 * k};
    ui_.setColour(ui_.theme.accent, 0.55);
    cairo_set_line_width(cr, 1.5);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_set_dash(cr, dash, 2, 0);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // the trail: a stroke that thins and fades behind the source
    trail_.push_back({sx, sy});
    while (trail_.size() > 46) trail_.pop_front();
    const size_t n = trail_.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const double f = double(i + 1) / double(n);
        ui_.setColour(on ? ui_.theme.motion : ui_.theme.ghost, f * f * 0.95);
        cairo_set_line_width(cr, 1.0 + f * 4.5);
        cairo_new_path(cr);
        cairo_move_to(cr, trail_[i].first, trail_[i].second);
        cairo_line_to(cr, trail_[i + 1].first, trail_[i + 1].second);
        cairo_stroke(cr);
    }
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

    // the source, and its light
    ui_.glow(sx, sy, 34 * k, ui_.theme.motion, running ? 0.55 : (on ? 0.34 : 0.14));
    ui_.circle(sx, sy, 9.5 * k, on ? ui_.theme.motion : ui_.theme.ghost);
    ui_.setColour(Rgb::hex(0xFFD1E3), on ? 0.55 : 0.2);
    cairo_set_line_width(cr, 1.6);
    cairo_new_path(cr);
    cairo_arc(cr, sx, sy, 9.5 * k, 0, 2 * M_PI);
    cairo_stroke(cr);
}

// --- the rail ------------------------------------------------------------------

void App::drawRail(const Rect& r) {
    const double x0 = r.x + kRailPadL;
    const double total = r.w - kRailPadL - kRailPadR;
    const double colW = (total - kColGap) * 0.5;
    const double innerW = colW - kCardPad * 2;
    const double top = r.y + kStagePadT;

    auto card = [&](double x, double y, double h) {
        ui_.fillRound({x, y, colW, h}, kCardR, ui_.theme.card);
    };
    auto sech = [&](double x, double y, const char* kick, const char* note) {
        ui_.font(10.5, W700);
        ui_.tracked(x + kCardPad, y + kCardPad + 12, kick, ui_.theme.accent, 2.0);
        if (note && *note) {
            ui_.font(11.5, W400);
            ui_.text(x + colW - kCardPad, y + kCardPad + 12, note,
                     ui_.theme.faint, Align::Right);
        }
    };
    // Four knobs across the width of a card.
    auto knobRow = [&](double x, double y, int n,
                       const std::function<void(int, const Rect&)>& each) {
        const double cell = innerW / 4.0;
        for (int i = 0; i < n; ++i)
            each(i, {x + kCardPad + cell * i, y, cell, kKnobBlock});
    };

    // ---- left column ----------------------------------------------------
    double x = x0, y = top;

    // MOVEMENT
    const double moveH = kCardPad + kSechH + 10 + kTileH * 2 + 7 + 12 + kKnobBlock + 16;
    card(x, y, moveH);
    sech(x, y, "MOVEMENT", nullptr);
    {
        const Rect seg{x + colW - kCardPad - 96, y + kCardPad, 96, 30};
        const int pick = ui_.segPill(kIdDir, seg, {"CW", "CCW"},
                                     params_.direction >= 0 ? 0 : 1);
        if (pick >= 0) { params_.direction = pick == 0 ? 1 : -1; pushParams(); }
    }
    double gy = y + kCardPad + kSechH + 10;
    const double tileW = (innerW - 7 * 3) / 4.0;
    for (int i = 0; i < 8; ++i) {
        const Rect cell{x + kCardPad + (tileW + 7) * (i % 4),
                        gy + (kTileH + 7) * (i / 4), tileW, kTileH};
        const Mode m = Mode(i);
        if (ui_.tile(kIdMode + i, cell, shortMode(m), modeGlyph(m),
                     params_.mode == m, m == Mode::Static)) {
            params_.mode = m; preset_ = -1; pushParams();
        }
        if (m == Mode::Static) {   // the dot that marks the parked position
            const Rgb c = params_.mode == m ? ui_.theme.deep : ui_.theme.faint;
            ui_.icon(ico::kStaticPos, {cell.cx() - 11, cell.cy() - 15, 22, 22}, c);
        }
    }
    gy += kTileH * 2 + 7 + 12;
    {
        double speed = params_.speed, radius = params_.radius;
        double depth = params_.depth, smooth = params_.smoothness;
        knobRow(x, gy, 4, [&](int i, const Rect& b) {
            bool changed = false;
            if (i == 0) changed = ui_.knob(kIdSpeed, b, "Speed", fmt("%.2f", speed),
                                           speed, 0.01, 1.2,
                                           params_.mode != Mode::Static, false, 0.12, true);
            if (i == 1) changed = ui_.knob(kIdDistance, b, "Distance", metres(radius),
                                           radius, 0.25, 3.0, true, false, 1.0, true);
            if (i == 2) changed = ui_.knob(kIdIntensity, b, "Intensity", pct(depth),
                                           depth, 0.0, 1.0, true, false, 0.85, true);
            if (i == 3) changed = ui_.knob(kIdSmooth, b, "Smooth", pct(smooth),
                                           smooth, 0.0, 1.0, true, false, 0.35, true);
            if (changed) {
                params_.speed = float(speed); params_.radius = float(radius);
                params_.depth = float(depth); params_.smoothness = float(smooth);
                preset_ = -1; pushParams();
            }
        });
    }
    y += moveH + kColGap;

    // SPACE
    const double spaceH = kCardPad + kSechH + 12 + kKnobBlock + 16;
    card(x, y, spaceH);
    sech(x, y, "SPACE", "Reverb & stereo");
    {
        double mix_ = params_.reverbMix, size = params_.reverbSize;
        double damp = params_.reverbDamp, width = params_.width;
        knobRow(x, y + kCardPad + kSechH + 12, 4, [&](int i, const Rect& b) {
            bool changed = false;
            if (i == 0) changed = ui_.knob(kIdReverb, b, "Reverb", pct(mix_),
                                           mix_, 0.0, 1.0, true, false, 0.18, true);
            if (i == 1) changed = ui_.knob(kIdRoom, b, "Room", pct(size),
                                           size, 0.0, 1.0, true, false, 0.6, true);
            if (i == 2) changed = ui_.knob(kIdDamping, b, "Damping", pct(damp),
                                           damp, 0.0, 1.0, true, false, 0.45, true);
            if (i == 3) changed = ui_.knob(kIdWidth, b, "Width", pct(width),
                                           width, 0.0, 2.0, true, false, 1.0, true);
            if (changed) {
                params_.reverbMix = float(mix_); params_.reverbSize = float(size);
                params_.reverbDamp = float(damp); params_.width = float(width);
                preset_ = -1; pushParams();
            }
        });
    }
    y += spaceH + kColGap;

    // ECHO
    card(x, y, spaceH);
    {
        const bool on = params_.delayMix > 0.001;
        sech(x, y, "ECHO", on ? "Repeats follow the orbit" : "Off · raise Delay");
        double mix_ = params_.delayMix, time = params_.delayTime, fb = params_.delayFeedback;
        knobRow(x, y + kCardPad + kSechH + 12, 3, [&](int i, const Rect& b) {
            bool changed = false;
            if (i == 0) changed = ui_.knob(kIdDelay, b, "Delay", pct(mix_),
                                           mix_, 0.0, 1.0, true, false, 0.0, true);
            if (i == 1) changed = ui_.knob(kIdDelayTime, b, "Time ms",
                                           fmt("%.0f", time * 1000), time, 0.04, 1.2,
                                           on, false, 0.28, true, 58, 0.005);
            if (i == 2) changed = ui_.knob(kIdFeedback, b, "Feedback", pct(fb),
                                           fb, 0.0, 0.85, on, false, 0.35, true);
            if (changed) {
                params_.delayMix = float(mix_); params_.delayTime = float(time);
                params_.delayFeedback = float(fb);
                preset_ = -1; pushParams();
            }
        });
    }

    // ---- right column ---------------------------------------------------
    x = x0 + colW + kColGap; y = top;

    // CHARACTER -- its amount reads across, not around: one long throw is
    // easier to place than a dial, and there is width here to give it.
    const double charH = kCardPad + kSechH + 10 + 62 + 16 + 18 + 12 + 6 + 16;
    card(x, y, charH);
    sech(x, y, "CHARACTER", "Colour on the source");
    {
        const double cw = (innerW - 14) / 3.0;
        for (int i = 0; i < 3; ++i) {
            const Character c = Character(i);
            const Rect cell{x + kCardPad + (cw + 7) * i, y + kCardPad + kSechH + 10,
                            cw, 62};
            if (ui_.tile(kIdCharacter + i, cell, shortCharacter(c),
                         characterGlyph(c), params_.character == c)) {
                params_.character = c; preset_ = -1; pushParams();
            }
        }
        const bool on = params_.character != Character::Clean;
        double amount = params_.characterAmount;
        const double ly = y + kCardPad + kSechH + 10 + 62 + 16;
        ui_.font(13, W400);
        ui_.text(x + kCardPad, ly + 8, "Amount",
                 on ? ui_.theme.dim : ui_.theme.ghost);
        ui_.font(13, W700);
        ui_.text(x + colW - kCardPad, ly + 8, pct(amount),
                 on ? ui_.theme.accent : ui_.theme.ghost, Align::Right);
        const Rect track{x + kCardPad, ly + 18 + 12, innerW, 6};
        if (ui_.slider(kIdAmount, track, amount, 0.0, 1.0, on)) {
            params_.characterAmount = float(amount);
            preset_ = -1; pushParams();
        }
    }
    y += charH + kColGap;

    // EQUALISER
    card(x, y, spaceH);
    sech(x, y, "EQUALISER", "±12 dB");
    {
        double bass = params_.eqBass, mid = params_.eqMid, treble = params_.eqTreble;
        double gain = params_.outputGain;
        knobRow(x, y + kCardPad + kSechH + 12, 4, [&](int i, const Rect& b) {
            bool changed = false;
            // A tone control is read in whole decibels, so that is its step.
            if (i == 0) changed = ui_.knob(kIdBass, b, "Bass", dbText(bass),
                                           bass, -12, 12, true, true, 0, true, 58, 1);
            if (i == 1) changed = ui_.knob(kIdMid, b, "Mid", dbText(mid),
                                           mid, -12, 12, true, true, 0, true, 58, 1);
            if (i == 2) changed = ui_.knob(kIdTreble, b, "Treble", dbText(treble),
                                           treble, -12, 12, true, true, 0, true, 58, 1);
            if (i == 3) changed = ui_.knob(kIdOutput, b, "Output", pct(gain),
                                           gain, 0.0, 1.5, true, false, 0.9, true);
            if (changed) {
                params_.eqBass = float(bass); params_.eqMid = float(mid);
                params_.eqTreble = float(treble); params_.outputGain = float(gain);
                preset_ = -1; pushParams();
            }
        });
    }
    y += spaceH + kColGap;

    // ENGINE
    const double rowH = 26, rowGap = 12;
    const double engineH = kCardPad + kSechH + 12 + rowH * 4 + rowGap * 3 + 16;
    card(x, y, engineH);
    sech(x, y, "ENGINE", "48 kHz · C++ DSP");
    {
        double ry = y + kCardPad + kSechH + 12;
        auto label = [&](const char* s, const Rgb& c) {
            ui_.font(13, W400);
            ui_.text(x + kCardPad, ry + rowH * 0.5, s, c);
        };
        const bool running = engine_.running();

        label("Capture system audio", ui_.theme.text);
        {
            const Rect b{x + colW - kCardPad - 86, ry, 86, rowH};
            if (ui_.pill(kIdCapture, b, running ? "Stop" : "Start",
                         running ? ui_.theme.motion : ui_.theme.accent,
                         running ? ui_.theme.onMotion : ui_.theme.onAccent,
                         12.5, W700, !sinks_.empty()))
                startStop();
        }
        ry += rowH + rowGap;

        label("Quality", ui_.theme.text);
        {
            // The list reads from safest to lowest latency; the engine's own
            // order is the other way round.
            const int shown = latency_ == 2 ? 0 : (latency_ == 1 ? 1 : 2);
            const int pick = ui_.segPill(kIdQuality,
                                         {x + colW - kCardPad - 172, ry - 2, 172, 30},
                                         {"Safe", "Balanced", "Max"}, shown);
            if (pick >= 0) {
                latency_ = pick == 0 ? 2 : (pick == 1 ? 1 : 0);
                staticDirty_ = true;
                if (engine_.running())
                    setStatus("Quality applies the next time capture starts.",
                              ui_.theme.faint);
            }
        }
        ry += rowH + rowGap;

        label("Pause orbit when silent", ui_.theme.text);
        {
            const Rect b{x + colW - kCardPad - 44, ry, 44, 26};
            if (ui_.switchPill(kIdPause, b, params_.pauseWhenSilent, false)) {
                params_.pauseWhenSilent = !params_.pauseWhenSilent;
                pushParams();
            }
        }
        ry += rowH + rowGap;

        label("Reset studio to defaults", ui_.theme.dim);
        {
            const Rect b{x + colW - kCardPad - 70, ry - 2, 70, 30};
            if (ui_.click(kIdReset, b)) resetSettings();
            ui_.fillRound(b, kPill, ui_.over(b)
                          ? mix(ui_.theme.well, ui_.theme.text, 0.08) : ui_.theme.well);
            ui_.font(12.5, W700);
            ui_.text(b.cx(), b.cy(), "Reset", ui_.theme.accent, Align::Centre);
        }
    }
}

// --- the dock ------------------------------------------------------------------

// The transport floats: it belongs to the player, not to the effect.
double App::drawNowPlaying(const Rect& dock) {
    const bool have = nowPlaying_.has();
    const Track t = nowPlaying_.track();

    ui_.glow(dock.cx(), dock.cy() + 8, dock.w * 0.4, ui_.theme.accent,
             ui_.theme.dark ? 0.05 : 0.03);
    ui_.fillRound(dock, kPill, ui_.theme.card);
    ui_.strokeRound(dock, kPill, ui_.theme.text, 1, 0.05);

    // the cover: a lettered gradient, as in the design
    const Rect cover{dock.x + 16, dock.cy() - 26, 52, 52};
    ui_.fillRound(cover, 15, have ? Rgb::hex(0x7A1E3C) : ui_.theme.well);
    if (have) {
        struct Wash { double x, y, r, g, b; };
        const Wash washes[2] = {{0.30, 0.25, 1.0, 0.70, 0.42},
                                {0.80, 0.85, 1.0, 0.27, 0.56}};
        for (const auto& wsh : washes) {
            cairo_pattern_t* p = cairo_pattern_create_radial(
                cover.x + cover.w * wsh.x, cover.y + cover.h * wsh.y, 0,
                cover.x + cover.w * wsh.x, cover.y + cover.h * wsh.y, cover.w * 0.62);
            cairo_pattern_add_color_stop_rgba(p, 0, wsh.r, wsh.g, wsh.b, 1.0);
            cairo_pattern_add_color_stop_rgba(p, 1, wsh.r, wsh.g, wsh.b, 0.0);
            ui_.roundRect(cover, 15);
            cairo_set_source(ui_.cr, p);
            cairo_fill(ui_.cr);
            cairo_pattern_destroy(p);
        }
    }
    ui_.font(17, W700);
    ui_.text(cover.cx(), cover.cy(), have ? t.initials() : "—",
             have ? Rgb::hex(0xFFFFFF) : ui_.theme.ghost, Align::Centre);

    // transport, at the far end
    const double bs = 40, small = 34;
    const double tw = small + 8 + bs + 8 + small;
    const Rect prev{dock.x + dock.w - 18 - tw, dock.cy() - small * 0.5, small, small};
    const Rect play{prev.x + small + 8, dock.cy() - bs * 0.5, bs, bs};
    const Rect next{play.x + bs + 8, prev.y, small, small};
    const bool live = have && !t.bus.empty();

    auto round = [&](const Rect& b, const Icon& glyph, const Rgb& bg, const Rgb& fg,
                     double gs, int id, bool enabled) {
        const bool clicked = ui_.click(id, b, enabled);
        ui_.fillRound(b, kPill, enabled && ui_.over(b)
                      ? mix(bg, ui_.theme.text, 0.10) : bg);
        ui_.icon(glyph, {b.cx() - gs * 0.5, b.cy() - gs * 0.5, gs, gs},
                 enabled ? fg : ui_.theme.ghost);
        return clicked;
    };
    if (round(prev, ico::kPrev, ui_.theme.well, ui_.theme.text, 13,
              kIdPrev, live && t.canPrev)) nowPlaying_.previous();
    if (round(play, t.playing ? ico::kPause : ico::kPlay, ui_.theme.text,
              ui_.theme.ground, 13, kIdPlayPause, live)) nowPlaying_.playPause();
    if (round(next, ico::kNext, ui_.theme.well, ui_.theme.text, 13,
              kIdNext, live && t.canNext)) nowPlaying_.next();

    // title and artist on one line, the source tagged at the end of it
    const double tx = cover.x + cover.w + 16;
    const double tWidth = prev.x - 16 - tx;
    const std::string tag = have && !t.player.empty() ? t.player + " · MPRIS"
                                                      : "MPRIS";
    ui_.font(10.5, W700);
    const double tagW = ui_.textWidth(tag) + 14;
    ui_.tracked(tx + tWidth, dock.cy() - 14, tag, ui_.theme.ghost, 1.6, Align::Right);

    ui_.font(15, W700);
    const std::string title = have ? t.title : "Nothing playing";
    const double titleW = std::min(ui_.textWidth(title),
                                   std::max(60.0, tWidth - tagW - 140));
    ui_.text(tx, dock.cy() - 14, fitText(ui_, title, titleW),
             have ? ui_.theme.text : ui_.theme.dim);
    ui_.font(12.5, W400);
    ui_.text(tx + titleW + 10, dock.cy() - 14,
             fitText(ui_, have ? t.artistLine() : "Start a player and it appears here",
                     std::max(0.0, tWidth - titleW - 10 - tagW)),
             ui_.theme.dim);

    // the progress line under them
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double pos = have ? t.at(now) : 0.0;
    const double len = have ? t.length : 0.0;
    ui_.font(11, W400);
    const std::string left = have ? clockText(pos) : "--:--";
    const std::string right = (have && len > 0) ? clockText(len) : "--:--";
    const double lw = ui_.textWidth(left) + 9, rw = ui_.textWidth(right) + 9;
    const double by = dock.cy() + 13;
    ui_.text(tx, by, left, ui_.theme.faint);
    ui_.text(tx + tWidth, by, right, ui_.theme.faint, Align::Right);
    const Rect bar{tx + lw, by - 2, tWidth - lw - rw, 4};
    if (bar.w > 10) {
        ui_.fillRound(bar, kPill, ui_.theme.well);
        if (len > 0) {
            const double frac = std::clamp(pos / len, 0.0, 1.0);
            if (frac > 0.001)
                ui_.fillRound({bar.x, bar.y, bar.w * frac, bar.h}, kPill,
                              ui_.theme.motion);
            ui_.glow(bar.x + bar.w * frac, by, 16, ui_.theme.motion, 0.5);
            ui_.circle(bar.x + bar.w * frac, by, 5, ui_.theme.motion);
        }
    }

    // The line along the bottom: what just happened, or what the keyboard can
    // do when nothing has.
    const double ky = dock.y + dock.h + 20;
    const bool fresh = !status_.empty() &&
        std::chrono::steady_clock::now() - statusAt_ < std::chrono::seconds(10);
    if (fresh) {
        ui_.font(11.5, W500);
        ui_.text(dock.cx(), ky, fitText(ui_, status_, dock.w - 80), statusColour_,
                 Align::Centre);
    } else {
        struct Key { const char* key; const char* what; };
        const Key keys[] = {{"Space", "bypass"}, {"Ctrl+R", "start / stop"},
                            {"Ctrl+T", "theme"}, {"", "drag the bar to move the window"}};
        const int count = int(sizeof(keys) / sizeof(keys[0]));
        double total = 0;
        for (const auto& kk : keys) {
            ui_.font(11, W600); total += ui_.textWidth(kk.key);
            ui_.font(11, W400); total += (*kk.key ? 6 : 0) + ui_.textWidth(kk.what) + 18;
        }
        total -= 18;
        double kx = dock.cx() - total * 0.5;
        for (int i = 0; i < count; ++i) {
            if (*keys[i].key) {
                ui_.font(11, W600);
                ui_.text(kx, ky, keys[i].key, ui_.theme.ghost);
                kx += ui_.textWidth(keys[i].key) + 6;
            }
            ui_.font(11, W400);
            ui_.text(kx, ky, keys[i].what, ui_.theme.faint);
            kx += ui_.textWidth(keys[i].what);
            if (i + 1 < count) {
                ui_.text(kx + 7, ky, "·", ui_.theme.ghost);
                kx += 18;
            }
        }
    }
    return dock.y + dock.h;
}

} // namespace eightd
