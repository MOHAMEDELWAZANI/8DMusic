// A double-buffered X11 window with a cairo surface.
//
// Rendering goes to an image surface and is blitted in one operation, so the
// window never tears and a redraw costs one memcpy on the X side.
#pragma once
#include <X11/Xlib.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <functional>
#include <string>
#include <chrono>

namespace eightd {

struct MouseEvent { int x = 0, y = 0; int button = 0; bool pressed = false; };
struct KeyEvent { unsigned long keysym = 0; bool ctrl = false, shift = false; };

class Window {
public:
    bool open(const char* title, int width, int height, std::string& error);
    void close();

    // Runs until the window is closed.  `draw` paints one frame; `frameMs` sets
    // how often a redraw is offered when something is animating.
    void run();
    void requestRedraw() { dirty_ = true; }
    void quit() { running_ = false; }

    int width()  const { return width_; }
    int height() const { return height_; }
    cairo_t* cr() const { return cr_; }
    Display* display() const { return dpy_; }
    ::Window handle() const { return win_; }

    std::function<void(cairo_t*, int, int)> onDraw;
    std::function<void(const MouseEvent&)>  onMouse;
    std::function<void(int, int)>           onMotion;
    std::function<void(const KeyEvent&)>    onKey;
    std::function<void(int, int)>           onResize;
    std::function<bool()>                   onIdle;   // true if a redraw is due
    std::function<void()>                   onClose;

    // Milliseconds between idle wake-ups.
    int frameMs = 33;

    // Region the last frame actually touched.  Pushing the whole window to the
    // X server every frame is the single most expensive thing this UI can do,
    // so the app narrows this to the part that moved.
    void setDamage(double x, double y, double w, double h) {
        dmgX_ = x; dmgY_ = y; dmgW_ = w; dmgH_ = h;
    }
    void damageAll() { dmgX_ = dmgY_ = 0; dmgW_ = width_; dmgH_ = height_; }

private:
    void makeSurface();
    void dropSurface();

    Display* dpy_ = nullptr;
    ::Window win_ = 0;
    Visual* visual_ = nullptr;
    Atom wmDelete_ = 0;
    cairo_surface_t* xlib_ = nullptr;
    cairo_surface_t* image_ = nullptr;
    cairo_t* cr_ = nullptr;
    cairo_t* blit_ = nullptr;
    int width_ = 0, height_ = 0;
    bool running_ = false, dirty_ = true;
    double dmgX_ = 0, dmgY_ = 0, dmgW_ = 0, dmgH_ = 0;
    std::chrono::steady_clock::time_point lastFrame_{};
};

} // namespace eightd
