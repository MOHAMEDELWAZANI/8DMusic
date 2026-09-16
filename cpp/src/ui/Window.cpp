#include "Window.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/extensions/shape.h>
#include <sys/select.h>
#include <cstring>
#include <ctime>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

namespace eightd {

// How big a design pixel is on this desktop.
//
// Xft.dpi in the resource database is how a desktop tells X clients what scale
// it is running at -- 192 for 200%, 144 for 150%.  Everything else is a guess
// from the size of the screen, which is better than assuming 100% on a 4K
// panel and drawing an interface nobody can read.
static double detectScale(Display* dpy, int screen, int pageW, int pageH) {
    double s = 0;
    if (const char* env = std::getenv("EIGHTD_SCALE")) {
        s = std::atof(env);
    } else {
        if (char* rm = XResourceManagerString(dpy)) {
            XrmDatabase db = XrmGetStringDatabase(rm);
            if (db) {
                char* type = nullptr;
                XrmValue v{};
                if (XrmGetResource(db, "Xft.dpi", "Xft.Dpi", &type, &v) && v.addr)
                    s = std::atof(v.addr) / 96.0;
                XrmDestroyDatabase(db);
            }
        }
        if (s <= 0) {                       // nothing said: judge by the screen
            const int h = DisplayHeight(dpy, screen);
            s = h >= 2000 ? 2.0 : (h >= 1400 ? 1.5 : 1.0);
        }
        s = std::round(s * 4) / 4;          // quarter steps, like every desktop
    }
    s = std::clamp(s, 1.0, 3.0);

    // and never larger than the screen it has to live on
    const double maxW = (DisplayWidth(dpy, screen) - 80.0) / pageW;
    const double maxH = (DisplayHeight(dpy, screen) - 120.0) / pageH;
    while (s > 1.0 && (s > maxW || s > maxH)) s -= 0.25;
    return std::max(s, 1.0);
}

bool Window::open(const char* title, int width, int height, std::string& error) {
    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) { error = "cannot open the X display"; return false; }
    const int screen = DefaultScreen(dpy_);
    visual_ = DefaultVisual(dpy_, screen);

    pageW_ = width; pageH_ = height;
    scale_ = detectScale(dpy_, screen, width, height);
    width_  = int(width * scale_);
    height_ = int(height * scale_);

    win_ = XCreateSimpleWindow(dpy_, RootWindow(dpy_, screen), 0, 0,
                               (unsigned)width_, (unsigned)height_, 0,
                               BlackPixel(dpy_, screen), BlackPixel(dpy_, screen));
    XStoreName(dpy_, win_, title);

    // No decoration: the app draws the title bar itself.  Motif's old hint is
    // still what every window manager reads for this.
    struct MotifHints { unsigned long flags, functions, decorations; long input; unsigned long status; };
    const MotifHints hints{2 /* MWM_HINTS_DECORATIONS */, 0, 0, 0, 0};
    const Atom motif = XInternAtom(dpy_, "_MOTIF_WM_HINTS", False);
    XChangeProperty(dpy_, win_, motif, motif, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char*>(&hints), 5);

    // Ask the window manager to tell us about the close button instead of
    // killing the connection under us.
    wmDelete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy_, win_, &wmDelete_, 1);
    applySizeHints();

    XSelectInput(dpy_, win_, ExposureMask | KeyPressMask | ButtonPressMask |
                             ButtonReleaseMask | PointerMotionMask |
                             StructureNotifyMask);
    XMapWindow(dpy_, win_);
    makeSurface();
    applyShape();
    damageAll();
    return true;
}

// The interface is laid out at one size and is never stretched, so the window
// says so: an equal minimum and maximum is what tells a window manager to grey
// out maximise and full screen rather than fight it.
void Window::applySizeHints() {
    XSizeHints size{};
    size.flags = PMinSize | PMaxSize;
    size.min_width = size.max_width = width_;
    size.min_height = size.max_height = height_;
    XSetWMNormalHints(dpy_, win_, &size);
}

void Window::setScale(double s) {
    s = std::clamp(std::round(s * 4) / 4, 1.0, 3.0);
    if (std::fabs(s - scale_) < 0.01) return;
    scale_ = s;
    width_  = int(pageW_ * scale_);
    height_ = int(pageH_ * scale_);
    applySizeHints();
    XResizeWindow(dpy_, win_, (unsigned)width_, (unsigned)height_);
    makeSurface();
    applyShape();
    if (onResize) onResize(width_, height_);
    damageAll();
    dirty_ = true;
}

// The page is a rounded rectangle, and the corners are cut out of the window
// rather than painted: no alpha channel is involved, so it looks the same with
// or without a compositor, and clicks in the corners fall through.
void Window::applyShape() {
    if (!dpy_ || !win_) return;
    int major = 0, minor = 0;
    if (!XShapeQueryVersion(dpy_, &major, &minor)) return;

    const int r = int(corner() * scale_);
    const int w = width_, h = height_;
    if (w <= 0 || h <= 0 || r <= 0) return;

    // A rounded rectangle as a handful of rectangles: exact along the straight
    // edges, and one row per pixel of the corner arcs.
    std::vector<XRectangle> rects;
    rects.push_back({0, short(r), (unsigned short)w, (unsigned short)(h - r * 2)});
    for (int y = 0; y < r; ++y) {
        const double dy = r - y - 0.5;
        const int dx = int(r - std::sqrt(double(r) * r - dy * dy) + 0.5);
        rects.push_back({short(dx), short(y), (unsigned short)(w - dx * 2), 1});
        rects.push_back({short(dx), short(h - 1 - y), (unsigned short)(w - dx * 2), 1});
    }
    XShapeCombineRectangles(dpy_, win_, ShapeBounding, 0, 0, rects.data(),
                            int(rects.size()), ShapeSet, Unsorted);
    XShapeCombineRectangles(dpy_, win_, ShapeInput, 0, 0, rects.data(),
                            int(rects.size()), ShapeSet, Unsorted);
}

// Hand the drag to the window manager: it knows about snapping, workspaces and
// multiple monitors, and we do not.
void Window::startDrag(int rootX, int rootY) {
    if (!dpy_ || !win_) return;
    XUngrabPointer(dpy_, CurrentTime);
    XEvent e{};
    e.xclient.type = ClientMessage;
    e.xclient.window = win_;
    e.xclient.message_type = XInternAtom(dpy_, "_NET_WM_MOVERESIZE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = rootX;
    e.xclient.data.l[1] = rootY;
    e.xclient.data.l[2] = 8;          // _NET_WM_MOVERESIZE_MOVE
    e.xclient.data.l[3] = Button1;
    e.xclient.data.l[4] = 1;          // the source is the application
    XSendEvent(dpy_, DefaultRootWindow(dpy_), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(dpy_);
}

void Window::minimise() {
    if (!dpy_ || !win_) return;
    XIconifyWindow(dpy_, win_, DefaultScreen(dpy_));
    XFlush(dpy_);
}

void Window::makeSurface() {
    dropSurface();
    xlib_  = cairo_xlib_surface_create(dpy_, win_, visual_, width_, height_);
    image_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width_, height_);
    cr_    = cairo_create(image_);
    blit_  = cairo_create(xlib_);
    // The frame already carries its own alpha, so the blit replaces the window
    // contents rather than painting over what was there.
    cairo_set_operator(blit_, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface(blit_, image_, 0, 0);
}

void Window::dropSurface() {
    if (blit_)  { cairo_destroy(blit_); blit_ = nullptr; }
    if (cr_)    { cairo_destroy(cr_); cr_ = nullptr; }
    if (image_) { cairo_surface_destroy(image_); image_ = nullptr; }
    if (xlib_)  { cairo_surface_destroy(xlib_); xlib_ = nullptr; }
}

void Window::close() {
    dropSurface();
    if (dpy_) {
        if (win_) XDestroyWindow(dpy_, win_);
        XCloseDisplay(dpy_);
        dpy_ = nullptr; win_ = 0;
    }
}

void Window::run() {
    running_ = true;
    const int fd = ConnectionNumber(dpy_);

    // EIGHTD_PROFILE=1 reports how much drawing actually costs.
    const bool profile = std::getenv("EIGHTD_PROFILE") != nullptr;
    int frames = 0, loops = 0;
    double drawMs = 0;
    auto mark = std::chrono::steady_clock::now();

    while (running_) {
        while (XPending(dpy_)) {
            XEvent ev;
            XNextEvent(dpy_, &ev);
            switch (ev.type) {
            case Expose:
                dirty_ = true;
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != width_ || ev.xconfigure.height != height_) {
                    width_  = ev.xconfigure.width;
                    height_ = ev.xconfigure.height;
                    makeSurface();
                    if (onResize) onResize(width_, height_);
                    dirty_ = true;
                }
                break;
            case ButtonPress:
            case ButtonRelease: {
                shift_ = (ev.xbutton.state & ShiftMask) != 0;
                MouseEvent m{ev.xbutton.x, ev.xbutton.y,
                             ev.xbutton.x_root, ev.xbutton.y_root,
                             int(ev.xbutton.button), ev.type == ButtonPress};
                if (onMouse) onMouse(m);
                dirty_ = true;
                break;
            }
            case MotionNotify: {
                // Coalesce: X can queue a long trail of motion events, and only
                // the newest position matters.
                XEvent last = ev;
                while (XCheckTypedWindowEvent(dpy_, win_, MotionNotify, &ev))
                    last = ev;
                shift_ = (last.xmotion.state & ShiftMask) != 0;
                if (onMotion) onMotion(last.xmotion.x, last.xmotion.y);
                // Hover highlighting and slider drags both need a repaint.
                dirty_ = true;
                break;
            }
            case KeyPress: {
                KeyEvent k;
                k.keysym = XLookupKeysym(&ev.xkey, 0);
                k.ctrl  = (ev.xkey.state & ControlMask) != 0;
                k.shift = (ev.xkey.state & ShiftMask) != 0;
                shift_ = k.shift;
                if (onKey) onKey(k);
                dirty_ = true;
                break;
            }
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == wmDelete_) {
                    if (onClose) onClose();
                    running_ = false;
                }
                break;
            default: break;
            }
        }
        if (!running_) break;

        if (onIdle && onIdle()) dirty_ = true;

        ++loops;
        // X can stay readable for reasons of its own, so select() alone is not a
        // reliable clock.  Pace the frames explicitly.
        const auto now = std::chrono::steady_clock::now();
        const double sinceFrame =
            std::chrono::duration<double, std::milli>(now - lastFrame_).count();
        if (dirty_ && cr_ && sinceFrame >= double(frameMs)) {
            lastFrame_ = now;
            dirty_ = false;
            const auto t0 = now;
            damageAll();                       // the app narrows this if it can
            if (onDraw) onDraw(cr_, width_, height_);
            cairo_surface_flush(image_);
            cairo_set_operator(blit_, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_surface(blit_, image_, 0, 0);
            cairo_rectangle(blit_, dmgX_, dmgY_, dmgW_, dmgH_);
            cairo_fill(blit_);
            cairo_surface_flush(xlib_);
            XFlush(dpy_);
            ++frames;
            drawMs += std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - t0).count();
        }

        if (profile) {
            const auto now = std::chrono::steady_clock::now();
            if (now - mark >= std::chrono::seconds(1)) {
                std::fprintf(stderr, "[profile] %d frames/s, %d loops/s, %.2f ms/frame\n",
                             frames, loops, frames ? drawMs / frames : 0.0);
                frames = 0; loops = 0; drawMs = 0; mark = now;
            }
        }

        // Sleep until either X has something or the next frame is due.
        fd_set set;
        FD_ZERO(&set);
        FD_SET(fd, &set);
        double waitMs = double(frameMs);
        if (dirty_) {
            waitMs = double(frameMs) -
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - lastFrame_).count();
            if (waitMs < 0) waitMs = 0;
        }
        timeval tv{time_t(waitMs / 1000), suseconds_t(std::fmod(waitMs, 1000.0) * 1000)};
        select(fd + 1, &set, nullptr, nullptr, &tv);
    }
}

} // namespace eightd
