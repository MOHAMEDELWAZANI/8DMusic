#include "App.h"
#include <X11/keysym.h>
#include <cstdio>
#include <cstring>

namespace eightd {

namespace {
constexpr double kRailWidth = 430;
constexpr double kTopBar    = 66;
constexpr double kPad       = 26;

std::string pct(double v)      { return fmt("%.0f%%", v * 100); }
std::string metres(double v)   { return fmt("%.2f m", v); }
std::string degrees(double v)  { return fmt("%+.0f°", v); }
std::string ms(double v)       { return fmt("%.0f ms", v * 1000); }
std::string speedText(double v) {
    char buf[80];
    std::snprintf(buf, sizeof buf, "%.2f rot/s · %.1f s", v, 1.0 / std::max(v, 0.01));
    return buf;
}
} // namespace

// --- lifecycle --------------------------------------------------------------

bool App::run(std::string& error) {
    if (!engine_.init(error)) return false;
    nowPlaying_.start();
    loadSettings();
    refreshDevices();
    pushParams();
    statusColour_ = Theme::light().inkFaint;

    // The registry tells us the moment a device appears or disappears; the flag
    // keeps the reaction on the UI thread where the list is actually used.
    engine_.graph().onSinksChanged = [this] { sinksDirty_ = true; };

    if (!window_.open("8D Music — Real-Time Spatial Audio", 1180, 820, error)) {
        engine_.shutdown();
        return false;
    }

    window_.onDraw   = [this](cairo_t* cr, int w, int h) { draw(cr, w, h); };
    window_.onMotion = [this](int x, int y) { ui_.mouseX = x; ui_.mouseY = y; };
    window_.onMouse  = [this](const MouseEvent& m) {
        staticDirty_ = true;
        if (m.button == 4 || m.button == 5) {           // wheel
            if (m.pressed) ui_.wheel = (m.button == 4) ? -1 : 1;
            return;
        }
        ui_.mouseX = m.x; ui_.mouseY = m.y;
        if (m.pressed) { ui_.mouseDown = true; ui_.mousePressed = true; }
        else           { ui_.mouseDown = false; ui_.mouseReleased = true; }
    };
    window_.onKey = [this](const KeyEvent& k) {
        staticDirty_ = true;
        if (k.keysym == XK_space) { bypass_ = !bypass_; params_.enabled = !bypass_; pushParams(); }
        else if (k.ctrl && (k.keysym == XK_r || k.keysym == XK_R)) {
            if (k.shift) resetSettings(); else startStop();
        } else if (k.ctrl && (k.keysym == XK_t || k.keysym == XK_T)) {
            dark_ = !dark_;
        } else if (k.keysym == XK_Escape) {
            ui_.openMenu = 0;
        }
    };
    window_.onIdle = [this] {
        if (sinksDirty_) { sinksDirty_ = false; refreshDevices(); }

        // The progress bar and its clock only ever move a pixel a second, so
        // the chrome is rebuilt when the displayed second changes, not per frame.
        if (nowPlaying_.has()) {
            const Track t = nowPlaying_.track();
            const double now = std::chrono::duration<double>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const long second = long(t.at(now));
            if (second != shownSecond_) { shownSecond_ = second; staticDirty_ = true; }
        } else if (shownSecond_ != -1) {
            shownSecond_ = -1; staticDirty_ = true;
        }

        if (std::getenv("EIGHTD_PROFILE")) {
            static auto mark = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            if (now - mark >= std::chrono::seconds(1)) {
                const double n = chromePasses_ ? double(chromePasses_) : 1.0;
                std::fprintf(stderr, "[breakdown] %d passes %.2f ms/pass "
                             "(bg %.2f, top %.2f, stage %.2f, rail %.2f) | "
                             "blit %.2f ms/s | live %.2f ms/s\n",
                             chromePasses_, chromeMs_ / n, bgMs_ / n, topMs_ / n,
                             stageMs_ / n, railMs_ / n, blitMs_, liveMs_);
                chromeMs_ = blitMs_ = liveMs_ = 0;
                bgMs_ = topMs_ = stageMs_ = railMs_ = 0;
                chromePasses_ = 0; mark = now;
            }
        }

        if (engine_.running()) {
            // Anything that pins its own output needs a nudge now and then.
            const auto now = std::chrono::steady_clock::now();
            if (now - lastSweep_ > std::chrono::seconds(2)) {
                lastSweep_ = now;
                engine_.captureExistingStreams();
                // Tell the watcher whose audio we are carrying, so a player it
                // can see on the bus can be marked as ours.
                std::vector<std::string> apps;
                for (const auto& s : engine_.graph().outputStreams())
                    apps.push_back(s.app);
                nowPlaying_.setCapturedApps(std::move(apps));
            }
            return true;                      // the orbit is moving
        }
        return meterL_ > 0.001 || meterR_ > 0.001;
    };
    window_.onClose = [this] { saveSettings(); };

    window_.run();
    nowPlaying_.stop();
    engine_.stop();
    engine_.shutdown();
    window_.close();
    return true;
}

void App::setStatus(const std::string& text, const Rgb& colour) {
    status_ = text; statusColour_ = colour; staticDirty_ = true;
}

void App::refreshDevices() {
    staticDirty_ = true;
    const std::string keep = device_ >= 0 && device_ < int(sinks_.size())
                           ? sinks_[device_].name : std::string();
    sinks_ = engine_.graph().sinks();
    if (sinks_.empty()) { device_ = 0; return; }
    device_ = 0;
    for (size_t i = 0; i < sinks_.size(); ++i)
        if (sinks_[i].name == keep) { device_ = int(i); break; }
}

void App::startStop() {
    if (engine_.running()) {
        engine_.stop();
        trail_.clear();
        setStatus("Stopped. Audio is back to normal.", ui_.theme.inkFaint);
        return;
    }
    if (sinks_.empty() || device_ >= int(sinks_.size())) {
        setStatus("Pick an output device first.", ui_.theme.motionText);
        return;
    }
    std::string err;
    if (!engine_.start(sinks_[device_].name, kLatencies[latency_].quantum, err)) {
        setStatus("Could not start: " + err, ui_.theme.motionText);
        return;
    }
    lastSweep_ = std::chrono::steady_clock::now();
    setStatus("Running — system audio is routed through 8D and out to "
              + sinks_[device_].label() + ".", ui_.theme.good);
}

void App::applyPreset(int index) {
    int count = 0;
    const Preset* list = presets(count);
    if (index < 0 || index >= count) return;
    const bool wasEnabled = params_.enabled;
    params_ = list[index].p;
    params_.enabled = wasEnabled;
    preset_ = index;
    pushParams();
    setStatus(std::string("Preset applied: ") + list[index].name, ui_.theme.accentText);
}

void App::resetSettings() {
    const bool wasEnabled = params_.enabled;
    params_ = Params();
    params_.enabled = wasEnabled;
    bypass_ = !wasEnabled;
    preset_ = -1;
    pushParams();
    setStatus("Reset — every effect setting is back to its default.",
              ui_.theme.accentText);
}

// --- drawing -----------------------------------------------------------------

void App::drawChrome(cairo_t* cr, int w, int h) {
    ui_.useCr(cr);
    ui_.beginHitTest();
    auto T = [] { return std::chrono::steady_clock::now(); };
    auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count(); };

    auto t0 = T();
    ui_.fillRect({0, 0, double(w), double(h)}, ui_.theme.ground);
    auto t1 = T(); bgMs_ += ms(t0, t1);

    const Rect top{0, 0, double(w), kTopBar};
    const Rect rail{double(w) - kRailWidth, kTopBar,
                    kRailWidth, double(h) - kTopBar};
    const Rect stage{0, kTopBar, double(w) - kRailWidth, double(h) - kTopBar - 42};

    drawTopBar(top);
    auto t2 = T(); topMs_ += ms(t1, t2);
    drawStage(stage);            // records `dynamic_` and paints the chips
    auto t3 = T(); stageMs_ += ms(t2, t3);
    drawRail(rail);
    auto t4 = T(); railMs_ += ms(t3, t4);

    const Rect strip{kPad, double(h) - 38, double(w) - kRailWidth - kPad * 2, 24};
    ui_.font(12.5);
    ui_.text(strip.x, strip.y + strip.h * 0.5, status_, statusColour_);
}

void App::draw(cairo_t* cr, int w, int h) {
    ui_.theme = dark_ ? Theme::darkTheme() : Theme::light();
    ui_.beginFrame();

    if (!cache_ || cacheW_ != w || cacheH_ != h) {
        if (cacheCr_) cairo_destroy(cacheCr_);
        if (cache_) cairo_surface_destroy(cache_);
        cache_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
        cacheCr_ = cairo_create(cache_);
        cacheW_ = w; cacheH_ = h;
        staticDirty_ = true;
    }

    // Input is only ever handled during a full pass, so a frame that merely
    // advances the orbit cannot swallow a click.  Hover lives in the chrome
    // too, but only a pointer crossing from one control to another changes
    // anything -- sweeping across a control's interior does not.
    const int hovered = ui_.hitIndexAt(ui_.mouseX, ui_.mouseY);
    const bool hoverChanged = hovered != hoveredIndex_;
    const bool full = staticDirty_ || hoverChanged || ui_.mousePressed ||
                      ui_.mouseReleased || ui_.wheel != 0 || ui_.mouseDown;
    if (full) {
        // Widget handlers run inside drawChrome and change state as they go --
        // a preset applied, a menu picked.  Clear the flag *before* the pass so
        // anything they set survives it, and book another pass to paint the
        // result; otherwise the click is acted on but never drawn, and the
        // control only catches up on the next unrelated repaint.
        staticDirty_ = false;
        hoveredIndex_ = hovered;
        const auto t0 = std::chrono::steady_clock::now();
        drawChrome(cacheCr_, w, h);
        cairo_surface_flush(cache_);
        chromeMs_ += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        ++chromePasses_;
        if (staticDirty_) window_.requestRedraw();
    }

    ui_.useCr(cr);
    const auto tb = std::chrono::steady_clock::now();
    if (full || dynamic_.w <= 0) {
        cairo_set_source_surface(cr, cache_, 0, 0);
        cairo_paint(cr);
        window_.damageAll();
    } else {   // only the orbit strip changed
        // Only the orbit strip changed: restore that patch from the cache and
        // repaint it, then tell the window to push just those pixels.
        cairo_save(cr);
        cairo_rectangle(cr, dynamic_.x, dynamic_.y, dynamic_.w, dynamic_.h);
        cairo_clip(cr);
        cairo_set_source_surface(cr, cache_, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        window_.setDamage(dynamic_.x, dynamic_.y, dynamic_.w, dynamic_.h);
    }
    blitMs_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tb).count();

    const auto tl = std::chrono::steady_clock::now();
    if (dynamic_.w > 0) {
        cairo_save(cr);
        cairo_rectangle(cr, dynamic_.x, dynamic_.y, dynamic_.w, dynamic_.h);
        cairo_clip(cr);
        drawOrbitLive(orbitRect_);
        drawReadout(orbitRect_);
        drawMeters({orbitRect_.x, orbitRect_.y + orbitRect_.h + 36, orbitRect_.w, 22});
        cairo_restore(cr);
    }
    liveMs_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tl).count();

    ui_.endFrame();
}

void App::drawTopBar(const Rect& r) {
    ui_.fillRect(r, ui_.theme.chrome);
    ui_.fillRect({r.x, r.y + r.h - 1, r.w, 1}, ui_.theme.line);

    // wordmark
    ui_.font(15, true);
    ui_.tracked(kPad, r.h * 0.5, "8D MUSIC", ui_.theme.ink, 2.4);

    // theme switch
    const Rect seg{r.w - kPad - 132, r.h * 0.5 - 15, 132, 30};
    const int pick = ui_.segmented(900, seg, {"Light", "Dark"}, dark_ ? 1 : 0);
    if (pick >= 0) { dark_ = (pick == 1); staticDirty_ = true; }

}

void App::drawStage(const Rect& r) {
    const double side = std::min(r.w - kPad * 2, r.h - 210);
    const Rect orbit{r.x + (r.w - side) * 0.5, r.y + 18, side, side};
    orbitRect_ = orbit;
    // everything that moves lives in this strip
    dynamic_ = {orbit.x - 4, orbit.y - 4, orbit.w + 8, orbit.h + 70};
    drawOrbitStatic(orbit);

    // presets
    // presets
    int count = 0;
    const Preset* list = presets(count);
    const double chipH = 30, gap = 8;
    double x = orbit.x, y = orbit.y + orbit.h + 74;
    ui_.font(13);
    for (int i = 0; i < count; ++i) {
        const double tw = ui_.textWidth(list[i].name) + 26;
        if (x + tw > orbit.x + orbit.w) { x = orbit.x; y += chipH + gap; }
        if (ui_.chip(1000 + i, {x, y, tw, chipH}, list[i].name, preset_ == i))
            applyPreset(i);
        x += tw + gap;
    }
}

void App::drawReadout(const Rect& orbit) {
    const double ang = engine_.processor().angle();
    double deg = std::fmod(ang * 180.0 / M_PI + 180.0, 360.0);
    if (deg < 0) deg += 360.0;
    deg -= 180.0;
    const char* where = deg > 6 ? "right" : (deg < -6 ? "left" : "centre");
    char line[192];
    if (engine_.running())
        std::snprintf(line, sizeof line, "%+.0f°  ·  %.2f m  ·  %s   ·   cpu %.0f%%",
                      deg, engine_.processor().distance(), where,
                      engine_.status().load * 100.0);
    else
        std::snprintf(line, sizeof line, "%+.0f°  ·  %.2f m  ·  %s",
                      deg, engine_.processor().distance(), where);
    ui_.font(13);
    ui_.text(orbit.x, orbit.y + orbit.h + 20, line, ui_.theme.inkSoft);
}

void App::drawOrbitStatic(const Rect& r) {
    cairo_t* cr = ui_.cr;
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
    const double scale = r.w * 0.45;

    // rings
    for (double ring : {0.94, 0.66, 0.38}) {
        ui_.setColour(ui_.theme.lineSoft);
        cairo_set_line_width(cr, 1);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, scale * ring, 0, 2 * M_PI);
        cairo_stroke(cr);
    }
    // cross
    ui_.setColour(ui_.theme.lineSoft);
    cairo_set_line_width(cr, 1);
    cairo_new_path(cr);
    cairo_move_to(cr, cx, r.y); cairo_line_to(cr, cx, r.y + r.h);
    cairo_move_to(cr, r.x, cy); cairo_line_to(cr, r.x + r.w, cy);
    cairo_stroke(cr);

    ui_.font(10, true);
    ui_.tracked(cx - 18, r.y + 8, "FRONT", ui_.theme.inkGhost, 1.4);
    ui_.tracked(cx - 14, r.y + r.h - 8, "BACK", ui_.theme.inkGhost, 1.4);
    ui_.text(r.x + 6, cy, "L", ui_.theme.inkGhost);
    ui_.text(r.x + r.w - 6, cy, "R", ui_.theme.inkGhost, Align::Right);

    // the listener
    const double head = std::max(scale * 0.11, 14.0);
    ui_.setColour(ui_.theme.line);
    cairo_set_line_width(cr, 2);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, head, 0, 2 * M_PI);
    cairo_stroke(cr);
    for (double dx : {-head, head}) {
        cairo_new_path(cr);
        cairo_arc(cr, cx + dx, cy, head * 0.24, 0, 2 * M_PI);
        cairo_stroke(cr);
    }
    ui_.setColour(ui_.theme.inkGhost);
    cairo_new_path(cr);
    cairo_move_to(cr, cx, cy - head - head * 0.42);
    cairo_line_to(cr, cx - head * 0.28, cy - head + head * 0.05);
    cairo_line_to(cr, cx + head * 0.28, cy - head + head * 0.05);
    cairo_close_path(cr);
    cairo_fill(cr);

}

// Everything below moves, so it is never baked into the cached chrome: the
// cache would otherwise keep a stale dot and the new frame would draw over it.
void App::drawOrbitLive(const Rect& r) {
    cairo_t* cr = ui_.cr;
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
    const double scale = r.w * 0.45;
    const bool running = engine_.running();
    const double dashes[2] = {3, 6};

    const double angle = engine_.processor().angle();
    const double dist  = std::clamp(double(engine_.processor().distance()), 0.2, 3.0);
    const double rad   = dist / 3.0 * scale;
    const double sx = cx + std::sin(angle) * rad;
    const double sy = cy - std::cos(angle) * rad;

    // the orbit it is travelling
    ui_.setColour(ui_.theme.accent, 0.5);
    cairo_set_line_width(cr, 1.2);
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_new_path(cr);
    cairo_arc(cr, cx, cy, rad, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);

    trail_.push_back({sx, sy});
    while (trail_.size() > 52) trail_.pop_front();
    const size_t n = trail_.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const double f = double(i) / double(std::max<size_t>(n - 1, 1));
        ui_.setColour(running ? ui_.theme.motion : ui_.theme.inkGhost, f * 0.5);
        cairo_new_path(cr);
        cairo_arc(cr, trail_[i].first, trail_[i].second, 1.0 + f * 2.6, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    ui_.setColour(ui_.theme.accent, 0.16);
    cairo_new_path(cr);
    cairo_arc(cr, sx, sy, 17, 0, 2 * M_PI); cairo_fill(cr);
    ui_.setColour(running ? ui_.theme.accent : ui_.theme.inkGhost);
    cairo_new_path(cr);
    cairo_arc(cr, sx, sy, 7, 0, 2 * M_PI); cairo_fill(cr);

    ui_.setColour(ui_.theme.accent, 0.35);
    cairo_set_line_width(cr, 1);
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_new_path(cr);
    cairo_move_to(cr, cx, cy); cairo_line_to(cr, sx, sy);
    cairo_stroke(cr);
    cairo_set_dash(cr, nullptr, 0, 0);
}

void App::drawMeters(const Rect& r) {
    const double lvlL = engine_.processor().peakL();
    const double lvlR = engine_.processor().peakR();
    meterL_ = std::max(double(lvlL), meterL_ * 0.82);
    meterR_ = std::max(double(lvlR), meterR_ * 0.82);

    const char* names[2] = {"L", "R"};
    const double vals[2] = {meterL_, meterR_};
    for (int i = 0; i < 2; ++i) {
        const Rect row{r.x, r.y + i * 14.0, r.w, 10};
        ui_.font(10, true);
        ui_.text(row.x, row.y + 5, names[i], ui_.theme.inkGhost);
        const Rect track{row.x + 18, row.y + 2.5, row.w - 18, 5};
        ui_.fillRound(track, 2, ui_.theme.lineSoft);
        const double t = std::clamp(vals[i], 0.0, 1.0);
        if (t > 0.002) {
            const Rgb c = t < 0.7 ? ui_.theme.good
                        : (t < 0.9 ? ui_.theme.warn : ui_.theme.motion);
            ui_.fillRound({track.x, track.y, track.w * t, track.h}, 2.5, c);
        }
    }
}

} // namespace eightd

namespace eightd {

// The rail carries every parameter, so it scrolls.  Content is drawn into a
// clip with a scroll offset rather than into its own surface: at this size the
// clip costs nothing and keeps the widget code oblivious to scrolling.
void App::drawRail(const Rect& r) {
    ui_.fillRect(r, ui_.theme.rail);
    ui_.fillRect({r.x, r.y, 1, r.h}, ui_.theme.line);

    if (r.contains(ui_.mouseX, ui_.mouseY) && ui_.wheel != 0) {
        railScroll_ = std::clamp(railScroll_ + ui_.wheel * 54.0, 0.0,
                                 std::max(0.0, railHeight_ - r.h + 30));
        staticDirty_ = true;
    }

    cairo_save(ui_.cr);
    cairo_rectangle(ui_.cr, r.x, r.y, r.w, r.h);
    cairo_clip(ui_.cr);

    const double x = r.x + kPad;
    const double w = r.w - kPad * 2 - 6;
    double y = r.y + 22 - railScroll_;

    y = drawNowPlaying(r, y);
    ui_.fillRect({x, y + 6, w, 1}, ui_.theme.line);
    y += 14;

    auto section = [&](const char* title) {
        y += 14;                       // air above every heading
        ui_.font(11, true);
        ui_.tracked(x, y + 6, title, ui_.theme.accentText, 1.8);
        y += 30;
    };
    auto slider = [&](int id, const char* label, double& value, double lo, double hi,
                      const std::string& readout, const char* hint = "",
                      bool enabled = true) {
        if (ui_.slider(id, {x, y, w, 34}, label, readout, value, lo, hi, hint, enabled)) {
            preset_ = -1;
            pushParams();
        }
        y += (hint && *hint) ? 74 : 56;
    };

    // ---- movement --------------------------------------------------------
    section("MOVEMENT");
    const Rect modeBox{x, y, w * 0.56 - 5, 34};
    const Rect dirBox{x + w * 0.56 + 5, y, w * 0.44 - 5, 34};
    ui_.dropdown(100, modeBox, modeLabel(params_.mode));
    ui_.dropdown(101, dirBox, params_.direction >= 0 ? "Clockwise" : "Counter-cw");
    y += 48;

    double speed = params_.speed;
    slider(102, "Movement speed", speed, 0.01, 1.2, speedText(speed),
           "How fast the source travels around you", params_.mode != Mode::Static);
    params_.speed = float(speed);

    double radius = params_.radius;
    slider(103, "Orbit radius", radius, 0.25, 3.0, metres(radius),
           "Virtual distance — affects level, tone and room");
    params_.radius = float(radius);

    double depth = params_.depth;
    slider(104, "Effect depth", depth, 0.0, 1.0, pct(depth),
           "How far through the stereo field it swings");
    params_.depth = float(depth);

    double smooth = params_.smoothness;
    slider(105, "Smoothness", smooth, 0.0, 1.0, pct(smooth),
           "Rounds off the motion — higher is more gradual");
    params_.smoothness = float(smooth);

    double manual = params_.manualAngle * 180.0 / M_PI;
    slider(106, "Manual position", manual, -180, 180, degrees(manual),
           "Used by the Static position mode", params_.mode == Mode::Static);
    params_.manualAngle = float(manual * M_PI / 180.0);

    if (ui_.checkbox(107, {x, y, w, 24}, "Pause the orbit when nothing plays",
                     reinterpret_cast<bool&>(params_.pauseWhenSilent)))
        pushParams();
    y += 40;

    // ---- character --------------------------------------------------------
    section("CHARACTER");
    const Rect charBox{x, y, w, 34};
    ui_.dropdown(110, charBox, characterLabel(params_.character));
    y += 48;
    double amount = params_.characterAmount;
    slider(111, "Character amount", amount, 0.0, 1.0, pct(amount),
           characterHint(params_.character), params_.character != Character::Clean);
    params_.characterAmount = float(amount);

    // ---- space -------------------------------------------------------------
    section("SPACE");
    double width = params_.width;
    slider(120, "Stereo width", width, 0.0, 2.0, pct(width),
           "Width of the source before it enters the orbit");
    params_.width = float(width);

    const double halfW = w * 0.5 - 8;
    auto pair = [&](int idA, const char* la, double& va, double loa, double hia,
                    const std::string& ra,
                    int idB, const char* lb, double& vb, double lob, double hib,
                    const std::string& rb) {
        if (ui_.slider(idA, {x, y, halfW, 34}, la, ra, va, loa, hia)) { preset_ = -1; pushParams(); }
        if (ui_.slider(idB, {x + w - halfW, y, halfW, 34}, lb, rb, vb, lob, hib)) { preset_ = -1; pushParams(); }
        y += 56;
    };
    double dMix = params_.delayMix, rMix = params_.reverbMix;
    pair(121, "Delay", dMix, 0.0, 1.0, pct(dMix),
         124, "Reverb", rMix, 0.0, 1.0, pct(rMix));
    params_.delayMix = float(dMix); params_.reverbMix = float(rMix);

    double dTime = params_.delayTime, rSize = params_.reverbSize;
    pair(122, "Delay time", dTime, 0.04, 1.2, ms(dTime),
         125, "Room size", rSize, 0.0, 1.0, pct(rSize));
    params_.delayTime = float(dTime); params_.reverbSize = float(rSize);

    double dFb = params_.delayFeedback, rDamp = params_.reverbDamp;
    pair(123, "Delay feedback", dFb, 0.0, 0.85, pct(dFb),
         126, "Damping", rDamp, 0.0, 1.0, pct(rDamp));
    params_.delayFeedback = float(dFb); params_.reverbDamp = float(rDamp);

    // ---- output --------------------------------------------------------------
    section("OUTPUT");
    const Rect devBox{x, y, w - 92, 34};
    ui_.dropdown(130, devBox,
                 sinks_.empty() ? "No output found"
                                : sinks_[std::min<size_t>(device_, sinks_.size() - 1)].label(),
                 !sinks_.empty());
    if (ui_.ghostButton(131, {x + w - 84, y, 84, 34}, "Refresh")) refreshDevices();
    y += 46;

    const Rect latBox{x, y, w - 130, 34};
    ui_.dropdown(132, latBox, kLatencies[latency_].label);
    if (ui_.checkbox(133, {x + w - 120, y, 120, 34}, "Bypass", bypass_)) {
        params_.enabled = !bypass_;
        pushParams();
    }
    y += 48;

    double gain = params_.outputGain;
    slider(134, "Output volume", gain, 0.0, 1.5, pct(gain));
    params_.outputGain = float(gain);

    if (ui_.ghostButton(135, {x, y, w, 36}, "Reset to defaults")) resetSettings();
    y += 52;

    railHeight_ = y + railScroll_ - r.y;
    cairo_restore(ui_.cr);

    // Menus paint last so their lists sit above everything, and outside the
    // clip so a long list is never cut off by the rail.
    std::vector<std::string> modeNames;
    for (int i = 0; i < int(Mode::Count); ++i) modeNames.push_back(modeLabel(Mode(i)));
    int pick = ui_.menuPopup(100, modeBox, modeNames, int(params_.mode));
    if (pick >= 0) { params_.mode = Mode(pick); preset_ = -1; pushParams(); }

    pick = ui_.menuPopup(101, dirBox, {"Clockwise", "Counter-cw"},
                         params_.direction >= 0 ? 0 : 1);
    if (pick >= 0) { params_.direction = pick == 0 ? 1 : -1; pushParams(); }

    std::vector<std::string> charNames;
    for (int i = 0; i < int(Character::Count); ++i)
        charNames.push_back(characterLabel(Character(i)));
    pick = ui_.menuPopup(110, charBox, charNames, int(params_.character));
    if (pick >= 0) { params_.character = Character(pick); preset_ = -1; pushParams(); }

    std::vector<std::string> devNames;
    for (const auto& s : sinks_) devNames.push_back(s.label());
    pick = ui_.menuPopup(130, devBox, devNames, device_);
    if (pick >= 0) {
        device_ = pick;
        staticDirty_ = true;
        if (engine_.running() && engine_.retarget(sinks_[device_].name))
            setStatus("Output moved to " + sinks_[device_].label() + ".",
                      ui_.theme.good);
    }

    std::vector<std::string> latNames;
    for (const auto& l : kLatencies) latNames.push_back(l.label);
    pick = ui_.menuPopup(132, latBox, latNames, latency_);
    if (pick >= 0) {
        latency_ = pick;
        staticDirty_ = true;
        if (engine_.running())
            setStatus("Latency applies the next time you press Start.",
                      ui_.theme.inkFaint);
    }
}

// --- settings ------------------------------------------------------------------

void App::loadSettings() {
    const auto kv = readConfig();
    auto num = [&](const char* key, double fallback) {
        const auto it = kv.find(key);
        return it == kv.end() ? fallback : std::strtod(it->second.c_str(), nullptr);
    };
    params_.mode        = Mode(std::clamp(int(num("mode", 0)), 0, int(Mode::Count) - 1));
    params_.character   = Character(std::clamp(int(num("character", 0)), 0,
                                               int(Character::Count) - 1));
    params_.speed       = float(num("speed", params_.speed));
    params_.radius      = float(num("radius", params_.radius));
    params_.depth       = float(num("depth", params_.depth));
    params_.smoothness  = float(num("smoothness", params_.smoothness));
    params_.width       = float(num("width", params_.width));
    params_.direction   = num("direction", 1) >= 0 ? 1 : -1;
    params_.manualAngle = float(num("manualAngle", 0));
    params_.pauseWhenSilent = num("pauseWhenSilent", 1) != 0;
    params_.characterAmount = float(num("characterAmount", params_.characterAmount));
    params_.delayMix      = float(num("delayMix", params_.delayMix));
    params_.delayTime     = float(num("delayTime", params_.delayTime));
    params_.delayFeedback = float(num("delayFeedback", params_.delayFeedback));
    params_.reverbMix     = float(num("reverbMix", params_.reverbMix));
    params_.reverbSize    = float(num("reverbSize", params_.reverbSize));
    params_.reverbDamp    = float(num("reverbDamp", params_.reverbDamp));
    params_.outputGain    = float(num("outputGain", params_.outputGain));
    latency_ = std::clamp(int(num("latency", kDefaultLatency)), 0,
                          int(sizeof(kLatencies) / sizeof(kLatencies[0])) - 1);
    dark_ = num("dark", 0) != 0;
    const auto it = kv.find("device");
    if (it != kv.end()) {
        sinks_ = engine_.graph().sinks();
        for (size_t i = 0; i < sinks_.size(); ++i)
            if (sinks_[i].name == it->second) device_ = int(i);
    }
}

void App::saveSettings() {
    std::map<std::string, std::string> kv;
    auto put = [&](const char* k, double v) { kv[k] = fmt("%.6g", v); };
    put("mode", double(int(params_.mode)));
    put("character", double(int(params_.character)));
    put("speed", params_.speed);
    put("radius", params_.radius);
    put("depth", params_.depth);
    put("smoothness", params_.smoothness);
    put("width", params_.width);
    put("direction", params_.direction);
    put("manualAngle", params_.manualAngle);
    put("pauseWhenSilent", params_.pauseWhenSilent ? 1 : 0);
    put("characterAmount", params_.characterAmount);
    put("delayMix", params_.delayMix);
    put("delayTime", params_.delayTime);
    put("delayFeedback", params_.delayFeedback);
    put("reverbMix", params_.reverbMix);
    put("reverbSize", params_.reverbSize);
    put("reverbDamp", params_.reverbDamp);
    put("outputGain", params_.outputGain);
    put("latency", latency_);
    put("dark", dark_ ? 1 : 0);
    if (device_ >= 0 && device_ < int(sinks_.size())) kv["device"] = sinks_[device_].name;
    writeConfig(kv);
}

} // namespace eightd

namespace eightd {

namespace {

std::string clock_(double seconds) {
    if (seconds < 0 || seconds > 60 * 60 * 24) return "--:--";
    const long total = long(seconds);
    char buf[32];
    if (total >= 3600)
        std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld",
                      total / 3600, (total / 60) % 60, total % 60);
    else
        std::snprintf(buf, sizeof buf, "%ld:%02ld", total / 60, total % 60);
    return buf;
}

// Trims to fit, dropping whole UTF-8 characters.  Handing cairo a string cut
// through a multi-byte character puts the context into a permanent error state
// and everything drawn after it is silently discarded.
std::string fitText(const Ui& ui, std::string s, double limit) {
    if (ui.textWidth(s) <= limit) return s;
    while (!s.empty()) {
        while (!s.empty()) {                       // drop one character
            const unsigned char c = static_cast<unsigned char>(s.back());
            s.pop_back();
            if ((c & 0xC0) != 0x80) break;         // that was the lead byte
        }
        if (ui.textWidth(s + "…") <= limit) break;
    }
    return s + "…";
}

} // namespace

// The panel at the head of the rail: what is playing, and the transport for it.
double App::drawNowPlaying(const Rect& r, double y) {
    const double x = r.x + kPad;
    const double w = r.w - kPad * 2 - 6;
    const bool have = nowPlaying_.has();
    const Track t = nowPlaying_.track();

    ui_.font(11, true);
    ui_.tracked(x, y + 6, "NOW PLAYING", ui_.theme.accentText, 1.8);
    if (have && !t.player.empty()) {
        ui_.font(12);
        ui_.text(x + w, y + 6, t.player, ui_.theme.inkFaint, Align::Right);
    }
    y += 30;

    // cover mark
    const double art = 62;
    const Rect cover{x, y, art, art};
    ui_.fillRound(cover, 6, have ? ui_.theme.ink : ui_.theme.field);
    if (!have) ui_.strokeRound(cover, 6, ui_.theme.line);
    ui_.font(21, false, true);
    ui_.text(cover.x + art * 0.5, cover.y + art * 0.5, have ? t.initials() : "—",
             have ? ui_.theme.chrome : ui_.theme.inkGhost, Align::Centre);

    const double tx = x + art + 16;
    const double tw = w - art - 16;
    ui_.font(17, false, true);
    ui_.text(tx, y + 15, have ? fitText(ui_, t.title, tw) : "Nothing playing",
             have ? ui_.theme.ink : ui_.theme.inkFaint);
    ui_.font(13.5);
    ui_.text(tx, y + 38, have ? fitText(ui_, t.artistLine(), tw)
                              : "Start a player and it appears here",
             ui_.theme.inkSoft);

    // progress
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double pos = have ? t.at(now) : 0.0;
    const double len = have ? t.length : 0.0;
    ui_.font(12);
    const std::string left = have ? clock_(pos) : "--:--";
    const std::string right = (have && len > 0) ? clock_(len) : "--:--";
    const double lw = ui_.textWidth(left) + 8, rw = ui_.textWidth(right) + 8;
    ui_.text(tx, y + 58, left, ui_.theme.inkFaint);
    ui_.text(x + w, y + 58, right, ui_.theme.inkFaint, Align::Right);
    const Rect bar{tx + lw, y + 55, tw - lw - rw, 5};
    if (bar.w > 10) {
        ui_.fillRound(bar, 2.5, ui_.theme.lineSoft);
        if (len > 0) {
            const double frac = std::clamp(pos / len, 0.0, 1.0);
            if (frac > 0.001)
                ui_.fillRound({bar.x, bar.y, bar.w * frac, bar.h}, 2.5, ui_.theme.motion);
            cairo_new_path(ui_.cr);
            ui_.setColour(ui_.theme.motion);
            cairo_arc(ui_.cr, bar.x + bar.w * frac, bar.y + 2.5, 5, 0, 2 * M_PI);
            cairo_fill(ui_.cr);
        }
    }
    y += art + 18;

    // transport, then the Start button
    const double bs = 38;
    const bool live = have && !t.bus.empty();
    auto icon = [&](int id, double bx, int kind, bool enabled) {
        const Rect b{bx, y, bs, bs};
        ui_.noteHit(b);
        const bool over = enabled && b.contains(ui_.mouseX, ui_.mouseY);
        if (over) ui_.fillRound(b, 8, ui_.theme.accentSoft);
        const Rgb c = enabled ? (over ? ui_.theme.accentText : ui_.theme.inkSoft)
                              : ui_.theme.inkGhost;
        cairo_t* cr = ui_.cr;
        const double cx = b.x + bs * 0.5, cy = b.y + bs * 0.5;
        ui_.setColour(c);
        cairo_new_path(cr);
        if (kind == 1 || kind == 3) {                 // previous / next
            // d points the way the triangle travels: left for previous.
            const double d = kind == 1 ? -1 : 1;
            cairo_move_to(cr, cx - d * 4.0, cy - 6.5);
            cairo_line_to(cr, cx - d * 4.0, cy + 6.5);
            cairo_line_to(cr, cx + d * 6.0, cy);
            cairo_close_path(cr);
            cairo_fill(cr);
            cairo_rectangle(cr, cx + d * 6.0, cy - 6.5, d * 2.2, 13);
            cairo_fill(cr);
        } else if (kind == 2) {                        // pause
            cairo_rectangle(cr, cx - 5.5, cy - 6.5, 3.5, 13);
            cairo_rectangle(cr, cx + 2.0, cy - 6.5, 3.5, 13);
            cairo_fill(cr);
        } else {                                       // play
            cairo_move_to(cr, cx - 4.5, cy - 6.5);
            cairo_line_to(cr, cx - 4.5, cy + 6.5);
            cairo_line_to(cr, cx + 6.5, cy);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
        bool clicked = false;
        if (over && ui_.mousePressed) ui_.active = id;
        if (over && ui_.mouseReleased && ui_.active == id) clicked = true;
        return clicked;
    };

    if (icon(200, x, 1, live && t.canPrev)) nowPlaying_.previous();
    if (icon(201, x + bs + 6, t.playing ? 2 : 0, live)) nowPlaying_.playPause();
    if (icon(202, x + (bs + 6) * 2, 3, live && t.canNext)) nowPlaying_.next();

    const double startW = 132;
    const Rect start{x + w - startW, y, startW, bs};
    const bool running = engine_.running();
    if (ui_.button(203, start, running ? "Stop" : "Start",
                   running ? ui_.theme.motion : ui_.theme.accent,
                   running ? ui_.theme.onMotion : ui_.theme.onAccent,
                   !sinks_.empty(), 8))
        startStop();

    return y + bs + 10;
}

} // namespace eightd
