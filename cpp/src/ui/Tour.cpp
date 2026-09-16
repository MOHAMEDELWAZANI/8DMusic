// The two presentations, and the written guides behind About.
//
// Both presentations are the same shape: a picture on the left, a few words on
// the right, progress above and the buttons below.  The welcome takes the whole
// window; the studio tour is a dialog over the page it is talking about.
#include "App.h"
#include "Layout.h"
#include <vector>

namespace eightd {

namespace {

// Brand marks, drawn as the design draws them.
void youtube(Ui& ui, const Rect& b) {
    ui.fillRound(b, b.h * 0.27, Rgb::hex(0xFF0000));
    ui.setColour(Rgb::hex(0xFFFFFF));
    cairo_new_path(ui.cr);
    SvgPath::add(ui.cr, "M258 130 L420 221 L258 313 Z", b.x, b.y + (b.h - b.w * 443.0 / 640.0) * 0.5,
                 b.w, 640);
    cairo_fill(ui.cr);
}
void spotify(Ui& ui, const Rect& b) {
    ui.circle(b.cx(), b.cy(), b.w * 0.5, Rgb::hex(0x1DB954));
    ui.setColour(Rgb{0, 0, 0});
    cairo_set_line_cap(ui.cr, CAIRO_LINE_CAP_ROUND);
    // The three bars occupy the middle of the circle, as the mark does.
    const Rect m{b.cx() - b.w * 0.31, b.cy() - b.h * 0.31, b.w * 0.62, b.h * 0.62};
    const double k = m.w / 36.0;
    const char* arcs[3] = {
        "M9 14c6-2.4 13-1.6 18 1.4",
        "M10.5 19.4c5-2 10.6-1.3 14.8 1.2",
        "M12 24.4c4-1.6 8.4-1 11.6 1",
    };
    const double widths[3] = {3.4, 3.0, 2.6};
    for (int i = 0; i < 3; ++i) {
        cairo_set_line_width(ui.cr, widths[i] * k);
        cairo_new_path(ui.cr);
        SvgPath::add(ui.cr, arcs[i], m.x, m.y - m.h * 0.04, m.w, 36);
        cairo_stroke(ui.cr);
    }
    cairo_set_line_cap(ui.cr, CAIRO_LINE_CAP_BUTT);
}

} // namespace

// --- the welcome ----------------------------------------------------------

void App::drawWelcome(const Rect& full) {
    const int page = welcome_;
    const double k = std::min(full.w / kDesignW, full.h / kDesignH);

    // the light behind the page
    ui_.glow(full.w * 0.24, full.h * 0.50, full.w * 0.46, ui_.theme.accent,
             ui_.theme.dark ? 0.14 : 0.08);
    ui_.glow(full.w * 0.44, full.h * 0.30, full.w * 0.36, ui_.theme.motion,
             ui_.theme.dark ? 0.14 : 0.07);

    const Rect left{full.x, full.y, full.w * 0.475, full.h};
    const double rx = left.x + left.w + 10 * k;
    const double rw = full.w - rx - 64 * k;

    auto advance = [&](int to) {
        if (to > 4) { welcome_ = -1; seenWelcome_ = true; if (!seenTour_) tour_ = 0; }
        else welcome_ = to;
        staticDirty_ = true;
    };

    // the picture
    const double side = std::min(left.w, left.h) * 0.86;
    const Rect art{left.cx() - side * 0.5, left.cy() - side * 0.5, side, side};
    const double a = side / 420.0;
    switch (page) {
    case 0:
        // Painted live, in drawWelcomeMark: this one loops.
        orbitRect_ = art;
        dynamic_ = {art.x - 8, art.y - 8, art.w + 16, art.h + 16};
        break;
    case 1: {
        ui_.setColour(ui_.theme.line, 0.5);
        cairo_set_line_width(ui_.cr, 1);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, art.cx(), art.cy(), 200 * a, 0, 2 * M_PI);
        cairo_stroke(ui_.cr);
        ui_.setColour(ui_.theme.accent, 0.45);
        cairo_set_line_width(ui_.cr, 1.6);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        const double dash[2] = {1, 8};
        cairo_set_dash(ui_.cr, dash, 2, 0);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, art.cx(), art.cy(), 150 * a, 0, 2 * M_PI);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);

        // the listener, in the middle
        ui_.circle(art.cx(), art.cy(), 60 * a, ui_.theme.raised);
        ui_.circle(art.cx(), art.cy(), 58 * a, ui_.theme.card);
        {
            const double s = 58 * a;
            const Rect hb{art.cx() - s * 0.5, art.cy() - s * 0.5, s, s};
            ui_.icon(ico::kHeadphones, hb, ui_.theme.text);
            ui_.fillRound({hb.x + s * 2.5 / 24, hb.y + s * 14 / 24, s * 5 / 24, s * 7 / 24},
                          s * 2 / 24, ui_.theme.accent);
            ui_.fillRound({hb.x + s * 16.5 / 24, hb.y + s * 14 / 24, s * 5 / 24, s * 7 / 24},
                          s * 2 / 24, ui_.theme.motion);
        }
        // the apps around it
        const double r = 33 * a;
        const Rect yt{art.x + 44 * a, art.y + 96 * a, r * 2, r * 2};
        const Rect sp{art.x + 306 * a, art.y + 186 * a, r * 2, r * 2};
        const Rect nt{art.x + 150 * a, art.y + 312 * a, r * 2, r * 2};
        ui_.circle(yt.cx(), yt.cy(), r, ui_.theme.well);
        youtube(ui_, {yt.cx() - r * 0.52, yt.cy() - r * 0.36, r * 1.04, r * 0.72});
        spotify(ui_, sp);
        ui_.circle(nt.cx(), nt.cy(), r, ui_.theme.well);
        ui_.icon(ico::kNote, {nt.cx() - r * 0.45, nt.cy() - r * 0.45, r * 0.9, r * 0.9},
                 ui_.theme.accent);
        break;
    }
    case 2: {
        const double b = side / 420.0;
        const Rect box{art.x, art.cy() - 170 * b, 420 * b, 340 * b};
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        struct Wave { double cx, r, a0, a1; Rgb c; double alpha; };
        const Wave waves[4] = {
            {84, 59, 0.5 * M_PI, 1.5 * M_PI, ui_.theme.accent, 0.35},
            {54, 85, 0.5 * M_PI, 1.5 * M_PI, ui_.theme.accent, 0.18},
            {336, 59, -0.5 * M_PI, 0.5 * M_PI, ui_.theme.motion, 0.35},
            {366, 85, -0.5 * M_PI, 0.5 * M_PI, ui_.theme.motion, 0.18},
        };
        for (const auto& wv : waves) {
            ui_.setColour(wv.c, wv.alpha);
            cairo_set_line_width(ui_.cr, 5 * b);
            cairo_new_path(ui_.cr);
            cairo_arc(ui_.cr, box.x + wv.cx * b, box.y + 189 * b, wv.r * b, wv.a0, wv.a1);
            cairo_stroke(ui_.cr);
        }
        ui_.setColour(ui_.theme.raised);
        cairo_set_line_width(ui_.cr, 18 * b);
        cairo_new_path(ui_.cr);
        SvgPath::add(ui_.cr, "M126 196v-32a84 84 0 0 1 168 0v32", box.x, box.y, box.w, 420);
        cairo_stroke(ui_.cr);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);

        const Rect pl{box.x + 100 * b, box.y + 172 * b, 54 * b, 96 * b};
        const Rect pr{box.x + 266 * b, box.y + 172 * b, 54 * b, 96 * b};
        ui_.fillRound(pl, 24 * b, ui_.theme.accent);
        ui_.fillRound(pr, 24 * b, ui_.theme.motion);
        ui_.font(22 * b, W800);
        ui_.text(pl.cx(), pl.cy(), "L", ui_.theme.onAccent, Align::Centre);
        ui_.text(pr.cx(), pr.cy(), "R", ui_.theme.dark ? Rgb::hex(0x3D0A1F)
                                                       : ui_.theme.onMotion, Align::Centre);
        break;
    }
    case 3: {
        ui_.setColour(ui_.theme.accent, 0.4);
        cairo_set_line_width(ui_.cr, 1.6);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        const double dash[2] = {1, 8};
        cairo_set_dash(ui_.cr, dash, 2, 0);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, art.cx(), art.cy(), 190 * a, 0, 2 * M_PI);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        ui_.setColour(ui_.theme.motion, 0.35);
        cairo_set_line_width(ui_.cr, 5 * a);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, art.cx(), art.cy(), 190 * a, -M_PI * 0.5, -M_PI * 0.22);
        cairo_stroke(ui_.cr);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);
        ui_.circle(art.cx() + std::cos(-M_PI * 0.22) * 190 * a,
                   art.cy() + std::sin(-M_PI * 0.22) * 190 * a, 11 * a, ui_.theme.motion);

        const Rect card{art.cx() - 115 * a, art.cy() - 85 * a, 230 * a, 170 * a};
        ui_.shadow(card, 22 * a, 16, 0.5, 10);
        ui_.fillRound(card, 22 * a, ui_.theme.card);
        ui_.strokeRound(card, 22 * a, ui_.theme.line, 2);
        ui_.glow(card.cx(), card.cy(), 60 * a, ui_.theme.accent, 0.25);
        ui_.circle(card.cx(), card.cy(), 43 * a, ui_.theme.tint);
        ui_.strokeRound({card.cx() - 43 * a, card.cy() - 43 * a, 86 * a, 86 * a}, 43 * a,
                        ui_.theme.accent, 1.5, 0.7);
        ui_.icon(ico::kLock, {card.cx() - 20 * a, card.cy() - 20 * a, 40 * a, 40 * a},
                 ui_.theme.deep);
        break;
    }
    default: {
        const double cw = std::min(400 * a, left.w - 40);
        const Rect card{left.cx() - cw * 0.5, left.cy() - 118 * a, cw, 236 * a};
        ui_.fillRound(card, 26 * a, ui_.theme.card);
        ui_.font(15, W700);
        ui_.text(card.x + 26 * a, card.y + 34 * a, "Your dashboard", ui_.theme.text);
        ui_.font(11, W800);
        const Rect beta{card.x + card.w - 26 * a - 54, card.y + 22 * a, 54, 24};
        ui_.fillRound(beta, kPill, mix(ui_.theme.card, ui_.theme.motion, 0.16));
        ui_.tracked(beta.cx(), beta.cy(), "BETA",
                    mix(ui_.theme.motion, ui_.theme.text, 0.35), 1.0, Align::Centre);

        const double bx = card.x + 26 * a, bw = card.w - 52 * a;
        const double slot = bw / 7.0, barW = slot * 0.65;
        const double heights[7] = {58, 76, 46, 94, 68, 114, 82};
        for (int i = 0; i < 7; ++i) {
            const double h = heights[i] / 114.0 * 114 * a;
            const Rect b{bx + slot * i, card.y + 152 * a - h, barW, h};
            ui_.fillRound(b, 10 * a, i == 5 ? ui_.theme.accent : ui_.theme.raised);
        }
        const char* tags[3] = {"Early builds", "Feedback", "Presets in sync"};
        double tx = bx;
        for (const char* t : tags) {
            ui_.font(12.5, W600);
            const Rect tag{tx, card.y + 172 * a, ui_.textWidth(t) + 24, 28};
            ui_.fillRound(tag, kPill, ui_.theme.well);
            ui_.text(tag.cx(), tag.cy(), t, ui_.theme.dim, Align::Centre);
            tx += tag.w + 8;
        }
        break;
    }
    }

    // the words
    struct Copy { const char* kicker; const char* line1; const char* line2; const char* body; };
    static const Copy kCopy[5] = {
        {"WELCOME TO 8D MUSIC", "Sound that moves", "around you",
         "8D Music makes anything this computer plays travel around your head, "
         "live, while it plays."},
        {"EVERYTHING THIS COMPUTER PLAYS", "One effect,", "every app",
         "Spotify, a browser tab, a game, your own files — 8D Music sits on the "
         "output, so whatever plays goes through Studio on its way to your headphones."},
        {"BEFORE YOU START", "Put your", "headphones on",
         "8D gives each ear its own version of the sound. Speakers mix the two back "
         "together, so the effect disappears."},
        {"PRIVATE BY DESIGN", "Nothing leaves", "this computer",
         "The effect runs locally in the same C++ engine as the Windows and Android "
         "builds. No uploading, no account needed."},
        {"OPTIONAL", "Help build", "8D Music",
         "Sign in to try early builds, send feedback and keep your presets across the "
         "desktop and the phone. The app works fully without an account."},
    };
    const Copy& c = kCopy[page];

    double y = full.cy() - 150 * k;
    if (page == 4) y = full.cy() - 220 * k;

    for (int i = 0; i < 5; ++i) {
        const Rect b{rx + (46 * k + 6) * i, y, 46 * k, 4};
        ui_.fillRound(b, kPill, ui_.theme.text, i <= page ? 1.0 : 0.14);
    }
    y += 4 + 26 * k;
    ui_.font(12, W700);
    ui_.tracked(rx, y + 7, c.kicker, ui_.theme.accent, 2.0);
    y += 14 + 14 * k;
    ui_.font(44 * k, W800);
    ui_.text(rx, y + 24 * k, c.line1, ui_.theme.text);
    ui_.text(rx, y + 24 * k + 48 * k, c.line2, ui_.theme.text);
    y += 96 * k + 16 * k;
    ui_.font(17 * k, W400);
    y += ui_.markup(rx, y, std::min(rw, 460 * k), c.body, ui_.theme.dim, 1.55);

    // There is no decoration here either, so the window's own buttons come
    // first and Skip sits to the left of them.  The last page asks a question
    // instead of offering Skip, and answers it with its own buttons.
    const Rect band{full.x, full.y, full.w, 56};
    Rect skip{};
    if (page < 4) {
        skip = {full.x + full.w - 12 - 36 - 2 - 36 - 16 - 60, band.y + 14, 60, 28};
        if (ui_.click(kIdTour + 90, skip)) {
            welcome_ = -1;
            seenWelcome_ = true;
            if (!seenTour_) tour_ = 0;
            staticDirty_ = true;
        }
        ui_.font(14, W600);
        ui_.text(skip.x + skip.w, skip.cy(), "Skip",
                 ui_.over(skip) ? ui_.theme.text : ui_.theme.faint, Align::Right);
    }
    drawWindowButtons(band, skip.w > 0 ? std::vector<Rect>{skip}
                                       : std::vector<Rect>{});

    if (page < 4) {
        y += 34 * k;
        double bx = rx;
        if (page > 0) {
            const Rect back{bx, y, 104, 54};
            if (ui_.ghostPill(kIdTour + 91, back, "Back", 15.5)) advance(page - 1);
            bx += back.w + 12;
        }
        ui_.font(16, W700);
        const Rect next{bx, y, ui_.textWidth("Get started →") + 60, 54};
        if (ui_.pill(kIdTour + 92, next, page == 0 ? "Get started →" : "Next →",
                     ui_.theme.text, ui_.theme.ground, 16))
            advance(page + 1);
    } else {
        y += 30 * k;
        const double bw = std::min(rw, 420.0);
        const Rect g{rx, y, bw, 54};
        if (ui_.pill(kIdTour + 93, g, "Continue with Google",
                     Rgb::hex(0xFFFFFF), Rgb::hex(0x161514), 16))
            setStatus("Accounts are not switched on yet — the app works without one.",
                      ui_.theme.faint);
        const Rect h{rx, y + 64, bw, 54};
        if (ui_.pill(kIdTour + 94, h, "Continue with GitHub",
                     ui_.theme.card, ui_.theme.text, 16))
            setStatus("Accounts are not switched on yet — the app works without one.",
                      ui_.theme.faint);
        const Rect s{rx, y + 128, bw, 44};
        if (ui_.click(kIdTour + 95, s)) advance(5);
        ui_.font(15, W700);
        ui_.text(s.cx(), s.cy(), "Skip, take me to Studio", ui_.theme.accent, Align::Centre);
    }
}

// The mark on page one: the dot travels the ring, its tail behind it, and the
// smile brightens as it passes the front.  Painted every frame, so it is kept
// out of the cached chrome.
void App::drawWelcomeMark(const Rect& art) {
    if (art.w <= 0) return;
    cairo_t* cr = ui_.cr;
    const double a = art.w / 420.0;
    const double cx = art.cx(), cy = art.cy();
    const double ring = 146 * a;

    const double t = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double lap = std::fmod(t, 7.0) / 7.0;          // one lap, seven seconds
    const double ang = lap * 2 * M_PI;                   // from the top, clockwise

    // the ring it travels
    ui_.setColour(ui_.theme.line, 0.6);
    cairo_set_line_width(cr, 1);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, 200 * a, 0, 2 * M_PI);
    cairo_stroke(cr);

    ui_.setColour(ui_.theme.accent, 0.5);
    cairo_set_line_width(cr, 1.6);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    const double dash[2] = {1, 8};
    cairo_set_dash(cr, dash, 2, 0);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, ring, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    // the tail, thinning and fading behind it
    const int steps = 26;
    const double sweep = 0.95;                           // radians of tail
    for (int i = 0; i < steps; ++i) {
        const double f0 = double(i) / steps, f1 = double(i + 1) / steps;
        const double a0 = ang - sweep * (1 - f0), a1 = ang - sweep * (1 - f1);
        ui_.setColour(ui_.theme.motion, f1 * f1 * 0.9);
        cairo_set_line_width(cr, (1.5 + f1 * 5.0) * a);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, ring, a0 - M_PI / 2, a1 - M_PI / 2);
        cairo_stroke(cr);
    }
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

    const double dx = cx + std::sin(ang) * ring;
    const double dy = cy - std::cos(ang) * ring;
    ui_.glow(dx, dy, 38 * a, ui_.theme.motion, 0.5);
    ui_.circle(dx, dy, 13 * a, ui_.theme.motion);
    ui_.setColour(Rgb::hex(0xFFD1E3), 0.6);
    cairo_set_line_width(cr, 2);
    cairo_new_path(cr);
    cairo_arc(cr, dx, dy, 13 * a, 0, 2 * M_PI);
    cairo_stroke(cr);

    // the mark, still, its smile catching the light once a lap as the dot
    // crosses the front
    const double d = std::fabs(lap - 0.5);
    const double pulse = std::exp(-(d * d) / (2 * 0.05 * 0.05));
    const Rect tile{cx - 65 * a, cy - 65 * a, 130 * a, 130 * a};
    ui_.shadow(tile, 38 * a, 16, 0.5, 10);
    ui_.fillRound(tile, 38 * a, ui_.theme.card);
    ui_.strokeRound(tile, 38 * a, ui_.theme.text, 1, 0.06);
    if (pulse > 0.01)
        ui_.glow(tile.cx(), tile.cy() + 20 * a, 90 * a, ui_.theme.accent, 0.20 * pulse);
    ui_.logo({tile.cx() - 48 * a, tile.cy() - 48 * a, 96 * a, 96 * a},
             0.8 + 0.2 * pulse);
}

// --- the studio tour ------------------------------------------------------

void App::drawTour(const Rect& full) {
    const int page = tour_;
    // dim what it is talking about
    ui_.fillRect(full, ui_.theme.dark ? Rgb{0, 0, 0} : Rgb{0.1, 0.1, 0.1}, 0.55);

    const double w = std::min(770.0, full.w - 80);
    const double h = std::min(520.0, full.h - 80);
    const Rect dlg{full.cx() - w * 0.5, full.cy() - h * 0.5, w, h};
    ui_.shadow(dlg, 22, 26, 0.6, 12);
    ui_.fillRound(dlg, 22, ui_.theme.card);
    ui_.strokeRound(dlg, 22, ui_.theme.text, 1, 0.05);

    cairo_save(ui_.cr);
    ui_.roundRect(dlg, 22);
    cairo_clip(ui_.cr);

    const Rect art{dlg.x, dlg.y, 320, dlg.h};
    ui_.fillRect(art, ui_.theme.ground);
    ui_.glow(art.x + art.w * 0.42, art.y + art.h * 0.44, art.w * 0.8,
             ui_.theme.accent, ui_.theme.dark ? 0.16 : 0.09);
    ui_.glow(art.x + art.w * 0.62, art.y + art.h * 0.28, art.w * 0.7,
             ui_.theme.motion, ui_.theme.dark ? 0.16 : 0.08);

    const double acx = art.cx(), acy = art.cy();
    switch (page) {
    case 0: {                                   // the pink dot
        const double s = 290, k = s / 290.0;
        ui_.circle(acx, acy, 136 * k, mix(ui_.theme.ground, ui_.theme.text, 0.04));
        ui_.strokeRound({acx - 136 * k, acy - 136 * k, 272 * k, 272 * k}, 136 * k,
                        ui_.theme.line, 1, 0.6);
        ui_.setColour(ui_.theme.accent, 0.5);
        cairo_set_line_width(ui_.cr, 1.5);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        const double dash[2] = {1, 7};
        cairo_set_dash(ui_.cr, dash, 2, 0);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, acx, acy, 92 * k, 0, 2 * M_PI);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        ui_.setColour(ui_.theme.motion, 0.35);
        cairo_set_line_width(ui_.cr, 5);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, acx, acy, 92 * k, -M_PI * 0.5, -M_PI * 0.15);
        cairo_stroke(ui_.cr);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);
        const double dx = acx + std::cos(-M_PI * 0.15) * 92 * k;
        const double dy = acy + std::sin(-M_PI * 0.15) * 92 * k;
        ui_.glow(dx, dy, 26, ui_.theme.motion, 0.5);
        ui_.circle(dx, dy, 10, ui_.theme.motion);
        ui_.circle(acx - 14 * k, acy, 4.5 * k, ui_.theme.raised);
        ui_.circle(acx + 14 * k, acy, 4.5 * k, ui_.theme.raised);
        ui_.circle(acx, acy, 15 * k, ui_.theme.raised);
        ui_.font(9, W700);
        ui_.tracked(acx, acy - 136 * k - 12, "FRONT", ui_.theme.ghost, 2.0, Align::Centre);
        ui_.tracked(acx, acy + 136 * k + 12, "BACK", ui_.theme.ghost, 2.0, Align::Centre);
        break;
    }
    case 1: {                                   // eight modes
        const double tw = 111, th = 58, gap = 8;
        for (int i = 0; i < 8; ++i) {
            const Rect t{acx - tw - gap * 0.5 + (tw + gap) * (i % 2),
                         acy - (th * 4 + gap * 3) * 0.5 + (th + gap) * (i / 2), tw, th};
            const bool on = i == 0;
            if (on) { ui_.fillRound(t, 14, ui_.theme.tint);
                      ui_.strokeRound(t, 14, ui_.theme.accent, 1.5, 0.7); }
            else ui_.fillRound(t, 14, ui_.theme.well);
            const Rgb c = on ? ui_.theme.deep : ui_.theme.faint;
            static const Icon* glyphs[8] = {
                &ico::kCircular, &ico::kPingPong, &ico::kPendulum, &ico::kLinear,
                &ico::kFigure8, &ico::kSpiral, &ico::kRandom, &ico::kStaticRing};
            static const char* names[8] = {"Circle", "Ping-pong", "Pendulum", "Linear",
                                           "Figure 8", "Spiral", "Random", "Static"};
            static const double dsh[2] = {1.6, 2.4};
            ui_.icon(*glyphs[i], {t.cx() - 11, t.y + 9, 22, 22}, c,
                     1.0, i == 7 ? dsh : nullptr, i == 7 ? 2 : 0);
            if (i == 7) ui_.icon(ico::kStaticPos, {t.cx() - 11, t.y + 9, 22, 22}, c);
            ui_.font(10.5, on ? W600 : W400);
            ui_.text(t.cx(), t.y + t.h - 12, names[i], c, Align::Centre);
        }
        break;
    }
    case 2: {                                   // a knob
        const double r = 56, sw = 10;
        const double cx = acx - 22, cy = acy;
        ui_.setColour(ui_.theme.line);
        cairo_set_line_width(ui_.cr, sw);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, cx, cy, r, M_PI * 0.75, M_PI * 0.75 + M_PI * 1.5);
        cairo_stroke(ui_.cr);
        ui_.setColour(ui_.theme.accent, 0.2);
        cairo_set_line_width(ui_.cr, sw + 12);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, cx, cy, r, M_PI * 0.75, M_PI * 0.75 + M_PI * 1.2);
        cairo_stroke(ui_.cr);
        ui_.setColour(ui_.theme.accent);
        cairo_set_line_width(ui_.cr, sw);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, cx, cy, r, M_PI * 0.75, M_PI * 0.75 + M_PI * 1.2);
        cairo_stroke(ui_.cr);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);
        {
            cairo_pattern_t* cap = cairo_pattern_create_radial(
                cx, cy - 13, 0, cx, cy, 59);
            cairo_pattern_add_color_stop_rgba(cap, 0, ui_.theme.line.r, ui_.theme.line.g,
                                              ui_.theme.line.b, 1);
            cairo_pattern_add_color_stop_rgba(cap, 1, ui_.theme.well.r, ui_.theme.well.g,
                                              ui_.theme.well.b, 1);
            cairo_set_source(ui_.cr, cap);
            cairo_new_path(ui_.cr);
            cairo_arc(ui_.cr, cx, cy, 42, 0, 2 * M_PI);
            cairo_fill(ui_.cr);
            cairo_pattern_destroy(cap);
        }
        ui_.font(24, W800);
        ui_.text(cx, cy - 8, "80%", ui_.theme.text, Align::Centre);
        ui_.font(12, W400);
        ui_.text(cx, cy + 14, "Intensity", ui_.theme.faint, Align::Centre);

        // the way round, and the wheel that steps it
        ui_.setColour(ui_.theme.deep, 0.5);
        cairo_set_line_width(ui_.cr, 2.4);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, cx, cy, 74, M_PI * 1.08, M_PI * 1.82);
        cairo_stroke(ui_.cr);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);
        {
            const double a = M_PI * 1.82;
            const double tx = cx + std::cos(a) * 74, ty = cy + std::sin(a) * 74;
            const double ax = -std::sin(a), ay = std::cos(a);
            ui_.setColour(ui_.theme.deep);
            cairo_new_path(ui_.cr);
            cairo_move_to(ui_.cr, tx + ax * 8, ty + ay * 8);
            cairo_line_to(ui_.cr, tx - ax * 2 + std::cos(a) * 7,
                          ty - ay * 2 + std::sin(a) * 7);
            cairo_line_to(ui_.cr, tx - ax * 2 - std::cos(a) * 7,
                          ty - ay * 2 - std::sin(a) * 7);
            cairo_close_path(ui_.cr);
            cairo_fill(ui_.cr);
        }

        const Rect strip{acx + 62, cy - 66, 44, 132};
        ui_.fillRound(strip, kPill, ui_.theme.card);
        const Rect mouse{strip.cx() - 9, strip.cy() - 13, 18, 26};
        ui_.strokeRound(mouse, 9, ui_.theme.text, 1.6);
        ui_.setColour(ui_.theme.accent);
        cairo_set_line_width(ui_.cr, 2.4);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(ui_.cr);
        cairo_move_to(ui_.cr, mouse.cx(), mouse.y + 6);
        cairo_line_to(ui_.cr, mouse.cx(), mouse.y + 12);
        cairo_stroke(ui_.cr);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);
        break;
    }
    case 3: {                                   // a room
        const Rect box{acx - 132, acy - 105, 264, 194};
        ui_.fillRound(box, 34, ui_.theme.card);
        ui_.strokeRound(box, 34, ui_.theme.line);
        const double rings[3] = {34, 58, 82};
        const double alphas[3] = {0.85, 0.5, 0.26};
        for (int i = 0; i < 3; ++i) {
            ui_.setColour(ui_.theme.accent, alphas[i]);
            cairo_set_line_width(ui_.cr, 3);
            cairo_new_path(ui_.cr);
            cairo_arc(ui_.cr, box.cx(), box.cy(), rings[i], 0, 2 * M_PI);
            cairo_stroke(ui_.cr);
        }
        ui_.glow(box.cx(), box.cy(), 30, ui_.theme.motion, 0.3);
        ui_.circle(box.cx(), box.cy(), 11, ui_.theme.motion);
        ui_.font(11, W600);
        ui_.tracked(acx, box.y + box.h + 26, "ROOM 62%  ·  DAMPING 40%",
                    ui_.theme.ghost, 1.4, Align::Centre);
        break;
    }
    case 4: {                                   // echoes
        const double xs[4] = {-85, -21, 37, 85};
        const double rs[4] = {16, 13, 10, 7.5};
        const double as[4] = {1.0, 0.6, 0.34, 0.18};
        for (int i = 0; i < 4; ++i)
            ui_.circle(acx + xs[i], acy, rs[i], ui_.theme.motion, as[i]);
        ui_.setColour(ui_.theme.line);
        cairo_set_line_width(ui_.cr, 2);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_ROUND);
        const double dash[2] = {2, 8};
        cairo_set_dash(ui_.cr, dash, 2, 0);
        cairo_new_path(ui_.cr);
        cairo_move_to(ui_.cr, acx - 85, acy + 40);
        cairo_line_to(ui_.cr, acx + 85, acy + 40);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        cairo_set_line_cap(ui_.cr, CAIRO_LINE_CAP_BUTT);
        ui_.font(11, W600);
        ui_.tracked(acx, acy + 66, "340 ms  ·  38% FEEDBACK", ui_.theme.ghost,
                    1.4, Align::Centre);
        break;
    }
    case 5: {                                   // character and EQ
        const double tw = 74, th = 58;
        static const Icon* glyphs[3] = {&ico::kSlowed, &ico::kSpark, &ico::kWide};
        static const char* names[3] = {"Warm", "Bright", "Wide"};
        for (int i = 0; i < 3; ++i) {
            const Rect t{acx - (tw * 3 + 16) * 0.5 + (tw + 8) * i, acy - 92, tw, th};
            const bool on = i == 1;
            if (on) { ui_.fillRound(t, 14, ui_.theme.tint);
                      ui_.strokeRound(t, 14, ui_.theme.accent, 1.5, 0.7); }
            else ui_.fillRound(t, 14, ui_.theme.well);
            const Rgb c = on ? ui_.theme.deep : ui_.theme.faint;
            ui_.icon(*glyphs[i], {t.cx() - 11, t.y + 9, 22, 22}, c);
            ui_.font(10.5, on ? W600 : W400);
            ui_.text(t.cx(), t.y + t.h - 12, names[i], c, Align::Centre);
        }
        const Rect card{acx - 118, acy - 20, 236, 108};
        ui_.fillRound(card, 16, ui_.theme.card);
        const double hs[5] = {26, 16, 4, 36, 20};
        for (int i = 0; i < 5; ++i) {
            const double bx = card.x + 16 + 42.0 * i;
            const Rect b{bx, card.y + 22 + (36 - hs[i]), 16, hs[i]};
            ui_.fillRound(b, 8, i == 2 ? ui_.theme.raised : ui_.theme.accent);
        }
        ui_.setColour(ui_.theme.line);
        cairo_set_line_width(ui_.cr, 1.5);
        const double dash[2] = {2, 6};
        cairo_set_dash(ui_.cr, dash, 2, 0);
        cairo_new_path(ui_.cr);
        cairo_move_to(ui_.cr, card.x + 12, card.y + 60);
        cairo_line_to(ui_.cr, card.x + card.w - 12, card.y + 60);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        const char* bands[5] = {"60", "250", "1k", "4k", "12k"};
        ui_.font(9.5, W400);
        for (int i = 0; i < 5; ++i)
            ui_.text(card.x + 24 + 42.0 * i, card.y + 78, bands[i], ui_.theme.ghost,
                     Align::Centre);
        break;
    }
    default: {                                  // presets and the switch
        int count = 0;
        const Preset* list = presets(count);
        double cx = acx - 110, cy = acy - 60;
        for (int i = 0; i < 4 && i < count; ++i) {
            ui_.font(12.5, i == 0 ? W700 : W500);
            const Rect c{cx, cy, ui_.textWidth(list[i].name) + 26, 32};
            if (c.x + c.w > acx + 118) { cx = acx - 110; cy += 40; }
            const Rect c2{cx, cy, c.w, 32};
            ui_.fillRound(c2, kPill, i == 0 ? ui_.theme.accent : ui_.theme.well);
            ui_.text(c2.cx(), c2.cy(), list[i].name,
                     i == 0 ? ui_.theme.onAccent : ui_.theme.dim, Align::Centre);
            cx += c2.w + 8;
        }
        const Rect plus{cx, cy, 32, 32};
        ui_.fillRound(plus, kPill, ui_.theme.well);
        ui_.icon(ico::kPlus, {plus.x + 9.5, plus.y + 9.5, 13, 13}, ui_.theme.faint);

        const Rect dock{acx - 96, acy + 48, 192, 56};
        ui_.glow(dock.cx(), dock.cy(), 90, ui_.theme.accent, 0.18);
        ui_.fillRound(dock, kPill, ui_.theme.card);
        ui_.font(14, W700);
        ui_.tracked(dock.x + 20, dock.cy(), "8D", ui_.theme.text, 1.4);
        const Rect sw{dock.x + 56, dock.cy() - 16, 58, 32};
        ui_.glow(sw.cx(), sw.cy(), 40, ui_.theme.accent, 0.5);
        ui_.fillRound(sw, kPill, ui_.theme.accent);
        ui_.circle(sw.x + sw.w - 4 - 12, sw.cy(), 12, ui_.theme.onAccent);
        ui_.font(13, W400);
        ui_.text(sw.x + sw.w + 12, dock.cy(), "Space", ui_.theme.faint);
        break;
    }
    }
    cairo_restore(ui_.cr);

    // the words
    struct Copy { const char* kicker; const char* title; const char* body; };
    static const Copy kCopy[7] = {
        {"STUDIO · HOW IT WORKS", "The pink dot is the sound",
         "It travels around your head, and you hear it move. In Static mode, drag it "
         "with the mouse to park the sound anywhere in the circle."},
        {"MOVEMENT", "Eight ways to move",
         "Circle, ping-pong, pendulum, figure 8 and more. Click one and the orbit "
         "changes straight away; CW / CCW flips the direction."},
        {"CONTROLS", "Turn a knob",
         "Grab a dial anywhere and move around it, like a real knob: it turns by as "
         "much as you turn, never jumping to meet the pointer. Hold Shift for finer, "
         "roll the wheel to step, double-click to reset."},
        {"SPACE", "Give the sound a room",
         "Reverb adds the room, Room size sets how big it is, Damping softens the "
         "walls, and Width spreads the sound further apart."},
        {"ECHO", "Repeats that follow the orbit",
         "Delay sets the gap between repeats, Feedback how many come back, Mix how "
         "loud they sit. Each echo travels behind the source, so the tail moves too."},
        {"CHARACTER · EQUALISER", "Then colour the sound",
         "Character is a one-click tone — warm, bright, wide. The five-band "
         "equaliser is underneath it when you want to shape it yourself."},
        {"PRESETS", "Save it, then A/B it",
         "Keep a setting you like as a preset with +, and recall it in one click. The "
         "big 8D switch — or the space bar — turns the whole effect off so "
         "you can hear what it is doing."},
    };
    const Copy& c = kCopy[page];
    const double tx = art.x + art.w + 34;
    const double tw = dlg.x + dlg.w - 34 - tx;
    double y = dlg.y + 34;

    for (int i = 0; i < 7; ++i) {
        const Rect b{tx + (28 + 5) * i, y, 28, 4};
        ui_.fillRound(b, kPill, ui_.theme.text, i <= page ? 1.0 : 0.14);
    }
    y += 4 + 22;
    ui_.font(11, W700);
    ui_.tracked(tx, y + 6, c.kicker, ui_.theme.accent, 2.0);
    y += 12 + 10;
    ui_.font(30, W800);
    y += ui_.markup(tx, y, tw, c.title, ui_.theme.text, 1.15) + 12;
    ui_.font(15.5, W400);
    ui_.markup(tx, y, tw, c.body, ui_.theme.dim, 1.55);

    // skip, and the way forward
    const Rect skip{dlg.x + dlg.w - 26 - 50, dlg.y + 26 - 12, 50, 26};
    if (ui_.click(kIdTour, skip)) { tour_ = -1; seenTour_ = true; staticDirty_ = true; }
    ui_.font(13.5, W600);
    ui_.text(skip.x + skip.w, skip.cy(), "Skip",
             ui_.over(skip) ? ui_.theme.text : ui_.theme.faint, Align::Right);

    const double by = dlg.y + dlg.h - 26 - 44;
    ui_.font(15, W700);
    const std::string nextLabel = page == 6 ? "Open Studio" : "Next →";
    const Rect next{dlg.x + dlg.w - 34 - (ui_.textWidth(nextLabel) + 48), by,
                    ui_.textWidth(nextLabel) + 48, 44};
    if (ui_.pill(kIdTour + 1, next, nextLabel,
                 page == 6 ? ui_.theme.accent : ui_.theme.text,
                 page == 6 ? ui_.theme.onAccent : ui_.theme.ground, 15)) {
        if (page == 6) { tour_ = -1; seenTour_ = true; }
        else tour_ = page + 1;
        staticDirty_ = true;
    }
    if (page > 0) {
        const Rect back{next.x - 10 - 78, by, 78, 44};
        if (ui_.ghostPill(kIdTour + 2, back, "Back")) { tour_ = page - 1; staticDirty_ = true; }
    }
}

// --- the written guides ---------------------------------------------------

void App::drawGuide(const Rect& full) {
    struct Point { const char* title; const char* body; };
    struct GuideDef {
        const char* kicker; const char* title;
        const char* intro;
        Point points[5];
    };
    static const GuideDef kGuides[4] = {
        {"SETUP", "Capture your system audio",
         "8D Music creates its own output device. Everything that plays into it is "
         "spatialised and passed on to the speakers or headphones you pick here.",
         {{"Pick the output first",
           "The pill in the title bar lists every real output PipeWire can see. Choose "
           "the headphones you are actually wearing, then press Start."},
          {"Move the apps across",
           "Anything already playing is pulled in automatically, and anything that "
           "starts afterwards follows. An app that pins its own output is nudged back "
           "every couple of seconds."},
          {"Nothing is installed",
           "The virtual sink lives inside this process. Quit the app and your audio "
           "goes back exactly as it was."},
          {nullptr, nullptr}, {nullptr, nullptr}}},

        {"THE EFFECT", "How 8D works",
         "The sound is treated as a source orbiting your head. Rather than swinging the "
         "stereo balance left and right, the position is turned into the cues a real "
         "sound would produce.",
         {{"Time between the ears",
           "The far ear hears the sound up to about 0.7 ms later. This is what pushes the "
           "image outside your head instead of leaving it stuck between your ears."},
          {"Level and head shadow",
           "Constant-power panning keeps loudness steady as the source travels, and your "
           "skull blocks high frequencies, so the far ear gets a gentle treble roll-off."},
          {"Front and back",
           "Positions behind you lose a little upper-mid, the way the outer ear shapes "
           "sound arriving from the rear."},
          {"Distance",
           "Level, air absorption and how much reverb is sent all follow the orbit "
           "radius — the Distance knob in Studio."},
          {nullptr, nullptr}}},

        {"LISTENING", "Why headphones",
         "8D works by giving each ear its own version of the sound: slightly different "
         "timing, level and tone.",
         {{"Speakers undo it",
           "They send both versions to both ears, which mixes them back together and "
           "cancels the effect. Any headphones or earbuds work — they do not need to "
           "be expensive, or to advertise spatial audio of their own."},
          {"Turn other spatial effects off",
           "Two effects fighting each other sound worse than either alone. If your "
           "headphones have a spatial mode of their own, switch it off."},
          {nullptr, nullptr}, {nullptr, nullptr}, {nullptr, nullptr}}},

        {"LIMITS", "An app shows “Nothing playing”",
         "The panel in Studio reads what a player publishes on the session bus over "
         "MPRIS. Spotify publishes a title, and browsers report whatever the page says.",
         {{"Some apps publish nothing",
           "Games and most chat apps say nothing at all. They read as “Nothing "
           "playing” while you can plainly hear them. That is the boundary of the "
           "bus, not a fault in the app."},
          {"The effect still applies",
           "Whether the title shows has nothing to do with whether the sound is "
           "spatialised — that depends only on whether its audio reaches our sink."},
          {"Exclusive mode",
           "An app that grabs the hardware directly bypasses everything, including us. "
           "Set it to use the default output and it comes back."},
          {nullptr, nullptr}, {nullptr, nullptr}}},
    };
    const GuideDef& g = kGuides[std::clamp(guide_, 0, 3)];

    ui_.fillRect(full, Rgb{0, 0, 0}, 0.55);
    const double w = std::min(760.0, full.w - 80);
    const double h = std::min(660.0, full.h - 60);
    const Rect dlg{full.cx() - w * 0.5, full.y + (full.h - h) * 0.5, w, h};
    ui_.shadow(dlg, 22, 26, 0.6, 12);
    ui_.fillRound(dlg, 22, ui_.theme.card);

    const double x = dlg.x + 34, tw = dlg.w - 68;
    double y = dlg.y + 30;
    ui_.font(11, W700);
    ui_.tracked(x, y + 6, g.kicker, ui_.theme.accent, 2.0);
    y += 12 + 12;
    ui_.font(28, W800);
    y += ui_.markup(x, y, tw, g.title, ui_.theme.text, 1.15) + 14;
    ui_.font(14, W400);
    y += ui_.markup(x, y, tw, g.intro, ui_.theme.dim, 1.55) + 16;

    for (const Point& p : g.points) {
        if (!p.title) break;
        ui_.font(13.5, W400);
        const double bodyH = ui_.paragraphHeight(tw - 36, p.body, 1.5);
        const double ph = 14 + 18 + 4 + bodyH + 14;
        ui_.fillRound({x, y, tw, ph}, 16, ui_.theme.well);
        ui_.circle(x + 20, y + 23, 4, ui_.theme.accent);
        ui_.font(13.5, W700);
        ui_.text(x + 34, y + 23, p.title, ui_.theme.text);
        ui_.font(13.5, W400);
        ui_.markup(x + 34, y + 36, tw - 52, p.body, ui_.theme.dim, 1.5);
        y += ph + 10;
    }

    const Rect close{dlg.x + dlg.w - 34 - 96, dlg.y + dlg.h - 26 - 42, 96, 42};
    if (ui_.pill(kIdAbout + 60, close, "Close", ui_.theme.text, ui_.theme.ground, 14)) {
        guide_ = -1;
        staticDirty_ = true;
    }
}

} // namespace eightd
