// 8DMusic.exe -- the control window.
//
// The effect does not live here.  Windows loads the DSP into audiodg.exe as an
// APO; this process only writes settings into shared memory and draws what the
// orbit is doing.  That split is why the audio keeps running when this window
// is closed, and why closing it cannot glitch playback.
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../shared/SharedState.h"

#pragma comment(lib, "comctl32.lib")

using namespace eightd;

namespace {

SharedState* g_state = nullptr;
Params       g_params{};
int          g_preset = 0;
LONG         g_lastBeat = -1;
bool         g_apoAlive = false;

constexpr int kOrbitSize   = 300;
constexpr int kMargin      = 18;
constexpr int kRowHeight   = 46;
constexpr int kWindowWidth = 760;

// Ink, matching the desktop build's dark theme.
constexpr COLORREF kGround = RGB(0x0B, 0x0C, 0x0E);
constexpr COLORREF kPanel  = RGB(0x12, 0x14, 0x17);
constexpr COLORREF kLine   = RGB(0x2A, 0x2E, 0x35);
constexpr COLORREF kInk    = RGB(0xE6, 0xE8, 0xEC);
constexpr COLORREF kDim    = RGB(0x8A, 0x8F, 0x98);
constexpr COLORREF kAccent = RGB(0x62, 0xC5, 0xEE);
constexpr COLORREF kMotion = RGB(0xE8, 0x22, 0x5F);

struct Slider {
    const wchar_t* label;
    float Params::* field;
    float lo, hi;
    const wchar_t* unit;
};

// Only the controls that earn their place in a small window; the rest live in
// presets.
const Slider kSliders[] = {
    { L"Movement speed", &Params::speed,      0.01f, 1.00f, L" rot/s" },
    { L"Orbit radius",   &Params::radius,     0.20f, 3.00f, L" m"     },
    { L"Effect depth",   &Params::depth,      0.00f, 1.00f, L"%"      },
    { L"Smoothness",     &Params::smoothness, 0.00f, 1.00f, L"%"      },
    { L"Stereo width",   &Params::width,      0.00f, 2.00f, L"%"      },
    { L"Reverb",         &Params::reverbMix,  0.00f, 1.00f, L"%"      },
    { L"Output",         &Params::outputGain, 0.00f, 1.50f, L"%"      },
};
constexpr int kSliderCount = ARRAYSIZE(kSliders);

int g_dragging = -1;

void publish() {
    if (g_state) writeParams(g_state, g_params);
}

RECT sliderRect(int i) {
    const int x = kMargin * 2 + kOrbitSize;
    const int y = kMargin + 70 + i * kRowHeight;
    return RECT{ x, y + 24, kWindowWidth - kMargin, y + 30 };
}

void drawText(HDC dc, int x, int y, const wchar_t* s, COLORREF c, int size, bool bold) {
    HFONT f = CreateFontW(size, 0, 0, 0, bold ? FW_SEMIBOLD : FW_NORMAL,
                          FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    HFONT old = static_cast<HFONT>(SelectObject(dc, f));
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    TextOutW(dc, x, y, s, static_cast<int>(wcslen(s)));
    SelectObject(dc, old);
    DeleteObject(f);
}

void fill(HDC dc, RECT r, COLORREF c) {
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

void drawOrbit(HDC dc) {
    const int cx = kMargin + kOrbitSize / 2;
    const int cy = kMargin + 40 + kOrbitSize / 2;
    const int r  = kOrbitSize / 2 - 6;

    HPEN pen = CreatePen(PS_SOLID, 1, kLine);
    HPEN old = static_cast<HPEN>(SelectObject(dc, pen));
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, cx - r, cy - r, cx + r, cy + r);
    Ellipse(dc, cx - r / 2, cy - r / 2, cx + r / 2, cy + r / 2);
    MoveToEx(dc, cx, cy - r, nullptr); LineTo(dc, cx, cy + r);
    MoveToEx(dc, cx - r, cy, nullptr); LineTo(dc, cx + r, cy);
    SelectObject(dc, old);
    DeleteObject(pen);

    float angle = 0.f, distance = 1.f;
    if (g_state) { angle = g_state->angle; distance = g_state->distance; }

    const float d = (std::min)(3.0f, (std::max)(0.2f, distance)) / 3.0f;
    const int px = cx + static_cast<int>(r * d * std::sin(angle));
    const int py = cy - static_cast<int>(r * d * std::cos(angle));

    HBRUSH dot = CreateSolidBrush(g_apoAlive ? kAccent : kDim);
    RECT dr{ px - 7, py - 7, px + 7, py + 7 };
    HBRUSH oldB = static_cast<HBRUSH>(SelectObject(dc, dot));
    Ellipse(dc, dr.left, dr.top, dr.right, dr.bottom);
    SelectObject(dc, oldB);
    DeleteObject(dot);

    wchar_t buf[128];
    const int deg = static_cast<int>(angle * 180.0f / 3.14159265f) % 360;
    swprintf_s(buf, L"%+d°   %.2f m", deg, distance);
    drawText(dc, kMargin, cy + r + 14, buf, kAccent, 16, true);
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);

    RECT client;
    GetClientRect(hwnd, &client);

    // Double buffered: the orbit repaints many times a second and a flickering
    // window reads as a broken one.
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, client.right, client.bottom);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(dc, bmp));

    fill(dc, client, kGround);

    drawText(dc, kMargin, kMargin, L"8D MUSIC", kAccent, 15, true);
    drawText(dc, kMargin + 110, kMargin + 2, L"real-time spatial audio", kDim, 13, false);

    const wchar_t* status =
        !g_state   ? L"shared state unavailable" :
        g_apoAlive ? L"PROCESSING  ·  the effect is in the audio path"
                   : L"WAITING  ·  play something, or install the APO";
    drawText(dc, kMargin, kMargin + 22, status, g_apoAlive ? kAccent : kDim, 13, false);

    drawOrbit(dc);

    for (int i = 0; i < kSliderCount; ++i) {
        const Slider& s = kSliders[i];
        const RECT track = sliderRect(i);
        const float v = g_params.*(s.field);
        const float t = (v - s.lo) / (s.hi - s.lo);

        drawText(dc, track.left, track.top - 22, s.label, kInk, 15, false);

        wchar_t val[64];
        if (s.unit[0] == L'%') swprintf_s(val, L"%d%%", static_cast<int>(v * 100.0f + 0.5f));
        else swprintf_s(val, L"%.2f%s", v, s.unit);
        SIZE sz{};
        GetTextExtentPoint32W(dc, val, static_cast<int>(wcslen(val)), &sz);
        drawText(dc, track.right - sz.cx - 8, track.top - 22, val, kAccent, 15, true);

        fill(dc, track, kLine);
        RECT filled = track;
        filled.right = track.left + static_cast<int>((track.right - track.left) * t);
        fill(dc, filled, kAccent);

        const int hx = filled.right;
        HBRUSH knob = CreateSolidBrush(kAccent);
        HBRUSH oldK = static_cast<HBRUSH>(SelectObject(dc, knob));
        Ellipse(dc, hx - 7, track.top - 4, hx + 7, track.bottom + 4);
        SelectObject(dc, oldK);
        DeleteObject(knob);
    }

    const int py = kMargin + 70 + kSliderCount * kRowHeight + 10;
    drawText(dc, kMargin * 2 + kOrbitSize, py, L"Press 1-9 for presets, B to bypass", kDim, 13, false);

    BitBlt(screen, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
}

void setFromMouse(int index, int x) {
    const RECT r = sliderRect(index);
    const float t = (std::min)(1.0f, (std::max)(0.0f,
        static_cast<float>(x - r.left) / static_cast<float>(r.right - r.left)));
    const Slider& s = kSliders[index];
    g_params.*(s.field) = s.lo + t * (s.hi - s.lo);
    publish();
}

int hitSlider(int x, int y) {
    for (int i = 0; i < kSliderCount; ++i) {
        RECT r = sliderRect(i);
        InflateRect(&r, 8, 12);
        if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return i;
    }
    return -1;
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 33, nullptr);
        return 0;

    case WM_TIMER: {
        // The heartbeat is the only honest answer to "is the effect running".
        if (g_state) {
            const LONG beat = g_state->heartbeat;
            g_apoAlive = (beat != g_lastBeat);
            g_lastBeat = beat;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONDOWN: {
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        g_dragging = hitSlider(x, y);
        if (g_dragging >= 0) { SetCapture(hwnd); setFromMouse(g_dragging, x); }
        return 0;
    }
    case WM_MOUSEMOVE:
        if (g_dragging >= 0) setFromMouse(g_dragging, GET_X_LPARAM(lp));
        return 0;
    case WM_LBUTTONUP:
        if (g_dragging >= 0) { ReleaseCapture(); g_dragging = -1; }
        return 0;

    case WM_KEYDOWN: {
        int count = 0;
        const Preset* presets = eightd::presets(count);
        if (wp >= '1' && wp <= '9') {
            const int i = static_cast<int>(wp - '1');
            if (i < count) { g_preset = i; g_params = presets[i].p; publish(); }
        } else if (wp == 'B') {
            g_params.enabled = !g_params.enabled;
            publish();
        } else if (wp == 'R') {
            g_params = presets[0].p;
            publish();
        }
        return 0;
    }

    case WM_PAINT: paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY: KillTimer(hwnd, 1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    // The GUI owns the block: the APO opens it read-only and copes if it is
    // not there yet.
    g_state = openSharedState(true);
    if (g_state) {
        g_params = g_state->params;
        publish();
    }

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"EightDMusicWindow";
    RegisterClassExW(&wc);

    const int height = kMargin * 2 + 70 + kSliderCount * kRowHeight + 60;
    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"8D Music",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth + 16, height,
        nullptr, nullptr, inst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    closeSharedState();
    return 0;
}
