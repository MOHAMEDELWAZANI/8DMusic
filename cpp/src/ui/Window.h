// A double-buffered X11 window with a cairo surface.
//
// Rendering goes to an image surface and is blitted in one operation, so the
// window never tears and a redraw costs one memcpy on the X side.
//
// The window wears no decoration: the app draws its own title bar and its own
// rounded corners.  That means it also has to do the three jobs the window
// manager was doing -- moving the window, minimising it and closing it.  The
// corners are cut out of the window with the shape extension rather than
// painted with alpha, so they look the same whether or not anything is
// compositing the screen.
//
// Everything above this layer is drawn in the design's own units.  The window
// works out how many real pixels that is -- a desktop at 200% gets a window
// twice the size, with the drawing scaled to match rather than stretched.
#pragma once
#include <X11/Xlib.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <functional>
#include <string>
#include <chrono>

namespace eightd {

struct MouseEvent { int x = 0, y = 0, rootX = 0, rootY = 0;
                    int button = 0; bool pressed = false; };
struct KeyEvent { unsigned long keysym = 0; bool ctrl = false, shift = false; };

class Window {
public:
    // `width` and `height` are the page in design units; the window opens at
    // that size times the scale the desktop is running at.
    bool open(const char* title, int width, int height, std::string& error);
    void close();

    double corner() const { return 16.0; }

    // Design units to pixels.  One on an ordinary screen, two on a desktop at
    // 200%, and anything in between a quarter step.
    double scale() const { return scale_; }
    // Whether shift was held at the last event, for fine adjustment.
    bool shiftHeld() const { return shift_; }
    void setScale(double s);
    // The page size in design units, whatever the scale.
    double logicalWidth()  const { return width_ / scale_; }
    double logicalHeight() const { return height_ / scale_; }

    // The jobs the decoration used to do.
    void startDrag(int rootX, int rootY);
    void minimise();
    // Tells the X server which part of the window takes clicks, so the shadow
    // does not, and gives the window its rounded outline when nothing is there
    // to composite the alpha.
    void applyShape();

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
    void applySizeHints();

    Display* dpy_ = nullptr;
    ::Window win_ = 0;
    Visual* visual_ = nullptr;
    Atom wmDelete_ = 0;
    double scale_ = 1.0;
    bool shift_ = false;
    int pageW_ = 0, pageH_ = 0;      // the page, in design units
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
