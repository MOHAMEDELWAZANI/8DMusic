// Drives the control window and checks what it actually did.
//
// The interface is immediate mode: every control is a pure function of a value
// in the shared block, and the only way to know a knob is wired to the right
// field is to turn it and look. So this does exactly that -- it clicks, drags,
// rolls the wheel and types into a real 8DMusic.exe, and after each action
// reads %ProgramData%\8DMusic\state.bin to see what moved.
//
// It never guesses where a control is. The window writes down the rectangle it
// painted for every control (WM_APP+2 -> %TEMP%\8dmusic-hits.txt) and this
// clicks those. A layout change moves the test with it; a control that stops
// being drawn fails as "not found" rather than silently passing because the
// click landed on empty background.
//
// Two input modes, and the difference between them matters.
//
// By default input goes in as SendMessageW: the messages arrive on the window's
// own thread, the machine's real pointer is never moved, and a sync hook forces
// the repaint the immediate-mode loop needs in order to see them. That is fast
// and it does not fight the user for the mouse.
//
// But it posts straight into the client area, which means it SKIPS WM_NCHITTEST
// -- and a whole class of bug lives exactly there. Every control in the title
// bar once reported itself as caption, so clicking close or the 8D switch
// dragged the window instead of pressing it, and this harness passed anyway
// because it never asked the question Windows asks. `--real` therefore drives
// the actual cursor through SendInput, the same path a hand takes: hit-testing,
// hover, capture, double-click timing and all. Slower, and it takes the mouse
// for the duration, and it is the mode that finds what the other cannot.
#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "../src/shared/SharedState.h"

using namespace eightd;

namespace {

constexpr UINT WM_EIGHTD_SYNC     = WM_APP + 1;
constexpr UINT WM_EIGHTD_DUMPHITS = WM_APP + 2;
constexpr int  kMenuRow = 900000;

HWND         g_hwnd = nullptr;
SharedState* g_state = nullptr;
bool         g_real = false;      // drive the actual cursor, through SendInput
POINT        g_cursorWas{};
int          g_pass = 0, g_fail = 0;
double       g_dpi = 1.0;
bool         g_dark = true;
int          g_openMenu = 0;
std::map<int, RECT> g_hits;

// ---------------------------------------------------------------- reporting

void pass(const std::string& what) {
    std::printf("  PASS  %s\n", what.c_str());
    ++g_pass;
}
void fail(const std::string& what, const std::string& why) {
    std::printf("  FAIL  %s  -- %s\n", what.c_str(), why.c_str());
    ++g_fail;
}
void check(const std::string& what, bool ok, const std::string& why = "") {
    if (ok) pass(what); else fail(what, why);
}
std::string num(double v) {
    char b[64];
    std::snprintf(b, sizeof b, "%.4f", v);
    return b;
}
std::string moved(const char* field, double before, double after) {
    return std::string(field) + " " + num(before) + " -> " + num(after);
}

// ------------------------------------------------------------------- window

Params snap() {
    Params p{};
    if (g_state) readParams(g_state, p);
    return p;
}

// Force the window to finish a frame, so the immediate-mode pass has seen
// whatever was just sent to it.
//
// This has to be the window's own hook, not UpdateWindow() from here: called
// across a process boundary UpdateWindow does not paint before it returns, and
// every check then reads the state one action behind.
void settle() {
    // Real input is *posted* to the window's queue; WM_EIGHTD_SYNC is *sent*,
    // and a sent message jumps ahead of everything already posted. Sync alone
    // would therefore paint a frame that has not yet seen the click, and every
    // check would read one action behind -- a harness fault that looks exactly
    // like an application bug. Give the queue two of the window's own 33 ms
    // ticks to drain in order first.
    // Two syncs with a wait between them, because the first can still beat the
    // input to the queue: SendInput returns before the message is delivered, and
    // a sent message is handled ahead of everything already posted. One sync
    // alone was marginal -- most actions landed, a few read one frame early and
    // failed as if the control were dead.
    if (g_real) {
        Sleep(40);
        SendMessageW(g_hwnd, WM_EIGHTD_SYNC, 0, 0);
        Sleep(40);
    }
    SendMessageW(g_hwnd, WM_EIGHTD_SYNC, 0, 0);
}

bool readHits() {
    SendMessageW(g_hwnd, WM_EIGHTD_DUMPHITS, 0, 0);
    settle();

    wchar_t dir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"8dmusic-hits.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"r") != 0 || !f) return false;

    g_hits.clear();
    char line[256];
    while (std::fgets(line, sizeof line, f)) {
        double v = 0;
        int id = 0;
        if (std::sscanf(line, "dpi %lf", &v) == 1)        { g_dpi = v; continue; }
        if (std::sscanf(line, "dark %d", &id) == 1)       { g_dark = id != 0; continue; }
        if (std::sscanf(line, "menu %d", &id) == 1)       { g_openMenu = id; continue; }
        double x, y, w, h;
        if (std::sscanf(line, "%d %lf %lf %lf %lf", &id, &x, &y, &w, &h) == 5) {
            // Later wins: a control drawn twice in one frame is on top the
            // second time, which is what a click would hit.
            g_hits[id] = { long(x), long(y), long(x + w), long(y + h) };
        }
    }
    std::fclose(f);
    return true;
}

bool haveHit(int id) { return g_hits.find(id) != g_hits.end(); }

POINT centre(int id) {
    const RECT& r = g_hits[id];
    return { (r.left + r.right) / 2, (r.top + r.bottom) / 2 };
}

// --- real input -------------------------------------------------------------

// SendInput wants absolute coordinates normalised across the whole virtual
// desktop rather than pixels.
void sendMouse(DWORD flags, POINT screen, int data = 0) {
    const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int vw = (std::max)(1, GetSystemMetrics(SM_CXVIRTUALSCREEN));
    const int vh = (std::max)(1, GetSystemMetrics(SM_CYVIRTUALSCREEN));
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = LONG((double(screen.x - vx) * 65535.0) / vw + 0.5);
    in.mi.dy = LONG((double(screen.y - vy) * 65535.0) / vh + 0.5);
    in.mi.mouseData = DWORD(data);
    in.mi.dwFlags = flags | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(INPUT));
    Sleep(18);                       // SendInput is asynchronous
}
POINT toScreen(POINT client) {
    POINT p = client;
    ClientToScreen(g_hwnd, &p);
    return p;
}

void moveTo(POINT p) {
    if (g_real) sendMouse(MOUSEEVENTF_MOVE, toScreen(p));
    else SendMessageW(g_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
    settle();
}
void press(POINT p) {
    if (g_real) {
        sendMouse(MOUSEEVENTF_MOVE, toScreen(p));
        sendMouse(MOUSEEVENTF_LEFTDOWN, toScreen(p));
    } else {
        SendMessageW(g_hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
    }
    settle();
}
void release(POINT p) {
    if (g_real) sendMouse(MOUSEEVENTF_LEFTUP, toScreen(p));
    else SendMessageW(g_hwnd, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
    settle();
}
void clickAt(POINT p) { moveTo(p); press(p); release(p); }
void clickId(int id)  { clickAt(centre(id)); }

// Two presses inside the double-click time, at the same place.
//
// Deliberately without a settle between the halves. Settling twice takes the
// gesture past 300 ms, and on a machine whose double-click time is anywhere
// near the low end that stops being a double click at all -- the harness would
// be reporting a broken control when all it had done was gesture too slowly.
void doubleClickAt(POINT p) {
    moveTo(p);
    if (g_real) {
        const POINT sc = toScreen(p);
        sendMouse(MOUSEEVENTF_LEFTDOWN, sc);
        sendMouse(MOUSEEVENTF_LEFTUP, sc);
        sendMouse(MOUSEEVENTF_LEFTDOWN, sc);
        sendMouse(MOUSEEVENTF_LEFTUP, sc);
        settle();
    } else {
        press(p); release(p);
        press(p); release(p);
    }
}

// WM_MOUSEWHEEL carries screen coordinates; the window converts them back.
void wheelAt(POINT client, int notches) {
    const POINT screen = toScreen(client);
    for (int i = 0; i < std::abs(notches); ++i) {
        if (g_real) {
            sendMouse(MOUSEEVENTF_MOVE, screen);
            sendMouse(MOUSEEVENTF_WHEEL, screen,
                      notches > 0 ? WHEEL_DELTA : -WHEEL_DELTA);
        } else {
            SendMessageW(g_hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(client.x, client.y));
            SendMessageW(g_hwnd, WM_MOUSEWHEEL,
                         MAKEWPARAM(0, notches > 0 ? WHEEL_DELTA : -WHEEL_DELTA),
                         MAKELPARAM(screen.x, screen.y));
        }
        settle();
    }
}
void wheelOver(int id, int notches) { wheelAt(centre(id), notches); }

void key(WPARAM vk) {
    if (g_real) {
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = WORD(vk);
        SendInput(1, &in, sizeof(INPUT));
        Sleep(15);
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &in, sizeof(INPUT));
        Sleep(15);
    } else {
        SendMessageW(g_hwnd, WM_KEYDOWN, vk, 0);
    }
    settle();
}

// Park the pointer somewhere that is not a control, so the next test does not
// start with something hovered or half-armed.
void parkPointer() { moveTo({ 4, 400 }); }

// ------------------------------------------------------------------- checks

// Rolls the wheel over a knob and checks the field it is wired to moved by the
// expected amount -- which is how a knob that quietly writes to the wrong
// parameter gets caught.
void knobTest(const char* name, int id, float Params::*field,
              double step, int notches, bool expectMove = true) {
    if (!haveHit(id)) { fail(name, "control not drawn"); return; }
    const Params before = snap();
    wheelOver(id, notches);
    const Params after = snap();
    const double b = double(before.*field), a = double(after.*field);
    if (!expectMove) {
        check(std::string(name) + " (disabled, must not move)",
              std::fabs(a - b) < 1e-6, moved(name, b, a));
        return;
    }
    const double want = b + step * notches;
    const bool ok = std::fabs(a - want) < step * 0.55 + 1e-6;
    check(name, ok, moved(name, b, a) + ", expected " + num(want));
    // put it back
    wheelOver(id, -notches);
}

// Turns a knob the way a hand does: press on the dial, sweep round it, let go.
//
// Nothing else in this file does that. The wheel and a double-click both reach
// the value by a different path, so a knob whose drag arithmetic is broken --
// wrong direction, jumping to meet the pointer, dead below the cap -- passes
// every other check in here.
//
// The recorded rectangle is the forgiving grab area: the dial is `size` across
// with 4 units of margin, and the caption sits in the 26 below it.
void knobDragTest(const char* name, int id, float Params::*field,
                  double lo, double hi, double fromDeg, double toDeg) {
    if (!haveHit(id)) { fail(name, "control not drawn"); return; }
    const RECT& g = g_hits[id];
    const double sz = double(g.right - g.left) - 8.0 * g_dpi;
    const double cx = (g.left + g.right) * 0.5;
    const double cy = double(g.top) + 4.0 * g_dpi + sz * 0.5;
    const double r  = sz * 0.38;          // on the cap, well clear of the middle
    const double rad = 3.14159265358979 / 180.0;
    auto at = [&](double deg) {
        return POINT{ long(cx + std::cos(deg * rad) * r + 0.5),
                      long(cy + std::sin(deg * rad) * r + 0.5) };
    };

    const double before = double(snap().*field);
    moveTo(at(fromDeg));
    press(at(fromDeg));
    const double stepDeg = (toDeg > fromDeg) ? 10.0 : -10.0;
    for (double d = fromDeg + stepDeg;
         (stepDeg > 0) ? (d <= toDeg) : (d >= toDeg); d += stepDeg)
        moveTo(at(d));
    moveTo(at(toDeg));
    release(at(toDeg));

    const double after = double(snap().*field);
    // Three quarters of a turn covers the range: 270 degrees of travel is the
    // whole of it.
    const double want = std::clamp(before + (toDeg - fromDeg) / 270.0 * (hi - lo),
                                   lo, hi);
    const bool ok = std::fabs(after - want) < (hi - lo) * 0.10;
    check(name, ok, moved(name, before, after) + ", expected about " + num(want));
}

void tileTest(const char* name, int id, Mode want) {
    if (!haveHit(id)) { fail(name, "tile not drawn"); return; }
    clickId(id);
    const Params p = snap();
    check(name, p.mode == want,
          "mode is " + std::to_string(int(p.mode)) + ", expected " +
              std::to_string(int(want)));
}

} // namespace

int main(int argc, char** argv) {
    bool closeAtEnd = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--close") closeAtEnd = true;
        if (std::string(argv[i]) == "--real")  g_real = true;
    }

    g_hwnd = FindWindowW(L"EightDMusicWindow", nullptr);
    if (!g_hwnd) {
        std::printf("8D Music is not running. Start 8DMusic.exe first.\n");
        return 2;
    }
    g_state = openSharedState(false);
    if (!g_state) {
        std::printf("No shared block at %ls -- run the installer first.\n",
                    sharedStatePath());
        return 2;
    }

    SetForegroundWindow(g_hwnd);
    Sleep(200);
    if (g_real) GetCursorPos(&g_cursorWas);
    if (!readHits()) {
        std::printf("The window did not write its hit list. Rebuild 8DMusic.exe.\n");
        return 2;
    }
    std::printf("Window found: %zu controls drawn, dpi %.2f, %s theme\n\n",
                g_hits.size(), g_dpi, g_dark ? "dark" : "light");

    // A known starting point, so every check below reads against the defaults
    // rather than against whatever the last session left behind.
    if (haveHit(377)) { clickId(377); parkPointer(); }
    readHits();

    // ---------------------------------------------------------------- modes
    std::printf("MOVEMENT -- the eight modes\n");
    const struct { const char* name; int id; Mode m; } kModes[] = {
        { "Circle",    300, Mode::Circular }, { "Ping-pong", 301, Mode::PingPong },
        { "Pendulum",  302, Mode::Pendulum }, { "Linear",    303, Mode::Linear },
        { "Figure 8",  304, Mode::Figure8 },  { "Spiral",    305, Mode::Spiral },
        { "Random",    306, Mode::Random },   { "Static",    307, Mode::Static },
    };
    for (const auto& m : kModes) tileTest(m.name, m.id, m.m);
    parkPointer();

    // ------------------------------------------------- the orbit as a control
    std::printf("\nSTAGE -- dragging the source in Static\n");
    readHits();
    if (!haveHit(299)) {
        fail("orbit drag", "the orbit is not a control in Static");
    } else {
        const RECT o = g_hits[299];
        const POINT c{ (o.left + o.right) / 2, (o.top + o.bottom) / 2 };
        const double side = double(o.right - o.left);
        const double reach = side * 0.417 * (2.0 / 3.0);   // aim for 2.00 m
        // due right of the listener: angle +90 degrees, two metres out
        const POINT target{ c.x + long(reach), c.y };
        moveTo(target);
        press(target);
        release(target);
        const Params p = snap();
        const double deg = p.manualAngle * 180.0 / 3.14159265358979;
        check("drag parks the source to the right",
              std::fabs(deg - 90.0) < 6.0, "angle " + num(deg) + " deg");
        check("drag sets the distance",
              std::fabs(double(p.radius) - 2.0) < 0.15, "radius " + num(p.radius));
    }
    parkPointer();

    // Speed is meaningless when the source is parked, and the interface greys
    // it out; check that the grey is real and not just paint.
    readHits();
    knobTest("Speed is inert in Static", 320, &Params::speed, 0.01, 5, false);

    // back to Circle for the rest
    clickId(300);
    parkPointer();
    readHits();

    // ---------------------------------------------------------------- direction
    std::printf("\nMOVEMENT -- direction, knobs\n");
    {
        clickId(311);                       // CCW
        const int ccw = snap().direction;
        clickId(310);                       // CW
        const int cw = snap().direction;
        check("CW / CCW", ccw == -1 && cw == 1,
              "CCW gave " + std::to_string(ccw) + ", CW gave " + std::to_string(cw));
    }
    parkPointer();

    // Turning one by hand, which nothing above does.
    knobDragTest("Reverb turns when dragged", 330, &Params::reverbMix,
                 0.0, 1.0, 180, 270);
    parkPointer();
    knobDragTest("Reverb turns back the other way", 330, &Params::reverbMix,
                 0.0, 1.0, 270, 180);
    parkPointer();
    knobDragTest("Bass turns down past centre", 361, &Params::eqMid,
                 -12, 12, 270, 200);
    parkPointer();
    readHits();

    knobTest("Speed",     320, &Params::speed,      0.01, 5);
    knobTest("Distance",  321, &Params::radius,     0.01, 5);
    knobTest("Intensity", 322, &Params::depth,      0.01, -5);
    knobTest("Smooth",    323, &Params::smoothness, 0.01, 5);

    // ------------------------------------------------------------------ space
    std::printf("\nSPACE\n");
    knobTest("Reverb",  330, &Params::reverbMix,  0.01, 5);
    knobTest("Room",    331, &Params::reverbSize, 0.01, 5);
    knobTest("Damping", 332, &Params::reverbDamp, 0.01, 5);
    knobTest("Width",   333, &Params::width,      0.01, 5);

    // ------------------------------------------------------------------- echo
    std::printf("\nECHO -- including the two that stay off until Delay is up\n");
    knobTest("Time ms is inert while Delay is 0",  341, &Params::delayTime, 0.005, 5, false);
    knobTest("Feedback is inert while Delay is 0", 342, &Params::delayFeedback, 0.01, 5, false);
    knobTest("Delay", 340, &Params::delayMix, 0.01, 20);
    {
        // raise Delay, then the other two must come alive
        wheelOver(340, 20);
        readHits();
        knobTest("Time ms (Delay up)",  341, &Params::delayTime, 0.005, 5);
        knobTest("Feedback (Delay up)", 342, &Params::delayFeedback, 0.01, 5);
        wheelOver(340, -20);
    }
    parkPointer();

    // -------------------------------------------------------------- character
    std::printf("\nCHARACTER\n");
    readHits();
    {
        clickId(351);
        const bool slowed = snap().character == Character::Slowed;
        clickId(352);
        const bool radio = snap().character == Character::Radio;
        clickId(350);
        const bool clean = snap().character == Character::Clean;
        check("Clean / Slowed / Old radio", slowed && radio && clean,
              std::string(slowed ? "" : "Slowed failed ") +
                  (radio ? "" : "Radio failed ") + (clean ? "" : "Clean failed"));
    }
    parkPointer();

    // The amount slider is dead while the character is Clean, and live after.
    readHits();
    if (!haveHit(355)) {
        fail("Amount slider", "not drawn");
    } else {
        const RECT g = g_hits[355];
        // The recorded rectangle is the forgiving grab area; the painted track
        // is inset by 8 design units at each end.
        const double inset = 8.0 * g_dpi;
        const double x0 = g.left + inset, x1 = g.right - inset;
        const POINT quarter{ long(x0 + (x1 - x0) * 0.25), (g.top + g.bottom) / 2 };

        const float before = snap().characterAmount;
        clickAt(quarter);
        check("Amount is inert while Clean",
              std::fabs(snap().characterAmount - before) < 1e-6,
              moved("amount", before, snap().characterAmount));

        clickId(351);                       // Slowed
        parkPointer();
        readHits();
        clickAt(quarter);
        const double amt = snap().characterAmount;
        check("Amount lands where it is clicked", std::fabs(amt - 0.25) < 0.03,
              "amount " + num(amt) + ", expected 0.25");

        // And dragged along, which is how anyone actually sets one.
        const long my = (g.top + g.bottom) / 2;
        const POINT lo20{ long(x0 + (x1 - x0) * 0.20), my };
        const POINT hi80{ long(x0 + (x1 - x0) * 0.80), my };
        moveTo(lo20);
        press(lo20);
        for (double f = 0.30; f <= 0.80; f += 0.10)
            moveTo({ long(x0 + (x1 - x0) * f), my });
        moveTo(hi80);
        release(hi80);
        const double dragged = snap().characterAmount;
        check("Amount follows a drag", std::fabs(dragged - 0.80) < 0.04,
              "amount " + num(dragged) + ", expected 0.80");

        clickId(350);                       // back to Clean
    }
    parkPointer();

    // ------------------------------------------------------------- equaliser
    std::printf("\nEQUALISER -- whole decibels, and the bipolar fill\n");
    readHits();
    knobTest("Bass",   360, &Params::eqBass,      1.0,  3);
    knobTest("Mid",    361, &Params::eqMid,       1.0, -3);
    knobTest("Treble", 362, &Params::eqTreble,    1.0,  3);
    knobTest("Output", 363, &Params::outputGain,  0.01, 5);

    // Double-click is the way back to the default, and it is the only way to
    // reach exactly 0 dB on a dial by hand.
    if (haveHit(360)) {
        wheelOver(360, 4);
        const double lifted = snap().eqBass;
        doubleClickAt(centre(360));
        const double home = snap().eqBass;
        check("double-click resets a knob", std::fabs(lifted - 4.0) < 1.1 &&
              std::fabs(home) < 1e-6, moved("bass", lifted, home));
    }
    parkPointer();

    // Shift makes every adjustment four times finer; on a 1 dB knob that is a
    // quarter of a decibel.
    if (haveHit(362)) {
        const double before = snap().eqTreble;
        // The window reads the real keyboard for Shift, so hold it for real.
        INPUT down{}; down.type = INPUT_KEYBOARD; down.ki.wVk = VK_SHIFT;
        INPUT up = down; up.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &down, sizeof(INPUT));
        Sleep(30);
        wheelOver(362, 1);
        SendInput(1, &up, sizeof(INPUT));
        const double after = snap().eqTreble;
        check("Shift makes a knob four times finer",
              std::fabs((after - before) - 0.25) < 0.05,
              moved("treble", before, after));
        wheelOver(362, -1);
    }
    parkPointer();

    // ------------------------------------------------------------- endpoint
    std::printf("\nENDPOINT\n");
    readHits();
    {
        const bool before = snap().pauseWhenSilent;
        clickId(376);
        const bool after = snap().pauseWhenSilent;
        check("Pause orbit when silent", before != after,
              "still " + std::to_string(int(after)));
        clickId(376);
    }
    parkPointer();

    // Output is a readout, deliberately: nothing in this build may change the
    // user's output device. Check it stayed a readout -- if it ever becomes a
    // control again, this fails and someone has to justify it.
    readHits();
    check("Output is a readout, not a picker", !haveHit(381),
          "something registered a hit rectangle for the output row");
    parkPointer();

    // ---------------------------------------------------------------- presets
    std::printf("\nPRESETS\n");
    readHits();
    int presetCount = 0;
    const Preset* list = presets(presetCount);
    {
        int chips = 0;
        for (int i = 0; i < presetCount; ++i) if (haveHit(200 + i)) ++chips;
        check("preset chips are drawn", chips > 0,
              std::to_string(chips) + " of " + std::to_string(presetCount));
        for (int i = 0; i < presetCount; ++i) {
            if (!haveHit(200 + i)) continue;
            clickId(200 + i);
            const Params p = snap();
            const Params& w = list[i].p;
            const bool ok = p.mode == w.mode &&
                            std::fabs(p.speed - w.speed) < 1e-6 &&
                            std::fabs(p.radius - w.radius) < 1e-6 &&
                            std::fabs(p.reverbMix - w.reverbMix) < 1e-6 &&
                            p.character == w.character;
            check(std::string("chip: ") + list[i].name, ok, "params do not match");
        }
    }
    parkPointer();

    // The + button reaches every preset, including the ones that do not fit.
    readHits();
    if (!haveHit(240)) {
        fail("preset menu (+)", "not drawn");
    } else {
        clickId(240);
        readHits();
        int rows = 0;
        for (const auto& h : g_hits)
            if (h.first >= kMenuRow + 24000 && h.first < kMenuRow + 24100) ++rows;
        check("+ lists every preset", rows == presetCount,
              std::to_string(rows) + " rows, expected " +
                  std::to_string(presetCount));
        // Every row has to be inside the window, or the last few cannot be
        // clicked at all.
        RECT client{};
        GetClientRect(g_hwnd, &client);
        bool inside = true;
        for (const auto& h : g_hits)
            if (h.first >= kMenuRow + 24000 && h.first < kMenuRow + 24100)
                if (h.second.top < 0 || h.second.bottom > client.bottom) inside = false;
        check("the preset list stays inside the window", inside,
              "a row is drawn off the bottom");
        if (rows == presetCount) {
            const int wanted = presetCount - 1;            // the last one
            clickId(kMenuRow + 24000 + wanted);
            const Params p = snap();
            check(std::string("+ menu picks ") + list[wanted].name,
                  p.mode == list[wanted].p.mode &&
                      std::fabs(p.speed - list[wanted].p.speed) < 1e-6,
                  "params do not match");
        }
    }
    parkPointer();

    // ---------------------------------------------------------------- chrome
    std::printf("\nTITLE BAR AND KEYBOARD\n");
    readHits();
    {
        const bool before = snap().enabled;
        clickId(111);
        const bool after = snap().enabled;
        check("the 8D switch", before != after,
              "still " + std::to_string(int(after)));
        clickId(111);
    }
    parkPointer();

    {
        const bool before = snap().enabled;
        key('B');
        const bool after = snap().enabled;
        check("B bypasses", before != after, "unchanged");
        key('B');
    }
    {
        const bool before = g_dark;
        key('T');
        readHits();
        check("T switches theme", g_dark != before,
              std::string("still ") + (g_dark ? "dark" : "light"));
        key('T');
        readHits();
    }
    {
        key('3');
        const Params p = snap();
        check("3 applies the third preset",
              p.mode == list[2].p.mode &&
                  std::fabs(p.speed - list[2].p.speed) < 1e-6,
              "params do not match");
        key('1');
    }
    {
        clickId(240);                       // open a menu
        readHits();
        const bool opened = g_openMenu == 240;
        key(VK_ESCAPE);
        readHits();
        check("Escape closes an open menu", opened && g_openMenu == 0,
              "menu " + std::to_string(g_openMenu));
    }
    parkPointer();

    // Reset has to put every field back, not just the ones on screen.
    readHits();
    if (!haveHit(377)) {
        fail("Reset", "not drawn");
    } else {
        key('7');                           // something far from the defaults
        clickId(377);
        const Params p = snap();
        const Params d{};
        const bool ok = p.mode == d.mode && std::fabs(p.speed - d.speed) < 1e-6 &&
                        std::fabs(p.radius - d.radius) < 1e-6 &&
                        std::fabs(p.reverbMix - d.reverbMix) < 1e-6 &&
                        std::fabs(p.delayMix - d.delayMix) < 1e-6 &&
                        std::fabs(p.eqBass - d.eqBass) < 1e-6 &&
                        std::fabs(p.outputGain - d.outputGain) < 1e-6 &&
                        p.character == d.character;
        check("Reset returns every setting to its default", ok,
              "something was left behind");
    }
    parkPointer();

    // Minimise, and come back.
    readHits();
    if (!haveHit(120)) {
        fail("Minimise", "not drawn");
    } else {
        clickId(120);
        Sleep(300);
        const bool iconic = IsIconic(g_hwnd) != 0;
        ShowWindow(g_hwnd, SW_RESTORE);
        Sleep(300);
        check("Minimise", iconic, "the window did not minimise");
    }

    // The title bar: a drag handle where there is no control, and NOT a drag
    // handle where there is one. Getting the second half wrong makes close,
    // minimise and the 8D switch unclickable, because once Windows believes a
    // point is caption it stops delivering client mouse messages there at all.
    {
        auto hitAt = [](POINT client) {
            POINT s = client;
            ClientToScreen(g_hwnd, &s);
            return SendMessageW(g_hwnd, WM_NCHITTEST, 0, MAKELPARAM(s.x, s.y));
        };
        // Left of the tab strip and right of the wordmark: the only part of
        // the bar that is still bare.
        check("the title bar is a drag handle", hitAt({ 280, 20 }) == HTCAPTION,
              "empty bar did not report caption");

        bool controlsClickable = true;
        std::string bad;
        for (const int id : { 101, 102, 103, 111, 120, 121 }) {
            if (!haveHit(id)) { controlsClickable = false; bad += std::to_string(id); continue; }
            if (hitAt(centre(id)) != HTCLIENT) {
                controlsClickable = false;
                bad += std::to_string(id) + " ";
            }
        }
        check("top-bar controls are clickable, not caption", controlsClickable,
              "these report caption: " + bad);
    }

    // And the drag actually moves the window. Reporting HTCAPTION is only half
    // of it; this is the half a hand notices.
    if (g_real) {
        RECT before{};
        GetWindowRect(g_hwnd, &before);
        // Screen coordinates, fixed up front. Client ones would be resolved
        // against a window that is moving underneath the drag, so each step
        // would compound and the window would travel three times as far.
        POINT grab{ 280, 20 };
        ClientToScreen(g_hwnd, &grab);
        auto go = [&](int d) { sendMouse(MOUSEEVENTF_MOVE, { grab.x + d, grab.y + d }); };
        go(0);
        sendMouse(MOUSEEVENTF_LEFTDOWN, grab);
        for (int d = 12; d <= 60; d += 12) go(d);
        sendMouse(MOUSEEVENTF_LEFTUP, { grab.x + 60, grab.y + 60 });
        Sleep(250);
        RECT after{};
        GetWindowRect(g_hwnd, &after);
        const long dx = after.left - before.left, dy = after.top - before.top;
        check("dragging the bar moves the window",
              std::abs(dx - 60) < 14 && std::abs(dy - 60) < 14,
              "moved " + std::to_string(dx) + "," + std::to_string(dy) +
                  ", expected about 60,60");
        // put it back
        SetWindowPos(g_hwnd, nullptr, before.left, before.top, 0, 0,
                     SWP_NOSIZE | SWP_NOZORDER);
        Sleep(150);
    }

    // Close, last of all.
    readHits();
    if (closeAtEnd && haveHit(121)) {
        clickId(121);
        Sleep(500);
        check("Close", !IsWindow(g_hwnd), "the window is still open");
    } else {
        check("Close is drawn", haveHit(121), "not drawn");
    }

    if (g_real) SetCursorPos(g_cursorWas.x, g_cursorWas.y);
    std::printf("\n%d passed, %d failed  (%s input)\n", g_pass, g_fail,
                g_real ? "real cursor" : "injected");
    closeSharedState();
    return g_fail == 0 ? 0 : 1;
}
