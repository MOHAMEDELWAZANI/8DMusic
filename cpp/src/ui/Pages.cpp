// About and Account.
//
// Two pages that do not move: what this is, where to find it, and the one
// thing an account would be for.
#include "App.h"
#include "Layout.h"

namespace eightd {

namespace {
constexpr double kPad = 22, kGap = 18, kLeftW = 520;
constexpr const char* kRepoUrl = "https://github.com/MOHAMEDELWAZANI/8DMusic";
} // namespace

void App::drawAbout(const Rect& body) {
    const double top = body.y + 18;
    const double bottom = body.y + body.h - 16;
    const Rect left{body.x + kPad, top, kLeftW, bottom - top};
    const Rect right{left.x + kLeftW + kGap, top,
                     body.w - kPad * 2 - kLeftW - kGap, bottom - top};

    // ---- who we are -----------------------------------------------------
    double y = left.y;

    // hero
    {
        // The headline decides how tall this card is, so it is measured before
        // anything is drawn.
        const double markSide = 132;
        const std::string headline = "Real-time 8D for everything your computer plays";
        const double tx = left.x + 22 + markSide + 22;
        const double tw = left.x + left.w - 22 - tx;
        ui_.font(24, W600, true);
        const double th = ui_.paragraphHeight(tw, headline, 1.2);
        const double h = std::max(markSide + 44, 42 + th + 16 + 28 + 22);

        ui_.fillRound({left.x, y, left.w, h}, kCardR, ui_.theme.card);
        const Rect mark{left.x + 22, y + (h - markSide) * 0.5, markSide, markSide};
        ui_.glow(mark.cx(), mark.cy(), 96, ui_.theme.accent,
                 ui_.theme.dark ? 0.14 : 0.08);
        ui_.setColour(ui_.theme.line);
        cairo_set_line_width(ui_.cr, 1);
        const double dash[2] = {1, 7};
        cairo_set_dash(ui_.cr, dash, 2, 0);
        cairo_new_path(ui_.cr);
        cairo_arc(ui_.cr, mark.cx(), mark.cy(), 62, 0, 2 * M_PI);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        ui_.circle(mark.cx(), mark.cy(), 46, ui_.theme.well);
        ui_.strokeRound({mark.cx() - 46, mark.cy() - 46, 92, 92}, 46,
                        ui_.theme.accent, 1, 0.25);
        ui_.logo({mark.cx() - 31, mark.cy() - 31, 62, 62});

        ui_.font(10.5, W700);
        ui_.tracked(tx, y + 30, "8D MUSIC", ui_.theme.faint, 3.0);
        ui_.font(24, W600, true);
        ui_.markup(tx, y + 42, tw, headline, ui_.theme.text, 1.2);
        const double chipY = y + 42 + th + 16;
        const char* chips[3] = {"Version 1.0", "Linux \u00B7 Windows", "Open source"};
        double cx = tx;
        for (int i = 0; i < 3; ++i) {
            ui_.font(11.5, i == 2 ? W600 : W400);
            const Rect c{cx, chipY, ui_.textWidth(chips[i]) + 26, 28};
            ui_.fillRound(c, kPill, i == 2 ? mix(ui_.theme.card, ui_.theme.motion, 0.18)
                                           : ui_.theme.well);
            ui_.text(c.cx(), c.cy(), chips[i],
                     i == 2 ? mix(ui_.theme.motion, ui_.theme.text, 0.35) : ui_.theme.dim,
                     Align::Centre);
            cx += c.w + 7;
        }
        y += h + 14;
    }

    // our story
    {
        const double h = 160;
        ui_.fillRound({left.x, y, left.w, h}, kCardR, ui_.theme.card);
        const double x = left.x + 18, w = left.w - 36;
        ui_.font(10.5, W700);
        ui_.tracked(x, y + 22, "OUR STORY", ui_.theme.accent, 2.0);
        ui_.font(17, W700);
        double used = ui_.markup(x, y + 33, w,
            "One effect, written once in C++, sounding the same on Linux, "
            "Windows and Android.", ui_.theme.text, 1.35);
        ui_.font(13, W400);
        used += 9;
        ui_.markup(x, y + 33 + used, w,
            "No uploading, no converting. Spotify, browsers, games and your own "
            "files are spatialised live on their way to your headphones.",
            ui_.theme.dim, 1.55);

        const Rect link{x, y + h - 34, 160, 24};
        if (ui_.click(kIdAbout + 1, link)) openUrl(kRepoUrl + std::string("#readme"));
        ui_.font(13, W700);
        ui_.text(link.x, link.cy(), "Read the full story", ui_.theme.accent);
        ui_.icon(ico::kChevron,
                 {link.x + ui_.textWidth("Read the full story") + 6, link.cy() - 7, 14, 14},
                 ui_.theme.accent);
        y += h + 14;
    }

    // what's new
    {
        const double h = 168;
        ui_.fillRound({left.x, y, left.w, h}, kCardR, ui_.theme.card);
        const double x = left.x + 18, w = left.w - 36;
        ui_.font(10.5, W700);
        ui_.tracked(x, y + 22, "WHAT'S NEW IN 1.0", ui_.theme.accent, 2.0);
        const Rect log{left.x + left.w - 18 - 110, y + 12, 110, 20};
        if (ui_.click(kIdAbout + 2, log)) openUrl(kRepoUrl + std::string("/releases"));
        ui_.font(11.5, W400);
        ui_.text(log.x + log.w - 16, log.cy(), "Full changelog", ui_.theme.faint,
                 Align::Right);
        ui_.icon(ico::kExternal, {log.x + log.w - 12, log.cy() - 6, 12, 12},
                 ui_.theme.faint);

        const char* bullets[3] = {
            "<b>One window, every control.</b> Movement, space, echo, character and "
            "EQ all sit beside the orbit — nothing is a page away any more.",
            "<b>Presets you can A/B.</b> Save a setting, recall it in one click, and "
            "hit Space to hear the track dry.",
            "<b>The same engine as the phone.</b> One C++20 core, so a preset sounds "
            "identical on Linux, Windows and Android.",
        };
        double by = y + 38;
        ui_.font(12.5, W400);
        for (int i = 0; i < 3; ++i) {
            ui_.text(x + 2, by + 9, "·", ui_.theme.accent);
            ui_.font(12.5, W400);
            by += ui_.markup(x + 12, by, w - 12, bullets[i], ui_.theme.dim, 1.5) + 8;
        }
        y += h + 14;
    }

    // support, pinned to the foot of the column
    {
        const double h = 92;
        const Rect c{left.x, left.y + left.h - h, left.w, h};
        ui_.fillRound(c, kCardR, ui_.theme.card);
        cairo_pattern_t* p = cairo_pattern_create_linear(c.x, c.y, c.x + c.w * 0.72, c.y + c.h);
        cairo_pattern_add_color_stop_rgba(p, 0, ui_.theme.motion.r, ui_.theme.motion.g,
                                          ui_.theme.motion.b, 0.22);
        cairo_pattern_add_color_stop_rgba(p, 1, ui_.theme.motion.r, ui_.theme.motion.g,
                                          ui_.theme.motion.b, 0.0);
        ui_.roundRect(c, kCardR);
        cairo_set_source(ui_.cr, p);
        cairo_fill(ui_.cr);
        cairo_pattern_destroy(p);

        ui_.glow(c.x + 44, c.cy(), 46, ui_.theme.motion, 0.35);
        ui_.circle(c.x + 44, c.cy(), 24, ui_.theme.motion);
        ui_.icon(ico::kHeart, {c.x + 33, c.cy() - 11, 22, 22}, Rgb::hex(0xFFFFFF));
        const Rect b{c.x + c.w - 20 - 112, c.cy() - 20, 112, 40};
        ui_.font(15, W700);
        ui_.text(c.x + 84, c.cy() - 20, "Support 8D Music", ui_.theme.text);
        ui_.font(12.5, W400);
        ui_.markup(c.x + 84, c.cy() - 8, b.x - 14 - (c.x + 84),
                   "Free, no ads, open source. A donation keeps it that way.",
                   ui_.theme.dim, 1.45);
        if (ui_.pill(kIdAbout + 3, b, "Donate", ui_.theme.text, ui_.theme.ground, 14))
            openUrl(kRepoUrl);
    }

    // ---- what you can do from here ---------------------------------------
    y = right.y;

    // presentations
    {
        ui_.font(19, W600, true);
        ui_.text(right.x, y + 13, "Presentations", ui_.theme.text);
        ui_.font(12, W400);
        ui_.text(right.x + right.w, y + 13, "Watch again", ui_.theme.faint, Align::Right);
        y += 26 + 11;

        struct Show { const char* name; const char* pages; bool welcome; };
        const Show shows[2] = {{"Welcome", "5 pages", true}, {"Studio 8D", "7 pages", false}};
        const double tw = (right.w - 12) * 0.5;
        for (int i = 0; i < 2; ++i) {
            const Rect t{right.x + (tw + 12) * i, y, tw, 118};
            if (ui_.click(kIdAbout + 10 + i, t)) {
                if (shows[i].welcome) { welcome_ = 0; }
                else { tour_ = 0; page_ = Page::Studio; }
                staticDirty_ = true;
            }
            // These two are artwork, not surfaces: they keep their own dark
            // ground in either theme, the way a poster does.
            ui_.fillRound(t, 18, i == 0 ? Rgb::hex(0x241C26) : Rgb::hex(0x14201F));
            cairo_save(ui_.cr);
            ui_.roundRect(t, 18);
            cairo_clip(ui_.cr);
            const Rgb a = i == 0 ? Rgb::hex(0x8E2C5B) : Rgb::hex(0x14506B);
            const Rgb b = i == 0 ? Rgb::hex(0x2A3F63) : Rgb::hex(0x5B2340);
            ui_.glow(t.x + t.w * 0.26, t.y + t.h * 0.22, t.w * 0.62, a, 0.95);
            ui_.glow(t.x + t.w * 0.78, t.y + t.h * 0.78, t.w * 0.6, b, 0.9);
            cairo_restore(ui_.cr);

            ui_.fillRound({t.x + 14, t.y + 14, 32, 32}, kPill, Rgb{0, 0, 0}, 0.34);
            ui_.icon(ico::kPlay, {t.x + 24, t.y + 24, 12, 12}, Rgb::hex(0xFFFFFF));
            ui_.font(14.5, W700);
            ui_.text(t.x + 14, t.y + t.h - 34, shows[i].name, Rgb::hex(0xF3F2F2));
            ui_.font(11.5, W400);
            ui_.text(t.x + 14, t.y + t.h - 17, shows[i].pages, Rgb::hex(0xBAB6B6));
        }
        y += 118 + 14;
    }

    // guides
    {
        ui_.font(19, W600, true);
        ui_.text(right.x, y + 13, "Guides", ui_.theme.text);
        y += 26 + 11;

        struct Row { const Icon* ic; bool tinted; const char* title; const char* sub; };
        const Row rows[4] = {
            {&ico::kMonitor, true, "Capture your system audio",
             "Pick the loopback device, once"},
            {&ico::kStudio, false, "How 8D works",
             "The five cues that move sound around you"},
            {&ico::kHeadphones, false, "Why headphones",
             "8D needs each ear to hear its own side"},
            {&ico::kClock, false, "An app shows “Nothing playing”",
             "Exclusive-mode apps and what to do about them"},
        };
        const double h = 4 * 58 + 8;
        ui_.fillRound({right.x, y, right.w, h}, kCardR, ui_.theme.card);
        for (int i = 0; i < 4; ++i) {
            const Rect row{right.x + 14, y + 4 + 58.0 * i, right.w - 28, 58};
            if (i) ui_.fillRect({row.x + 4, row.y, row.w - 8, 1}, ui_.theme.text, 0.05);
            if (ui_.listRow(kIdAbout + 20 + i, row, *rows[i].ic,
                            rows[i].tinted ? ui_.theme.accent : ui_.theme.dim,
                            rows[i].tinted ? mix(ui_.theme.card, ui_.theme.accent, 0.12)
                                           : ui_.theme.well,
                            rows[i].title, rows[i].sub, ico::kChevron)) {
                guide_ = i;
                staticDirty_ = true;
            }
        }
        y += h + 14;
    }

    // get involved
    {
        ui_.font(19, W600, true);
        ui_.text(right.x, y + 13, "Get involved", ui_.theme.text);
        y += 26 + 11;

        const double h = 3 * 58 + 8;
        ui_.fillRound({right.x, y, right.w, h}, kCardR, ui_.theme.card);

        const Rect r0{right.x + 14, y + 4, right.w - 28, 58};
        if (ui_.listRow(kIdAbout + 30, r0, ico::kGithub, ui_.theme.ground,
                        ui_.theme.text, "Source code",
                        "github.com/MOHAMEDELWAZANI/8DMusic", ico::kExternal))
            openUrl(kRepoUrl);
        const Rect r1{right.x + 14, y + 4 + 58, right.w - 28, 58};
        ui_.fillRect({r1.x + 4, r1.y, r1.w - 8, 1}, ui_.theme.text, 0.05);
        if (ui_.listRow(kIdAbout + 31, r1, ico::kGlobe, ui_.theme.dim, ui_.theme.well,
                        "Website", "News, releases and the mobile build", ico::kExternal))
            openUrl(kRepoUrl);
        const Rect r2{right.x + 14, y + 4 + 116, right.w - 28, 58};
        ui_.fillRect({r2.x + 4, r2.y, r2.w - 8, 1}, ui_.theme.text, 0.05);
        if (ui_.listRow(kIdAbout + 32, r2, ico::kUser, ui_.theme.accent,
                        mix(ui_.theme.card, ui_.theme.accent, 0.12), "Help build 8D Music",
                        "Sign in to test early versions — that is the Account tab",
                        ico::kChevron))
            setPage(Page::Account);
        y += h + 14;
    }

    // the footer
    {
        const double fy = right.y + right.h - 10;
        ui_.icon(ico::kLock, {right.x, fy - 7, 13, 13}, ui_.theme.ghost);
        ui_.font(11.5, W400);
        ui_.text(right.x + 20, fy,
                 "Your audio never leaves this computer · 8D Music 1.0 · "
                 "C++20 DSP engine · Cairo UI", ui_.theme.ghost);
    }
}

// --- account -------------------------------------------------------------

void App::drawAccount(const Rect& body) {
    ui_.glow(body.cx(), body.y + body.h * 0.42, 420, ui_.theme.accent,
             ui_.theme.dark ? 0.14 : 0.08);
    ui_.glow(body.cx() + 90, body.y + body.h * 0.22, 320, ui_.theme.motion,
             ui_.theme.dark ? 0.10 : 0.06);

    const double cx = body.cx();
    double y = body.y + body.h * 0.5 - 214;

    // the empty seat
    ui_.setColour(ui_.theme.line);
    cairo_set_line_width(ui_.cr, 1);
    const double dash[2] = {1, 7};
    cairo_set_dash(ui_.cr, dash, 2, 0);
    cairo_new_path(ui_.cr);
    cairo_arc(ui_.cr, cx, y + 48, 45, 0, 2 * M_PI);
    cairo_stroke(ui_.cr);
    cairo_set_dash(ui_.cr, nullptr, 0, 0);
    ui_.circle(cx, y + 48, 34, ui_.theme.card);
    ui_.icon(ico::kUser, {cx - 17, y + 31, 34, 34}, ui_.theme.faint);
    y += 96 + 16;

    ui_.font(27, W600, true);
    ui_.text(cx, y + 17, "You are not signed in", ui_.theme.text, Align::Centre);
    y += 34 + 12;

    ui_.font(14, W400);
    const std::string blurb =
        "8D Music works fully without an account. Signing in is only for testing "
        "early builds and keeping your presets on every machine.";
    const double bw = 470;
    ui_.markup(cx - bw * 0.5, y, bw, blurb, ui_.theme.dim, 1.55, Align::Centre);
    y += 50 + 20;

    const double bwid = 430;
    const Rect google{cx - bwid * 0.5, y, bwid, 52};
    if (ui_.click(kIdAccount, google))
        setStatus("Accounts are not switched on yet — the app works without one.",
                  ui_.theme.faint);
    ui_.fillRound(google, kPill, ui_.over(google)
                  ? mix(ui_.theme.text, ui_.theme.ground, 0.08) : ui_.theme.text);
    ui_.font(15, W600);
    {
        const double lw = ui_.textWidth("Continue with Google");
        const double gx = google.cx() - (lw + 11 + 19) * 0.5;
        // Google's mark, in its own colours
        struct Wedge { uint32_t colour; const char* d; };
        static const Wedge kG[4] = {
            {0x4285F4, "M23 12.3c0-.8-.1-1.6-.2-2.3H12v4.5h6.2a5.3 5.3 0 0 1-2.3 3.5v2.9h3.7c2.2-2 3.4-5 3.4-8.6z"},
            {0x34A853, "M12 23.5c3.1 0 5.7-1 7.6-2.8l-3.7-2.9c-1 .7-2.3 1.1-3.9 1.1-3 0-5.5-2-6.4-4.7H1.8v3C3.7 20.9 7.6 23.5 12 23.5z"},
            {0xFBBC05, "M5.6 14.2a6.9 6.9 0 0 1 0-4.4v-3H1.8a11.5 11.5 0 0 0 0 10.4l3.8-3z"},
            {0xEA4335, "M12 5.1c1.7 0 3.2.6 4.4 1.7l3.3-3.3C17.7 1.6 15.1.5 12 .5 7.6.5 3.7 3.1 1.8 6.8l3.8 3c.9-2.7 3.4-4.7 6.4-4.7z"},
        };
        for (const auto& wdg : kG) {
            ui_.setColour(Rgb::hex(wdg.colour));
            cairo_new_path(ui_.cr);
            SvgPath::add(ui_.cr, wdg.d, gx, google.cy() - 9.5, 19, 24);
            cairo_fill(ui_.cr);
        }
        ui_.font(15, W600);
        ui_.text(gx + 19 + 11, google.cy(), "Continue with Google", ui_.theme.ground);
    }
    y += 52 + 10;

    const Rect gh{cx - bwid * 0.5, y, bwid, 52};
    if (ui_.click(kIdAccount + 1, gh))
        setStatus("Accounts are not switched on yet — the app works without one.",
                  ui_.theme.faint);
    ui_.fillRound(gh, kPill, ui_.over(gh)
                  ? mix(ui_.theme.card, ui_.theme.text, 0.08) : ui_.theme.card);
    ui_.font(15, W600);
    {
        const double lw = ui_.textWidth("Continue with GitHub");
        const double gx = gh.cx() - (lw + 11 + 20) * 0.5;
        ui_.icon(ico::kGithub, {gx, gh.cy() - 10, 20, 20}, ui_.theme.text);
        ui_.text(gx + 20 + 11, gh.cy(), "Continue with GitHub", ui_.theme.text);
    }
    y += 52 + 12;

    ui_.font(13, W400);
    ui_.text(cx, y + 10, "Keep using 8D Music without an account", ui_.theme.faint,
             Align::Centre);

    // What this page becomes, once we have talked about it.
    const double sy = body.y + body.h - 62;
    ui_.setColour(ui_.theme.text, 0.10);
    cairo_set_line_width(ui_.cr, 1);
    const double sdash[2] = {4, 4};
    cairo_set_dash(ui_.cr, sdash, 2, 0);
    cairo_new_path(ui_.cr);
    cairo_move_to(ui_.cr, 0, sy); cairo_line_to(ui_.cr, body.w, sy);
    cairo_stroke(ui_.cr);
    cairo_set_dash(ui_.cr, nullptr, 0, 0);

    ui_.font(10.5, W700);
    const Rect tag{22, sy + 16, ui_.textWidth("TO DESIGN NEXT") + 26, 24};
    ui_.fillRound(tag, kPill, mix(ui_.theme.ground, ui_.theme.motion, 0.16));
    ui_.tracked(tag.cx(), tag.cy(), "TO DESIGN NEXT",
                mix(ui_.theme.motion, ui_.theme.text, 0.35), 1.4, Align::Centre);

    const char* stubs[4] = {"Profile & plan", "Presets synced across devices",
                            "Early builds / beta channel", "Contributor dashboard"};
    const double stubX = tag.x + tag.w + 12;
    const double stubW = (body.w - 22 - stubX - 8 * 3) / 4.0;
    for (int i = 0; i < 4; ++i) {
        const Rect s{stubX + (stubW + 8) * i, sy + 13, stubW, 34};
        ui_.setColour(ui_.theme.text, 0.12);
        cairo_set_line_width(ui_.cr, 1);
        cairo_set_dash(ui_.cr, sdash, 2, 0);
        ui_.roundRect(s.inset(0.5), 12);
        cairo_stroke(ui_.cr);
        cairo_set_dash(ui_.cr, nullptr, 0, 0);
        ui_.font(11.5, W400);
        ui_.text(s.cx(), s.cy(), stubs[i], ui_.theme.ghost, Align::Centre);
    }
}

} // namespace eightd
