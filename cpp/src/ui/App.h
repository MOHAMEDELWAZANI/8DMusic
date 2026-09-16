// The desktop interface.
//
// Three pages -- Studio, About, Account -- behind one title bar, plus the two
// presentations a first-time user sees.  Studio is the whole instrument on one
// screen: the orbit on the left, every control on the right.
#pragma once
#include "Window.h"
#include "Widgets.h"
#include "Config.h"
#include "../audio/Engine.h"
#include "../audio/NowPlaying.h"
#include <deque>
#include <chrono>

namespace eightd {

enum class Page { Studio, About, Account };

class App {
public:
    bool run(std::string& error);
    // Renders every page and both presentations to PNGs, for comparing the
    // build against the design without a running audio server.
    bool shoot(const std::string& outDir, std::string& error);

private:
    void draw(cairo_t* cr, int w, int h);
    void drawChrome(cairo_t* cr, int w, int h);   // everything that sits still
    void invalidate() { staticDirty_ = true; }

    void drawTopBar(const Rect& r);
    // Minimise and close, plus the drag: every page needs them, because there
    // is no decoration to fall back on.  Returns the left edge of the buttons.
    double drawWindowButtons(const Rect& bar, const std::vector<Rect>& controls);
    void drawSourceMenu();        // painted last, so it sits over the page
    void drawStudio(const Rect& body);
    void drawStage(const Rect& r);
    void drawRail(const Rect& r);
    double drawNowPlaying(const Rect& r);         // returns the bottom edge
    void drawOrbitStatic(const Rect& r);          // rings, labels, the listener
    void drawOrbitLive(const Rect& r);            // the orbit, trail and source
    void drawReadout(const Rect& r);
    void drawMeters(const Rect& r);

    void drawAbout(const Rect& body);
    void drawAccount(const Rect& body);

    void drawWelcome(const Rect& full);
    void drawWelcomeMark(const Rect& art);   // the looping mark on page one
    void drawTour(const Rect& full);
    void drawGuide(const Rect& full);

    void startStop();
    void applyPreset(int index);
    void resetSettings();
    void refreshDevices();
    void setPage(Page p);
    // Any state change also invalidates the cached chrome: its readouts,
    // button labels and status line are all rendered from this state.
    void pushParams() { engine_.setParams(params_); staticDirty_ = true; }
    void setStatus(const std::string& text, const Rgb& colour);
    void loadSettings();
    void saveSettings();
    void openUrl(const std::string& url);

    Window window_;
    Ui ui_;

    // Only the orbit, its readout and the meters actually animate.  The rest of
    // the frame is rendered once into `cache_` and blitted, so a running app
    // repaints a small rectangle each frame instead of the whole window.
    cairo_surface_t* cache_ = nullptr;
    cairo_t* cacheCr_ = nullptr;
    int cacheW_ = 0, cacheH_ = 0;
    bool staticDirty_ = true;
    double chromeMs_ = 0, blitMs_ = 0, liveMs_ = 0;   // EIGHTD_PROFILE breakdown
    double topMs_ = 0, stageMs_ = 0, railMs_ = 0, bgMs_ = 0;
    int chromePasses_ = 0;
    int hoveredIndex_ = -1;      // control under the pointer at the last chrome pass
    Rect dynamic_{}, orbitRect_{}, readoutRect_{}, metersRect_{}, sourcePill_{};

    Engine engine_;
    NowPlaying nowPlaying_;
    Params params_;

    std::vector<SinkInfo> sinks_;
    int device_ = 0;
    int latency_ = kDefaultLatency;
    int preset_ = -1;
    bool dark_ = true;
    bool bypass_ = false;

    Page page_ = Page::Studio;
    int welcome_ = -1;           // page of the welcome flow, or -1
    int tour_ = -1;              // page of the studio tour, or -1
    bool seenWelcome_ = false, seenTour_ = false;
    int guide_ = -1;             // the guide being read, or -1

    std::deque<std::pair<double, double>> trail_;
    double meterL_ = 0, meterR_ = 0;

    long shownSecond_ = -1;      // so the progress bar repaints once a second
    std::string status_;
    Rgb statusColour_{};
    std::chrono::steady_clock::time_point statusAt_{};
    std::chrono::steady_clock::time_point lastSweep_{};
    std::chrono::steady_clock::time_point lastPress_{};
    int pressRootX_ = 0, pressRootY_ = 0;   // where a press landed on the screen

    // The corner the page is cut to, and how many pixels a design unit is.
    // Both come from the window once it is open.
    double frameCorner_ = 16, scale_ = 1.0;
    // In the window the corners are cut away by the shape extension, so what
    // is painted behind them only matters if a window manager ignores that --
    // ground reads as square corners, black reads as a hole.  A rendered file
    // wants them transparent instead, so the desktop shows through.
    bool opaqueCorners_ = false;
    double savedScale_ = 0;      // what the user last chose, if anything
    bool sinksDirty_ = true;
};

} // namespace eightd
