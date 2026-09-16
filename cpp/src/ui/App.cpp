#include "App.h"
#include "Layout.h"
#include "Fonts.h"
#include <X11/keysym.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/wait.h>
#include <vector>
#include <unistd.h>

namespace eightd {

// --- lifecycle --------------------------------------------------------------

bool App::run(std::string& error) {
    registerBundledFonts();
    if (!engine_.init(error)) return false;
    nowPlaying_.start();
    loadSettings();
    refreshDevices();
    pushParams();
    statusColour_ = Theme::darkTheme().faint;
    if (!seenWelcome_) welcome_ = 0;
    else if (!seenTour_) tour_ = 0;

    // The registry tells us the moment a device appears or disappears; the flag
    // keeps the reaction on the UI thread where the list is actually used.
    engine_.graph().onSinksChanged = [this] { sinksDirty_ = true; };

    if (!window_.open("8D Music", int(kDesignW), int(kDesignH), error)) {
        engine_.shutdown();
        return false;
    }

    frameCorner_ = window_.corner();
    scale_ = window_.scale();
    opaqueCorners_ = true;
    if (savedScale_ > 0 && std::fabs(savedScale_ - scale_) > 0.01) {
        window_.setScale(savedScale_);
        scale_ = window_.scale();
    }

    window_.onDraw   = [this](cairo_t* cr, int w, int h) { draw(cr, w, h); };
    window_.onMotion = [this](int x, int y) {
        ui_.mouseX = x / scale_; ui_.mouseY = y / scale_;
    };
    window_.onMouse  = [this](const MouseEvent& m) {
        staticDirty_ = true;
        if (m.button == 4 || m.button == 5) {           // wheel
            if (m.pressed) ui_.wheel = (m.button == 4) ? -1 : 1;
            return;
        }
        ui_.mouseX = m.x / scale_; ui_.mouseY = m.y / scale_;
        if (m.pressed) {
            pressRootX_ = m.rootX; pressRootY_ = m.rootY;
            const auto now = std::chrono::steady_clock::now();
            ui_.doubleClick =
                now - lastPress_ < std::chrono::milliseconds(380);
            lastPress_ = now;
            ui_.mouseDown = true; ui_.mousePressed = true;
        } else {
            ui_.mouseDown = false; ui_.mouseReleased = true;
        }
    };
    window_.onKey = [this](const KeyEvent& k) {
        staticDirty_ = true;
        if (guide_ >= 0 && welcome_ < 0 && tour_ < 0) {
            if (k.keysym == XK_Escape || k.keysym == XK_Return) guide_ = -1;
            return;
        }
        // A presentation owns the keyboard while it is up.
        if (welcome_ >= 0 || tour_ >= 0) {
            int& page = welcome_ >= 0 ? welcome_ : tour_;
            const int last = welcome_ >= 0 ? 4 : 6;
            if (k.keysym == XK_Escape) {
                if (welcome_ >= 0) { welcome_ = -1; seenWelcome_ = true;
                                     if (!seenTour_) tour_ = 0; }
                else { tour_ = -1; seenTour_ = true; }
            } else if (k.keysym == XK_Right || k.keysym == XK_Return ||
                       k.keysym == XK_space) {
                if (page < last) ++page;
                else if (welcome_ >= 0) { welcome_ = -1; seenWelcome_ = true;
                                          if (!seenTour_) tour_ = 0; }
                else { tour_ = -1; seenTour_ = true; }
            } else if (k.keysym == XK_Left && page > 0) {
                --page;
            }
            return;
        }
        if (k.keysym == XK_space) {
            bypass_ = !bypass_; params_.enabled = !bypass_; pushParams();
        } else if (k.ctrl && (k.keysym == XK_r || k.keysym == XK_R)) {
            if (k.shift) resetSettings(); else startStop();
        } else if (k.ctrl && (k.keysym == XK_t || k.keysym == XK_T)) {
            dark_ = !dark_;
        } else if (k.ctrl && (k.keysym == XK_plus || k.keysym == XK_equal ||
                              k.keysym == XK_minus || k.keysym == XK_underscore)) {
            const bool up = k.keysym == XK_plus || k.keysym == XK_equal;
            window_.setScale(window_.scale() + (up ? 0.25 : -0.25));
            scale_ = window_.scale();
            savedScale_ = scale_;
            setStatus(fmt("Interface at %.0f%%", scale_ * 100), ui_.theme.faint);
        } else if (k.keysym == XK_Escape) {
            if (guide_ >= 0) guide_ = -1; else ui_.openMenu = 0;
        }
        // No digit shortcuts for the pages: XLookupKeysym reads the first level
        // of the keyboard map, and on an AZERTY layout that is not a digit at
        // all.  The tab pills are the way between pages.
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
            return welcome_ == 0 ||
                   (page_ == Page::Studio && welcome_ < 0 && tour_ < 0);
        }
        return welcome_ == 0 ||
               (page_ == Page::Studio && welcome_ < 0 && tour_ < 0 &&
                (meterL_ > 0.001 || meterR_ > 0.001));
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
    status_ = text; statusColour_ = colour;
    statusAt_ = std::chrono::steady_clock::now();
    staticDirty_ = true;
}

void App::setPage(Page p) {
    if (page_ == p) return;
    page_ = p;
    ui_.openMenu = 0;
    dynamic_ = {};
    staticDirty_ = true;
    if (p == Page::Studio && !seenTour_ && welcome_ < 0) tour_ = 0;
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
        setStatus("Stopped. Audio is back to normal.", ui_.theme.faint);
        return;
    }
    if (sinks_.empty() || device_ >= int(sinks_.size())) {
        setStatus("Pick an output device first.", ui_.theme.motion);
        return;
    }
    std::string err;
    if (!engine_.start(sinks_[device_].name, kLatencies[latency_].quantum, err)) {
        setStatus("Could not start: " + err, ui_.theme.motion);
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
    setStatus(std::string("Preset applied: ") + list[index].name, ui_.theme.accent);
}

void App::resetSettings() {
    const bool wasEnabled = params_.enabled;
    params_ = Params();
    params_.enabled = wasEnabled;
    bypass_ = !wasEnabled;
    preset_ = -1;
    pushParams();
    setStatus("Reset — every effect setting is back to its default.",
              ui_.theme.accent);
}

// Handing a URL to the desktop, without a shell in the middle.
void App::openUrl(const std::string& url) {
    const pid_t pid = fork();
    if (pid == 0) {
        setsid();
        ::execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr);
        _exit(127);
    }
    if (pid > 0) {
        // Reaped on the next pass; nothing here waits on a browser starting.
        int st = 0;
        waitpid(pid, &st, WNOHANG);
    }
}

// --- drawing -----------------------------------------------------------------

void App::drawChrome(cairo_t* cr, int w, int h) {
    ui_.useCr(cr);
    ui_.syncTransform();
    ui_.beginHitTest();
    auto T = [] { return std::chrono::steady_clock::now(); };
    auto msOf = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count(); };

    auto t0 = T();

    // The window wears no decoration, so the frame is ours to paint: start
    // from nothing, then lay the page on as a rounded plate.  Everything after
    // this is clipped to that plate.
    if (opaqueCorners_) {
        ui_.fillRect({0, 0, double(w), double(h)}, ui_.theme.ground);
    } else {
        cairo_save(cr);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
        cairo_restore(cr);
    }

    // The interface is drawn at one size and centred.  The window asks not to
    // be resized, but a window manager is free to ignore that, and a page that
    // is centred in whatever it gets is better than one that is stretched.
    const Rect full{std::max(0.0, (double(w) - kDesignW) * 0.5),
                    std::max(0.0, (double(h) - kDesignH) * 0.5),
                    std::min(double(w), kDesignW), std::min(double(h), kDesignH)};

    // No shadow: the corners are cut out of the window itself, so there is
    // nothing behind the app to paint on.
    const double corner = frameCorner_;
    ui_.fillRound(full, corner, ui_.theme.ground);
    cairo_save(cr);
    ui_.roundRect(full, corner);
    cairo_clip(cr);

    // The light this layout stands in: the stage is lit from the middle, with
    // one warm source off to the right of it.
    if (page_ == Page::Studio && welcome_ < 0) {
        // Kept below the title bar: a light that washes the chrome makes the
        // bar look like part of the stage, which it is not.
        cairo_save(cr);
        cairo_rectangle(cr, full.cx() - 490, full.y + 96, 980, 640);
        cairo_clip(cr);
        ui_.glow(full.cx(), full.y + 390, 510, ui_.theme.accent,
                 ui_.theme.dark ? 0.17 : 0.08);
        ui_.glow(full.cx() + 118, full.y + 262, 451, ui_.theme.motion,
                 ui_.theme.dark ? 0.16 : 0.07);
        cairo_restore(cr);
    }
    auto t1 = T(); bgMs_ += msOf(t0, t1);

    if (welcome_ >= 0) {
        drawWelcome(full);
    } else {
        const Rect top{full.x, full.y, full.w, kTopBar};
        const Rect body{full.x, full.y + kTopBar, full.w, full.h - kTopBar};

        // While a presentation or a guide is up it owns the pointer: the page
        // behind it is still painted, but nothing in it can be clicked through.
        const bool overlay = tour_ >= 0 || guide_ >= 0;
        const double keepX = ui_.mouseX, keepY = ui_.mouseY;
        if (overlay) { ui_.mouseX = -1e6; ui_.mouseY = -1e6; }

        drawTopBar(top);
        auto t2 = T(); topMs_ += msOf(t1, t2);

        switch (page_) {
            case Page::Studio:  drawStudio(body); break;
            case Page::About:   drawAbout(body);  break;
            case Page::Account: drawAccount(body); break;
        }
        auto t3 = T(); stageMs_ += msOf(t2, t3);

        drawSourceMenu();

        if (overlay) {
            ui_.mouseX = keepX; ui_.mouseY = keepY;
            dynamic_ = {};             // nothing animates under a dialog
            if (tour_ >= 0) drawTour(full);
            else            drawGuide(full);
        }
    }

    // the hairline that separates the plate from the desktop behind it
    ui_.strokeRound(full, corner, ui_.theme.text, 1, 0.08);
    cairo_restore(cr);
}

void App::draw(cairo_t* cr, int w, int h) {
    ui_.theme = dark_ ? Theme::darkTheme() : Theme::light();
    ui_.shift = window_.shiftHeld();
    ui_.beginFrame();

    const double s = scale_;
    const int lw = int(w / s), lh = int(h / s);

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
        dynamic_ = {};
        const auto t0 = std::chrono::steady_clock::now();
        cairo_save(cacheCr_);
        cairo_scale(cacheCr_, s, s);
        drawChrome(cacheCr_, lw, lh);
        cairo_restore(cacheCr_);
        cairo_surface_flush(cache_);
        chromeMs_ += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        ++chromePasses_;
        if (staticDirty_) window_.requestRedraw();
    }

    ui_.useCr(cr);
    const auto tb = std::chrono::steady_clock::now();
    // The cache is a picture of the window in real pixels, so it is blitted
    // with the transform out of the way.
    cairo_identity_matrix(cr);
    if (full || dynamic_.w <= 0) {
        cairo_set_source_surface(cr, cache_, 0, 0);
        cairo_paint(cr);
        window_.damageAll();
    } else {
        // Only the orbit strip changed: restore that patch from the cache and
        // repaint it, then tell the window to push just those pixels.
        cairo_save(cr);
        cairo_rectangle(cr, dynamic_.x * s, dynamic_.y * s,
                        dynamic_.w * s, dynamic_.h * s);
        cairo_clip(cr);
        cairo_set_source_surface(cr, cache_, 0, 0);
        cairo_paint(cr);
        cairo_restore(cr);
        window_.setDamage(dynamic_.x * s, dynamic_.y * s,
                          dynamic_.w * s, dynamic_.h * s);
    }
    blitMs_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tb).count();

    const auto tl = std::chrono::steady_clock::now();
    if (dynamic_.w > 0) {
        cairo_save(cr);
        cairo_scale(cr, s, s);
        ui_.syncTransform();
        cairo_rectangle(cr, dynamic_.x, dynamic_.y, dynamic_.w, dynamic_.h);
        cairo_clip(cr);
        if (welcome_ == 0) {
            drawWelcomeMark(orbitRect_);
        } else {
            drawOrbitLive(orbitRect_);
            drawReadout(readoutRect_);
            drawMeters(metersRect_);
        }
        cairo_restore(cr);
    }
    liveMs_ += std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - tl).count();

    ui_.endFrame();
}

// --- the title bar --------------------------------------------------------

void App::drawTopBar(const Rect& r) {
    ui_.fillRect({r.x, r.y + r.h - 1, r.w, 1}, ui_.theme.text, 0.05);

    ui_.logo({r.x + 20, r.cy() - 12, 24, 24});
    ui_.font(11, W700);
    ui_.tracked(r.x + 56, r.cy(), "8D MUSIC", ui_.theme.dim, 2.0);

    // the three pages, as pills
    struct TabDef { const char* label; const Icon* ic; Page page; int id; };
    const TabDef tabs[] = {
        {"Studio",  &ico::kStudio,  Page::Studio,  kIdTabStudio},
        {"About",   &ico::kAbout,   Page::About,   kIdTabAbout},
        {"Account", &ico::kAccount, Page::Account, kIdTabAccount},
    };
    double widths[3], total = 8;
    ui_.font(13.5, W600);
    for (int i = 0; i < 3; ++i) {
        widths[i] = ui_.textWidth(tabs[i].label) + 16 + 7 + 36;
        total += widths[i] + (i ? 4 : 0);
    }
    Rect bar{r.cx() - total * 0.5, r.cy() - 21, total, 42};
    ui_.fillRound(bar, kPill, ui_.theme.card);
    double x = bar.x + 4;
    for (int i = 0; i < 3; ++i) {
        const Rect cell{x, bar.y + 4, widths[i], 34};
        const bool on = page_ == tabs[i].page;
        if (ui_.click(tabs[i].id, cell)) setPage(tabs[i].page);
        if (on) ui_.fillRound(cell, kPill, ui_.theme.raised);
        else if (ui_.over(cell)) ui_.fillRound(cell, kPill, ui_.theme.well);
        const Rgb c = on ? ui_.theme.text : ui_.theme.faint;
        ui_.icon(*tabs[i].ic, {cell.x + 16, cell.cy() - 8, 16, 16}, c);
        ui_.font(13.5, W600);
        ui_.text(cell.x + 16 + 16 + 7, cell.cy(), tabs[i].label, c);
        x += widths[i] + 4;
    }

    // what we are carrying, and the master switch
    const Track t = nowPlaying_.track();
    const bool running = engine_.running();
    const std::string source = running ? "System audio" : "Not capturing";
    std::string detail = nowPlaying_.has() && !t.player.empty()
                       ? t.player
                       : (sinks_.empty() ? "no output"
                          : sinks_[std::min<size_t>(device_, sinks_.size() - 1)].label());
    ui_.font(13, W400);
    const double dw = ui_.textWidth(detail);
    ui_.font(13, W600);
    const double sw = ui_.textWidth(source);
    const double pillW = 14 + 8 + 8 + sw + 8 + 6 + 8 + dw + 14;

    // The window's own buttons sit at the trailing edge, where this desktop
    // puts them.  There is no maximise: the window is one size.
    const double buttonsX = r.x + r.w - 12 - 36 - 2 - 36;
    const Rect swBox{buttonsX - 16 - 44, r.cy() - 13, 44, 26};
    ui_.font(12, W700);
    const double lw = ui_.textWidth("8D") + 5;
    const Rect pillR{swBox.x - lw - 14 - pillW, r.cy() - 17, pillW, 34};

    // The pill is its own control: it says what we are carrying, and opens the
    // list of outputs when clicked.
    ui_.noteHit(pillR);
    const bool overPill = pillR.contains(ui_.mouseX, ui_.mouseY);
    if (overPill && ui_.mousePressed && !sinks_.empty())
        ui_.openMenu = (ui_.openMenu == kIdSource) ? 0 : kIdSource;
    ui_.fillRound(pillR, kPill,
                  overPill ? mix(ui_.theme.card, ui_.theme.text, 0.07)
                           : ui_.theme.card);
    const double ledX = pillR.x + 14 + 4;
    if (running) ui_.glow(ledX, pillR.cy(), 11, ui_.theme.deep, 0.5);
    ui_.circle(ledX, pillR.cy(), 4, running ? ui_.theme.deep : ui_.theme.ghost);
    ui_.font(13, W600);
    ui_.text(pillR.x + 14 + 8 + 8, pillR.cy(), source, ui_.theme.text);
    ui_.font(13, W400);
    ui_.text(pillR.x + 14 + 8 + 8 + sw + 8, pillR.cy(), "·", ui_.theme.faint);
    ui_.text(pillR.x + 14 + 8 + 8 + sw + 8 + 6 + 8, pillR.cy(), detail, ui_.theme.dim);

    ui_.font(12, W700);
    ui_.tracked(swBox.x - lw - 9, r.cy(), "8D", ui_.theme.deep, 1.2);
    if (ui_.switchPill(kIdSwitch, swBox, !bypass_)) {
        bypass_ = !bypass_; params_.enabled = !bypass_; pushParams();
    }

    drawWindowButtons(r, {pillR, swBox, bar});
    sourcePill_ = pillR;
}

double App::drawWindowButtons(const Rect& bar, const std::vector<Rect>& controls) {
    const double btn = 36;
    const Rect closeBtn{bar.x + bar.w - 12 - btn, bar.y + (bar.h - btn) * 0.5, btn, btn};
    const Rect minBtn{closeBtn.x - 2 - btn, closeBtn.y, btn, btn};
    cairo_t* cr = ui_.cr;

    if (ui_.click(kIdMinimise, minBtn)) window_.minimise();
    if (ui_.over(minBtn)) ui_.fillRound(minBtn, 12, ui_.theme.well);
    ui_.setColour(ui_.over(minBtn) ? ui_.theme.text : ui_.theme.faint);
    cairo_set_line_width(cr, 1.8);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    cairo_new_path(cr);
    cairo_move_to(cr, minBtn.cx() - 7, minBtn.cy());
    cairo_line_to(cr, minBtn.cx() + 7, minBtn.cy());
    cairo_stroke(cr);

    const bool overClose = ui_.over(closeBtn);
    if (ui_.click(kIdClose, closeBtn)) { saveSettings(); window_.quit(); }
    if (overClose) ui_.fillRound(closeBtn, 12, Rgb::hex(0xE5484D));
    ui_.setColour(overClose ? Rgb::hex(0xFFFFFF) : ui_.theme.faint);
    cairo_new_path(cr);
    cairo_move_to(cr, closeBtn.cx() - 6, closeBtn.cy() - 6);
    cairo_line_to(cr, closeBtn.cx() + 6, closeBtn.cy() + 6);
    cairo_move_to(cr, closeBtn.cx() + 6, closeBtn.cy() - 6);
    cairo_line_to(cr, closeBtn.cx() - 6, closeBtn.cy() + 6);
    cairo_stroke(cr);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

    // Anywhere else along the bar is a handle: press it and the window moves.
    // The window manager does the dragging -- it knows about edges, monitors
    // and workspaces, and we do not.
    if (ui_.mousePressed && bar.contains(ui_.mouseX, ui_.mouseY) &&
        !minBtn.contains(ui_.mouseX, ui_.mouseY) &&
        !closeBtn.contains(ui_.mouseX, ui_.mouseY)) {
        bool onControl = false;
        for (const Rect& c : controls)
            if (c.contains(ui_.mouseX, ui_.mouseY)) { onControl = true; break; }
        if (!onControl) {
            window_.startDrag(pressRootX_, pressRootY_);
            // The window manager owns the pointer from here, so the release
            // never arrives; end the press ourselves or every control stays
            // armed.
            ui_.mouseDown = false;
            ui_.active = 0;
        }
    }
    return minBtn.x;
}

// The output list drops out of the title bar into the page, so it is painted
// after the page rather than with the bar that owns it.
void App::drawSourceMenu() {
    if (ui_.openMenu != kIdSource || sourcePill_.w <= 0) return;
    std::vector<std::string> devNames;
    for (const auto& s : sinks_) devNames.push_back(s.label());
    const int pick = ui_.menuPopup(kIdSource, sourcePill_, devNames, device_, 300);
    if (pick >= 0) {
        device_ = pick;
        staticDirty_ = true;
        if (engine_.running() && engine_.retarget(sinks_[device_].name))
            setStatus("Output moved to " + sinks_[device_].label() + ".",
                      ui_.theme.good);
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
    params_.eqBass        = float(num("eqBass", params_.eqBass));
    params_.eqMid         = float(num("eqMid", params_.eqMid));
    params_.eqTreble      = float(num("eqTreble", params_.eqTreble));
    latency_ = std::clamp(int(num("latency", kDefaultLatency)), 0,
                          int(sizeof(kLatencies) / sizeof(kLatencies[0])) - 1);
    dark_ = num("dark", 1) != 0;
    savedScale_ = num("scale", 0);
    seenWelcome_ = num("seenWelcome", 0) != 0;
    seenTour_    = num("seenTour", 0) != 0;
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
    put("eqBass", params_.eqBass);
    put("eqMid", params_.eqMid);
    put("eqTreble", params_.eqTreble);
    put("latency", latency_);
    put("dark", dark_ ? 1 : 0);
    if (savedScale_ > 0) put("scale", savedScale_);
    put("seenWelcome", seenWelcome_ ? 1 : 0);
    put("seenTour", seenTour_ ? 1 : 0);
    if (device_ >= 0 && device_ < int(sinks_.size())) kv["device"] = sinks_[device_].name;
    writeConfig(kv);
}

// --- rendering the design to files ------------------------------------------

bool App::shoot(const std::string& outDir, std::string& error) {
    registerBundledFonts();
    // Rendered the way the window actually is: a page with its corners cut
    // out, sitting on a desktop.
    frameCorner_ = 16;
    const int w = int(kDesignW), h = int(kDesignH);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(surf);

    // something to sit on, so the shadow has a job
    cairo_surface_t* desk = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t* dcr = cairo_create(desk);
    cairo_pattern_t* bg = cairo_pattern_create_radial(w * 0.26, h * 0.18, 0,
                                                      w * 0.26, h * 0.18, w * 0.9);
    cairo_pattern_add_color_stop_rgb(bg, 0, 0.114, 0.137, 0.157);
    cairo_pattern_add_color_stop_rgb(bg, 1, 0.047, 0.043, 0.043);
    cairo_set_source(dcr, bg);
    cairo_paint(dcr);
    cairo_pattern_destroy(bg);
    ui_.theme = dark_ ? Theme::darkTheme() : Theme::light();

    // Enough state to make the pages look like the design rather than like a
    // cold start: a preset chosen, a track, the engine nominally live.
    applyPreset(1);
    preset_ = 1;
    status_.clear();

    auto shot = [&](const char* name) {
        ui_.beginFrame();
        drawChrome(cr, w, h);
        cairo_surface_flush(surf);
        // The orbit and the welcome mark are painted live, over the cached
        // chrome; the shot has to do the same or the page looks half drawn.
        if (dynamic_.w > 0) {
            if (welcome_ == 0) {
                drawWelcomeMark(orbitRect_);
            } else if (page_ == Page::Studio && welcome_ < 0) {
                drawOrbitLive(orbitRect_);
                drawReadout(readoutRect_);
                drawMeters(metersRect_);
                if (tour_ >= 0) drawTour({0, 0, double(w), double(h)});
            }
        }
        cairo_surface_flush(surf);
        // lay the window on the desktop and write that
        cairo_surface_t* out = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
        cairo_t* ocr = cairo_create(out);
        cairo_set_source_surface(ocr, desk, 0, 0);
        cairo_paint(ocr);
        cairo_set_source_surface(ocr, surf, 0, 0);
        cairo_paint(ocr);
        cairo_surface_flush(out);
        const std::string path = outDir + "/" + name + ".png";
        cairo_surface_write_to_png(out, path.c_str());
        cairo_destroy(ocr);
        cairo_surface_destroy(out);
        ui_.endFrame();
    };

    welcome_ = -1; tour_ = -1;
    page_ = Page::Studio;  shot("studio");
    page_ = Page::About;   shot("about");
    page_ = Page::Account; shot("account");
    page_ = Page::Studio;
    for (int i = 0; i < 5; ++i) { welcome_ = i; shot(("welcome" + std::to_string(i + 1)).c_str()); }
    welcome_ = -1;
    for (int i = 0; i < 7; ++i) { tour_ = i; shot(("tour" + std::to_string(i + 1)).c_str()); }
    tour_ = -1;
    page_ = Page::About;
    for (int i = 0; i < 4; ++i) { guide_ = i; shot(("guide" + std::to_string(i + 1)).c_str()); }
    guide_ = -1;

    // and the same page in daylight
    ui_.theme = Theme::light();
    dark_ = false;
    page_ = Page::Studio; shot("studio-light");
    page_ = Page::About;  shot("about-light");

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
    cairo_destroy(dcr);
    cairo_surface_destroy(desk);
    (void)error;
    return true;
}

} // namespace eightd
