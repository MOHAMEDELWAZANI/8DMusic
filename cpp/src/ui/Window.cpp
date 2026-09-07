#include "Window.h"
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <sys/select.h>
#include <cstring>
#include <ctime>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>

namespace eightd {

bool Window::open(const char* title, int width, int height, std::string& error) {
    dpy_ = XOpenDisplay(nullptr);
    if (!dpy_) { error = "cannot open the X display"; return false; }
    const int screen = DefaultScreen(dpy_);
    visual_ = DefaultVisual(dpy_, screen);
    width_ = width; height_ = height;

    win_ = XCreateSimpleWindow(dpy_, RootWindow(dpy_, screen), 0, 0,
                               (unsigned)width, (unsigned)height, 0,
                               BlackPixel(dpy_, screen), WhitePixel(dpy_, screen));
    XStoreName(dpy_, win_, title);

    // Ask the window manager to tell us about the close button instead of
    // killing the connection under us.
    wmDelete_ = XInternAtom(dpy_, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy_, win_, &wmDelete_, 1);

    XSizeHints hints{};
    hints.flags = PMinSize;
    hints.min_width = 900; hints.min_height = 600;
    XSetWMNormalHints(dpy_, win_, &hints);

    XSelectInput(dpy_, win_, ExposureMask | KeyPressMask | ButtonPressMask |
                             ButtonReleaseMask | PointerMotionMask |
                             StructureNotifyMask);
    XMapWindow(dpy_, win_);
    makeSurface();
    damageAll();
    return true;
}

void Window::makeSurface() {
    dropSurface();
    xlib_  = cairo_xlib_surface_create(dpy_, win_, visual_, width_, height_);
    image_ = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width_, height_);
    cr_    = cairo_create(image_);
    blit_  = cairo_create(xlib_);
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
                MouseEvent m{ev.xbutton.x, ev.xbutton.y, int(ev.xbutton.button),
                             ev.type == ButtonPress};
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
