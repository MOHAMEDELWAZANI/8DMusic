// The desktop interface.
#pragma once
#include "Window.h"
#include "Widgets.h"
#include "Config.h"
#include "../audio/Engine.h"
#include "../audio/NowPlaying.h"
#include <deque>
#include <chrono>

namespace eightd {

class App {
public:
    bool run(std::string& error);

private:
    void draw(cairo_t* cr, int w, int h);
    void drawChrome(cairo_t* cr, int w, int h);   // everything that sits still
    void invalidate() { staticDirty_ = true; }
    void drawTopBar(const Rect& r);
    void drawStage(const Rect& r);
    void drawRail(const Rect& r);
    double drawNowPlaying(const Rect& r, double y);   // returns the new cursor
    void drawOrbitStatic(const Rect& r);   // rings, labels, the listener
    void drawOrbitLive(const Rect& r);     // the orbit, trail and source
    void drawReadout(const Rect& orbit);
    void drawMeters(const Rect& r);

    void startStop();
    void applyPreset(int index);
    void resetSettings();
    void refreshDevices();
    // Any state change also invalidates the cached chrome: its readouts,
    // button labels and status line are all rendered from this state.
    void pushParams() { engine_.setParams(params_); staticDirty_ = true; }
    void setStatus(const std::string& text, const Rgb& colour);
    void loadSettings();
    void saveSettings();

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
    Rect dynamic_{}, orbitRect_{};

    Engine engine_;
    NowPlaying nowPlaying_;
    Params params_;

    std::vector<SinkInfo> sinks_;
    int device_ = 0;
    int latency_ = kDefaultLatency;
    int preset_ = -1;
    bool dark_ = false;
    bool bypass_ = false;

    double railScroll_ = 0, railHeight_ = 0;
    std::deque<std::pair<double, double>> trail_;
    double meterL_ = 0, meterR_ = 0;

    long shownSecond_ = -1;      // so the progress bar repaints once a second
    std::chrono::steady_clock::time_point lastCaptureScan_{};
    std::string status_ = "Ready.";
    Rgb statusColour_{};
    std::chrono::steady_clock::time_point lastSweep_{};
    bool sinksDirty_ = true;
};

} // namespace eightd
