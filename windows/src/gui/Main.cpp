// 8DMusic.exe -- the control window.
//
// The effect does not live here.  Windows loads the DSP into audiodg.exe as an
// APO; this process only writes settings into the shared block and draws what
// the orbit is doing.  That split is why audio keeps running when this window
// is closed, and why closing it cannot glitch playback.  Changing the interface
// changes none of it: nothing below touches src/apo or src/shared beyond
// `publish()` and reading telemetry.
//
// The layout is the desktop build's v2, function for function -- see
// cpp/src/ui/Studio.cpp.  The orbit and its presets on the left, every control
// as cards on the right, the player floating across the foot of the window, and
// the app's own title bar because the window wears no decoration.
//
// Theme.h and Icons.h are compiled in place rather than copied, so the two
// builds cannot drift into two palettes or two sets of glyphs, and the presets
// come from Params.h.
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <deque>
#include <string>
#include <vector>

#include "../shared/SharedState.h"
#include "Ui.h"
#include "NowPlaying.h"
#include "../../res/resource.h"

#pragma comment(lib, "gdiplus.lib")

using namespace eightd;

namespace {

// The page is laid out at this size and scaled by the monitor's DPI; it is
// never stretched, which is why the window refuses to be resized.
constexpr int    kBaseW  = 1180;
constexpr int    kBaseH  = 820;
constexpr double kTopBar = 56;

// Same numbers as cpp/src/ui/Studio.cpp.
constexpr double kStageW = 520;
constexpr double kStagePadL = 24, kStagePadR = 20, kStagePadT = 18;
constexpr double kRailPadL = 8, kRailPadR = 20, kColGap = 12;
constexpr double kCardPad = 14, kSechH = 24, kKnobBlock = 82, kTileH = 58;
constexpr double kCardR = 22, kOrbitSide = 460;
constexpr double kDockPad = 22, kDockH = 84, kDockBottom = 48;

Ui           g_ui;
SharedState* g_state = nullptr;
Params       g_params{};
bool         g_dark = true;
int          g_preset = -1;
LONG         g_lastBeat = -1;
int          g_staleTicks = 0;
bool         g_alive = false;
double       g_meterL = 0, g_meterR = 0;
std::deque<std::pair<double,double>> g_trail;
HWND         g_hwnd = nullptr;
Rect         g_orbitRect{};
Rect         g_plusRect{};
Rect         g_deviceBox{};
NowPlaying   g_np;
ULONGLONG    g_lastClickMs = 0;

// The wordmark lockup, one per theme, decoded once from the .rc resources.
Gdiplus::Image* g_logoDark  = nullptr;
Gdiplus::Image* g_logoLight = nullptr;

double S(double v) { return g_ui.s(v); }

// GDI+ wants a stream, and a resource is already a flat block of bytes, so it
// is copied into an HGLOBAL once rather than unpacked to a temporary file.
Gdiplus::Image* loadPngResource(HINSTANCE inst, int id) {
    HRSRC res = FindResourceW(inst, MAKEINTRESOURCEW(id), RT_RCDATA);
    if (!res) return nullptr;
    const DWORD size = SizeofResource(inst, res);
    HGLOBAL data = LoadResource(inst, res);
    if (!data || size == 0) return nullptr;
    const void* bytes = LockResource(data);
    if (!bytes) return nullptr;

    HGLOBAL buf = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!buf) return nullptr;
    if (void* dst = GlobalLock(buf)) {
        memcpy(dst, bytes, size);
        GlobalUnlock(buf);
    }
    IStream* stream = nullptr;
    // fDeleteOnRelease: the stream owns the buffer from here.
    if (FAILED(CreateStreamOnHGlobal(buf, TRUE, &stream)) || !stream) {
        GlobalFree(buf);
        return nullptr;
    }
    auto* img = Gdiplus::Image::FromStream(stream);
    stream->Release();
    if (img && img->GetLastStatus() != Gdiplus::Ok) { delete img; img = nullptr; }
    return img;
}

std::wstring widen(const char* s) {
    if (!s) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}
std::wstring fmt(const wchar_t* f, ...) {
    wchar_t buf[256];
    va_list ap; va_start(ap, f);
    _vsnwprintf_s(buf, _TRUNCATE, f, ap);
    va_end(ap);
    return buf;
}
std::wstring pct(double v)    { return fmt(L"%.0f%%", v * 100); }
std::wstring metres(double v) { return fmt(L"%.2f m", v); }
std::wstring dbText(double v) {
    if (std::fabs(v) < 0.05) return L"0";
    return fmt(L"%s%.0f", v > 0 ? L"+" : L"−", std::fabs(v));
}

// mm:ss, or h:mm:ss past an hour.
std::wstring clockText(double seconds) {
    if (seconds < 0 || seconds > 60 * 60 * 24) return L"--:--";
    const long total = long(seconds);
    if (total >= 3600)
        return fmt(L"%ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
    return fmt(L"%ld:%02ld", total / 60, total % 60);
}

const wchar_t* shortMode(Mode m) {
    switch (m) {
        case Mode::Circular: return L"Circle";
        case Mode::PingPong: return L"Ping-pong";
        case Mode::Pendulum: return L"Pendulum";
        case Mode::Linear:   return L"Linear";
        case Mode::Figure8:  return L"Figure 8";
        case Mode::Spiral:   return L"Spiral";
        case Mode::Random:   return L"Random";
        default:             return L"Static";
    }
}
const Icon& modeGlyph(Mode m) {
    switch (m) {
        case Mode::Circular: return ico::kCircular;
        case Mode::PingPong: return ico::kPingPong;
        case Mode::Pendulum: return ico::kPendulum;
        case Mode::Linear:   return ico::kLinear;
        case Mode::Figure8:  return ico::kFigure8;
        case Mode::Spiral:   return ico::kSpiral;
        case Mode::Random:   return ico::kRandom;
        default:             return ico::kStaticRing;
    }
}
const wchar_t* shortCharacter(Character c) {
    switch (c) {
        case Character::Slowed: return L"Slowed";
        case Character::Radio:  return L"Old radio";
        default:                return L"Clean";
    }
}
const Icon& characterGlyph(Character c) {
    switch (c) {
        case Character::Slowed: return ico::kSlowed;
        case Character::Radio:  return ico::kRadio;
        default:                return ico::kClean;
    }
}

// Where the sound is, in words.
std::wstring bearing(double deg) {
    const wchar_t* side = deg > 8 ? L"right" : (deg < -8 ? L"left" : L"centre");
    const bool front = std::fabs(deg) <= 90;
    if (std::fabs(deg) <= 8)   return front ? L"front" : L"back";
    if (std::fabs(deg) >= 172) return L"back";
    return std::wstring(front ? L"front-" : L"back-") + side;
}

void publish() { if (g_state) writeParams(g_state, g_params); }

// ------------------------------------------------------------------ devices
struct Device { std::wstring name; bool isDefault = false; };
std::vector<Device> g_devices;
int g_device = 0;

void refreshDevices() {
    g_devices.clear();
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&en)))) return;

    std::wstring defaultId;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eConsole, &def)) && def) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id)) && id) { defaultId = id; CoTaskMemFree(id); }
        def->Release();
    }
    IMMDeviceCollection* col = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(col->Item(i, &dev)) || !dev) continue;
            LPWSTR id = nullptr;
            std::wstring thisId;
            if (SUCCEEDED(dev->GetId(&id)) && id) { thisId = id; CoTaskMemFree(id); }
            IPropertyStore* store = nullptr;
            if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &store)) && store) {
                PROPVARIANT v; PropVariantInit(&v);
                if (SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) &&
                    v.vt == VT_LPWSTR && v.pwszVal)
                    g_devices.push_back({ v.pwszVal, thisId == defaultId });
                PropVariantClear(&v);
                store->Release();
            }
            dev->Release();
        }
        col->Release();
    }
    en->Release();
    for (size_t i = 0; i < g_devices.size(); ++i)
        if (g_devices[i].isDefault) { g_device = int(i); break; }
}

void applyPreset(int i) {
    int count = 0;
    const Preset* list = presets(count);
    if (i < 0 || i >= count) return;
    const bool wasEnabled = g_params.enabled;
    g_params = list[i].p;
    g_params.enabled = wasEnabled;
    g_preset = i;
    publish();
}

void resetSettings() {
    const bool wasEnabled = g_params.enabled;
    g_params = Params{};
    g_params.enabled = wasEnabled;
    g_preset = -1;
    publish();
}

// Where the effect stands, in one line.  The APO is either in the audio path
// or it is not; there is no start button to press.
struct ApoState { std::wstring label; std::wstring detail; Rgb tint; bool lit; };

ApoState apoState() {
    if (!g_state)
        return { L"Not installed", L"run the installer, then reopen this window",
                 g_ui.theme.warn, false };
    if (g_state->formatRejected)
        return { L"Format refused",
                 fmt(L"this output is %u channels / %u-bit; the effect is 32-bit stereo",
                     g_state->rejectedChannels, g_state->rejectedBits),
                 g_ui.theme.warn, false };
    if (g_alive) {
        std::wstring detail = L"in the audio path";
        if (g_state->sampleRate)
            detail = fmt(L"%u Hz · %u ch", g_state->sampleRate, g_state->channels);
        return { L"Processing", detail, g_ui.theme.deep, true };
    }
    return { L"Waiting", L"play something", g_ui.theme.faint, false };
}

// ------------------------------------------------------------------ orbit

// The floor, the rings and the listener: everything that stays where it is.
// Drawn in the 470-unit grid the design uses.
void drawOrbitStatic(const Rect& r) {
    const double k = r.w / 470.0;
    auto X = [&](double v) { return r.x + v * k; };
    auto Y = [&](double v) { return r.y + v * k; };
    const double cx = X(235), cy = Y(235);
    const Theme& t = g_ui.theme;

    // the lit floor: brightest just above the middle, so it reads as a stage
    g_ui.discGradient(cx, cy, 196 * k, Y(179),
                      mix(t.ground, t.text, t.dark ? 0.17 : 0.07),
                      mix(t.ground, t.text, t.dark ? 0.04 : 0.02));

    // the rim, then the light just inside it
    g_ui.ring(cx, cy, 196 * k, mix(t.line, t.text, 0.12), 1);
    g_ui.ring(cx, cy, 192 * k, t.accent, 3, 0.10);
    g_ui.ring(cx, cy, 131 * k, mix(t.line, t.text, 0.10), 1);
    g_ui.ring(cx, cy,  65 * k, mix(t.line, t.text, 0.10), 1);
    g_ui.line(cx, Y(39), cx, Y(431), t.line, 1, 0.7);
    g_ui.line(X(39), cy, X(431), cy, t.line, 1, 0.7);

    g_ui.font(9, W400);
    g_ui.text(X(241), Y(93), L"2 m", t.ghost);
    g_ui.text(X(241), Y(158), L"1 m", t.ghost);

    // the listener, sitting in a dip so it reads against the lit floor
    const Rgb head = mix(t.raised, t.text, t.dark ? 0.16 : 0.0);
    g_ui.circle(cx, cy, 30 * k, Rgb{ 0, 0, 0 }, 0.22);
    g_ui.circle(X(216), Y(235), 5 * k, head);
    g_ui.circle(X(254), Y(235), 5 * k, head);
    g_ui.circle(cx, cy, 18 * k, head);

    // the compass, tucked between the rim and the edge of the box
    g_ui.font(9.5, W700);
    g_ui.tracked(cx, r.y + S(20), L"FRONT", t.ghost, S(2), Align::Centre);
    g_ui.tracked(cx, r.y + r.h - S(20), L"BACK", t.ghost, S(2), Align::Centre);
    g_ui.tracked(r.x + S(14), cy, L"L", t.ghost, S(2));
    g_ui.tracked(r.x + r.w - S(14), cy, L"R", t.ghost, S(2), Align::Right);
}

// Everything here comes from the DSP's own telemetry, never a UI clock: if the
// effect is not running the dot does not move, which is the truth.
void drawOrbitLive(const Rect& r) {
    const double k = r.w / 470.0;
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
    const double scale = r.w * 0.417;              // 196/470: the 3 m rim
    const Theme& t = g_ui.theme;
    const bool on = g_params.enabled;

    float angle = 0.f, dist = 1.f;
    if (g_state) { angle = g_state->angle; dist = g_state->distance; }
    const double d = std::clamp(double(dist), 0.2, 3.0);
    const double rad = d / 3.0 * scale;
    const double sx = cx + std::sin(angle) * rad;
    const double sy = cy - std::cos(angle) * rad;

    g_ui.ringDashed(cx, cy, rad, t.accent, 1.5, 0.55, 1.5 * k, 10.5 * k);

    // the trail: a stroke that thins and fades behind the source
    g_trail.push_back({ sx, sy });
    while (g_trail.size() > 46) g_trail.pop_front();
    const size_t n = g_trail.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const double f = double(i + 1) / double(n);
        g_ui.line(g_trail[i].first, g_trail[i].second,
                  g_trail[i + 1].first, g_trail[i + 1].second,
                  on ? t.motion : t.ghost, S(1.0 + f * 4.5), f * f * 0.95);
    }

    // the source, and its light
    g_ui.glow(sx, sy, 34 * k, t.motion, g_alive && on ? 0.55 : (on ? 0.34 : 0.14));
    g_ui.circle(sx, sy, 9.5 * k, on ? t.motion : t.ghost);
    g_ui.ring(sx, sy, 9.5 * k, Rgb::hex(0xFFD1E3), 1.6, on ? 0.55 : 0.2);
}

// `+37 deg . 1.40 m . front-right`, with the meters beside it.
void drawReadout(const Rect& r) {
    float angle = 0.f, dist = 1.f;
    if (g_state) { angle = g_state->angle; dist = g_state->distance; }
    double deg = std::fmod(angle * 180.0 / kPi + 540.0, 360.0) - 180.0;

    g_ui.font(13, W700);
    const std::wstring a = fmt(L"%+.0f°", deg);
    g_ui.text(r.x, r.cy(), a, g_ui.theme.motion);
    const double w1 = g_ui.textWidth(a);
    g_ui.font(13, W400);
    const std::wstring dtext = metres(dist);
    g_ui.text(r.x + w1 + S(12), r.cy(), dtext, g_ui.theme.dim);
    const double w2 = g_ui.textWidth(dtext);
    g_ui.text(r.x + w1 + S(12) + w2 + S(12), r.cy(), bearing(deg), g_ui.theme.faint);
}

void drawMeters(const Rect& r) {
    float pL = 0.f, pR = 0.f;
    if (g_state) { pL = g_state->peakL; pR = g_state->peakR; }
    g_meterL = std::max(double(pL), g_meterL * 0.82);
    g_meterR = std::max(double(pR), g_meterR * 0.82);

    const wchar_t* names[2] = { L"L", L"R" };
    const double vals[2] = { g_meterL, g_meterR };
    for (int i = 0; i < 2; ++i) {
        const double ly = r.y + S(5) + i * S(9);
        g_ui.font(9, W700);
        g_ui.text(r.x, ly, names[i], g_ui.theme.ghost);
        const Rect track{ r.x + S(12), ly - S(2), r.w - S(12), S(4) };
        g_ui.fillRound(track, kPill, g_ui.theme.well);
        const double t = std::clamp(vals[i], 0.0, 1.0);
        if (t > 0.002)
            g_ui.fillRound({ track.x, track.y, track.w * t, track.h }, kPill,
                           t < 0.9 ? g_ui.theme.accent : g_ui.theme.motion);
    }
}

// ------------------------------------------------------------------ chrome

// The window's own title bar: there is no decoration behind it.
void drawTopBar(const Rect& r) {
    g_ui.fillRect({ r.x, r.y + r.h - 1, r.w, 1 }, g_ui.theme.text, 0.05);

    // The lockup, not a typeset wordmark: it is the same mark the icon and the
    // other builds carry. Falls back to the drawn one if the resource is gone.
    Gdiplus::Image* logo = g_dark ? g_logoDark : g_logoLight;
    double wordX = r.x + S(56);
    if (logo && logo->GetHeight() > 0) {
        const double lh = S(26);
        const double lw = lh * double(logo->GetWidth()) / double(logo->GetHeight());
        g_ui.image(logo, { r.x + S(20), r.cy() - lh * 0.5, lw, lh });
        wordX = r.x + S(20) + lw + S(12);
    } else {
        g_ui.logo({ r.x + S(20), r.cy() - S(12), S(24), S(24) });
    }
    g_ui.font(11, W700);
    g_ui.tracked(wordX, r.cy(), L"8D MUSIC", g_ui.theme.dim, S(2));

    // the window's own buttons, at the trailing edge
    const double btn = S(36);
    const Rect closeBtn{ r.x + r.w - S(12) - btn, r.cy() - btn * 0.5, btn, btn };
    const Rect minBtn{ closeBtn.x - S(2) - btn, closeBtn.y, btn, btn };

    // what the effect is doing, and the master switch
    const ApoState apo = apoState();
    g_ui.font(13, W400);
    const double dw = g_ui.textWidth(apo.detail);
    g_ui.font(13, W600);
    const double sw = g_ui.textWidth(apo.label);
    const double pillW = S(14) + S(16) + sw + S(8) + S(6) + S(8) + dw + S(14);

    const Rect swBox{ minBtn.x - S(16) - S(44), r.cy() - S(13), S(44), S(26) };
    g_ui.font(12, W700);
    const double lw2 = g_ui.trackedWidth(L"8D", S(1.2)) + S(5);
    const Rect pillR{ swBox.x - lw2 - S(14) - pillW, r.cy() - S(17), pillW, S(34) };

    g_ui.fillRound(pillR, kPill, g_ui.theme.card);
    const double ledX = pillR.x + S(18);
    if (apo.lit) g_ui.glow(ledX, pillR.cy(), S(11), g_ui.theme.deep, 0.5);
    g_ui.circle(ledX, pillR.cy(), S(4), apo.lit ? g_ui.theme.deep : g_ui.theme.ghost);
    g_ui.font(13, W600);
    g_ui.text(pillR.x + S(30), pillR.cy(), apo.label,
              apo.lit ? g_ui.theme.text : apo.tint);
    g_ui.font(13, W400);
    g_ui.text(pillR.x + S(30) + sw + S(8), pillR.cy(), L"·", g_ui.theme.faint);
    g_ui.text(pillR.x + S(30) + sw + S(22), pillR.cy(), apo.detail, g_ui.theme.dim);

    g_ui.font(12, W700);
    g_ui.tracked(swBox.x - lw2 - S(9), r.cy(), L"8D", g_ui.theme.deep, S(1.2));
    if (g_ui.switchPill(111, swBox, g_params.enabled)) {
        g_params.enabled = !g_params.enabled;
        publish();
    }

    if (g_ui.click(120, minBtn)) ShowWindow(g_hwnd, SW_MINIMIZE);
    if (g_ui.over(minBtn)) g_ui.fillRound(minBtn, S(12), g_ui.theme.well);
    g_ui.line(minBtn.cx() - S(7), minBtn.cy(), minBtn.cx() + S(7), minBtn.cy(),
              g_ui.over(minBtn) ? g_ui.theme.text : g_ui.theme.faint, S(1.8));

    const bool overClose = g_ui.over(closeBtn);
    if (g_ui.click(121, closeBtn)) PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    if (overClose) g_ui.fillRound(closeBtn, S(12), Rgb::hex(0xE5484D));
    const Rgb x = overClose ? Rgb::hex(0xFFFFFF) : g_ui.theme.faint;
    g_ui.line(closeBtn.cx() - S(6), closeBtn.cy() - S(6),
              closeBtn.cx() + S(6), closeBtn.cy() + S(6), x, S(1.8));
    g_ui.line(closeBtn.cx() + S(6), closeBtn.cy() - S(6),
              closeBtn.cx() - S(6), closeBtn.cy() + S(6), x, S(1.8));
}

// ------------------------------------------------------------------ stage
void drawStage(const Rect& r) {
    const double x = r.x + S(kStagePadL);
    const double w = r.w - S(kStagePadL) - S(kStagePadR);
    double y = r.y + S(kStagePadT);

    int count = 0;
    const Preset* list = presets(count);

    g_ui.font(27, W600, true);
    g_ui.text(x, y + S(17), L"Studio", g_ui.theme.text);
    g_ui.font(12.5, W400);
    g_ui.text(x + w, y + S(18),
              g_preset >= 0 ? widen(list[g_preset].name) : L"Custom",
              g_ui.theme.faint, Align::Right);
    y += S(34);

    const double side = std::min(S(kOrbitSide), w);
    const Rect orbit{ x + (w - side) * 0.5, y + S(8), side, side };
    g_orbitRect = orbit;
    drawOrbitStatic(orbit);
    drawOrbitLive(orbit);
    y = orbit.y + side;

    drawReadout({ x, y + S(10), w - S(144), S(20) });
    drawMeters({ x + w - S(130), y + S(10), S(130), S(20) });
    y += S(30) + S(14);

    // presets
    const Rect plus{ x, y, S(32), S(32) };
    g_plusRect = plus;
    if (g_ui.click(240, plus)) g_ui.openMenu = 240;
    g_ui.fillRound(plus, kPill, g_ui.over(plus)
                   ? mix(g_ui.theme.well, g_ui.theme.text, 0.08) : g_ui.theme.well);
    g_ui.icon(ico::kPlus, { plus.x + S(9.5), plus.y + S(9.5), S(13), S(13) },
              g_ui.theme.dim);

    double cx = plus.x + plus.w + S(8);
    for (int i = 0; i < count; ++i) {
        g_ui.font(12.5, W500);
        const std::wstring name = widen(list[i].name);
        const double tw = g_ui.textWidth(name) + S(30);
        if (cx + tw > x + w) break;                 // the rest live under +
        if (g_preset == i)
            g_ui.glow(cx + tw * 0.5, y + S(16), S(46), g_ui.theme.accent, 0.28);
        if (g_ui.chip(200 + i, { cx, y, tw, S(32) }, name, g_preset == i,
                      g_ui.theme.card))
            applyPreset(i);
        cx += tw + S(8);
    }
}

// ------------------------------------------------------------------- rail
void drawRail(const Rect& r, bool& dirty) {
    const double x0 = r.x + S(kRailPadL);
    const double total = r.w - S(kRailPadL) - S(kRailPadR);
    const double colW = (total - S(kColGap)) * 0.5;
    const double innerW = colW - S(kCardPad) * 2;
    const double top = r.y + S(kStagePadT);
    const Theme& t = g_ui.theme;

    auto card = [&](double x, double y, double h) {
        g_ui.fillRound({ x, y, colW, h }, S(kCardR), t.card);
    };
    auto sech = [&](double x, double y, const wchar_t* kick, const wchar_t* note) {
        g_ui.font(10.5, W700);
        g_ui.tracked(x + S(kCardPad), y + S(kCardPad) + S(12), kick, t.accent, S(2));
        if (note && *note) {
            g_ui.font(11.5, W400);
            g_ui.text(x + colW - S(kCardPad), y + S(kCardPad) + S(12), note,
                      t.faint, Align::Right);
        }
    };
    auto knobCell = [&](double x, int i) {
        const double cell = innerW / 4.0;
        return Rect{ x + S(kCardPad) + cell * i, 0, cell, S(kKnobBlock) };
    };

    // ---- left column ----------------------------------------------------
    double x = x0, y = top;

    const double moveH = S(kCardPad) + S(kSechH) + S(10) + S(kTileH) * 2 + S(7)
                       + S(12) + S(kKnobBlock) + S(16);
    card(x, y, moveH);
    sech(x, y, L"MOVEMENT", nullptr);
    {
        const Rect seg{ x + colW - S(kCardPad) - S(96), y + S(kCardPad), S(96), S(30) };
        const int pick = g_ui.segPill(310, seg, { L"CW", L"CCW" },
                                      g_params.direction >= 0 ? 0 : 1);
        if (pick >= 0) { g_params.direction = pick == 0 ? 1 : -1; dirty = true; }
    }
    double gy = y + S(kCardPad) + S(kSechH) + S(10);
    const double tileW = (innerW - S(7) * 3) / 4.0;
    for (int i = 0; i < 8; ++i) {
        const Rect cell{ x + S(kCardPad) + (tileW + S(7)) * (i % 4),
                         gy + (S(kTileH) + S(7)) * (i / 4), tileW, S(kTileH) };
        const Mode m = Mode(i);
        if (g_ui.tile(300 + i, cell, shortMode(m), modeGlyph(m),
                      g_params.mode == m, m == Mode::Static)) {
            g_params.mode = m; g_preset = -1; dirty = true;
        }
        if (m == Mode::Static) {   // the dot that marks the parked position
            const Rgb c = g_params.mode == m ? t.deep : t.faint;
            g_ui.icon(ico::kStaticPos,
                      { cell.cx() - S(11), cell.cy() - S(15), S(22), S(22) }, c);
        }
    }
    gy += S(kTileH) * 2 + S(7) + S(12);
    {
        double speed = g_params.speed, radius = g_params.radius;
        double depth = g_params.depth, smooth = g_params.smoothness;
        Rect b = knobCell(x, 0); b.y = gy;
        if (g_ui.knob(320, b, L"Speed", fmt(L"%.2f", speed), speed, 0.01, 1.2,
                      g_params.mode != Mode::Static, false, 0.12, true)) {
            g_params.speed = float(speed); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 1); b.y = gy;
        if (g_ui.knob(321, b, L"Distance", metres(radius), radius, 0.25, 3.0,
                      true, false, 1.0, true)) {
            g_params.radius = float(radius); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 2); b.y = gy;
        if (g_ui.knob(322, b, L"Intensity", pct(depth), depth, 0.0, 1.0,
                      true, false, 0.85, true)) {
            g_params.depth = float(depth); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 3); b.y = gy;
        if (g_ui.knob(323, b, L"Smooth", pct(smooth), smooth, 0.0, 1.0,
                      true, false, 0.35, true)) {
            g_params.smoothness = float(smooth); g_preset = -1; dirty = true;
        }
    }
    y += moveH + S(kColGap);

    const double spaceH = S(kCardPad) + S(kSechH) + S(12) + S(kKnobBlock) + S(16);
    card(x, y, spaceH);
    sech(x, y, L"SPACE", L"Reverb & stereo");
    {
        const double ky = y + S(kCardPad) + S(kSechH) + S(12);
        double mix_ = g_params.reverbMix, size = g_params.reverbSize;
        double damp = g_params.reverbDamp, width = g_params.width;
        Rect b = knobCell(x, 0); b.y = ky;
        if (g_ui.knob(330, b, L"Reverb", pct(mix_), mix_, 0.0, 1.0,
                      true, false, 0.18, true)) {
            g_params.reverbMix = float(mix_); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 1); b.y = ky;
        if (g_ui.knob(331, b, L"Room", pct(size), size, 0.0, 1.0,
                      true, false, 0.6, true)) {
            g_params.reverbSize = float(size); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 2); b.y = ky;
        if (g_ui.knob(332, b, L"Damping", pct(damp), damp, 0.0, 1.0,
                      true, false, 0.45, true)) {
            g_params.reverbDamp = float(damp); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 3); b.y = ky;
        if (g_ui.knob(333, b, L"Width", pct(width), width, 0.0, 2.0,
                      true, false, 1.0, true)) {
            g_params.width = float(width); g_preset = -1; dirty = true;
        }
    }
    y += spaceH + S(kColGap);

    card(x, y, spaceH);
    {
        const bool on = g_params.delayMix > 0.001f;
        sech(x, y, L"ECHO", on ? L"Repeats follow the orbit" : L"Off · raise Delay");
        const double ky = y + S(kCardPad) + S(kSechH) + S(12);
        double mix_ = g_params.delayMix, time = g_params.delayTime;
        double fb = g_params.delayFeedback;
        Rect b = knobCell(x, 0); b.y = ky;
        if (g_ui.knob(340, b, L"Delay", pct(mix_), mix_, 0.0, 1.0,
                      true, false, 0.0, true)) {
            g_params.delayMix = float(mix_); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 1); b.y = ky;
        if (g_ui.knob(341, b, L"Time ms", fmt(L"%.0f", time * 1000), time, 0.04, 1.2,
                      on, false, 0.28, true, 58, 0.005)) {
            g_params.delayTime = float(time); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 2); b.y = ky;
        if (g_ui.knob(342, b, L"Feedback", pct(fb), fb, 0.0, 0.85,
                      on, false, 0.35, true)) {
            g_params.delayFeedback = float(fb); g_preset = -1; dirty = true;
        }
    }

    // ---- right column ---------------------------------------------------
    x = x0 + colW + S(kColGap); y = top;

    // Character's amount reads across, not around: one long throw is easier to
    // place than a dial, and there is width here to give it.
    const double charH = S(kCardPad) + S(kSechH) + S(10) + S(62) + S(16)
                       + S(18) + S(12) + S(6) + S(16);
    card(x, y, charH);
    sech(x, y, L"CHARACTER", L"Colour on the source");
    {
        const double cw = (innerW - S(14)) / 3.0;
        for (int i = 0; i < 3; ++i) {
            const Character c = Character(i);
            const Rect cell{ x + S(kCardPad) + (cw + S(7)) * i,
                             y + S(kCardPad) + S(kSechH) + S(10), cw, S(62) };
            if (g_ui.tile(350 + i, cell, shortCharacter(c), characterGlyph(c),
                          g_params.character == c)) {
                g_params.character = c; g_preset = -1; dirty = true;
            }
        }
        const bool on = g_params.character != Character::Clean;
        double amount = g_params.characterAmount;
        const double ly = y + S(kCardPad) + S(kSechH) + S(10) + S(62) + S(16);
        g_ui.font(13, W400);
        g_ui.text(x + S(kCardPad), ly + S(8), L"Amount", on ? t.dim : t.ghost);
        g_ui.font(13, W700);
        g_ui.text(x + colW - S(kCardPad), ly + S(8), pct(amount),
                  on ? t.accent : t.ghost, Align::Right);
        const Rect track{ x + S(kCardPad), ly + S(30), innerW, S(6) };
        if (g_ui.slider(355, track, amount, 0.0, 1.0, on)) {
            g_params.characterAmount = float(amount); g_preset = -1; dirty = true;
        }
    }
    y += charH + S(kColGap);

    card(x, y, spaceH);
    sech(x, y, L"EQUALISER", L"±12 dB");
    {
        const double ky = y + S(kCardPad) + S(kSechH) + S(12);
        double bass = g_params.eqBass, mid = g_params.eqMid, treble = g_params.eqTreble;
        double gain = g_params.outputGain;
        // A tone control is read in whole decibels, so that is its step.
        Rect b = knobCell(x, 0); b.y = ky;
        if (g_ui.knob(360, b, L"Bass", dbText(bass), bass, -12, 12,
                      true, true, 0, true, 58, 1)) {
            g_params.eqBass = float(bass); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 1); b.y = ky;
        if (g_ui.knob(361, b, L"Mid", dbText(mid), mid, -12, 12,
                      true, true, 0, true, 58, 1)) {
            g_params.eqMid = float(mid); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 2); b.y = ky;
        if (g_ui.knob(362, b, L"Treble", dbText(treble), treble, -12, 12,
                      true, true, 0, true, 58, 1)) {
            g_params.eqTreble = float(treble); g_preset = -1; dirty = true;
        }
        b = knobCell(x, 3); b.y = ky;
        if (g_ui.knob(363, b, L"Output", pct(gain), gain, 0.0, 1.5,
                      true, false, 0.9, true)) {
            g_params.outputGain = float(gain); g_preset = -1; dirty = true;
        }
    }
    y += spaceH + S(kColGap);

    // Where the desktop build has ENGINE, Windows has the endpoint and the
    // state of the APO: there is nothing to start, only somewhere to be.
    const double rowH = S(26), rowGap = S(12);
    const double endH = S(kCardPad) + S(kSechH) + S(12) + rowH * 4 + rowGap * 3 + S(16);
    card(x, y, endH);
    {
        const ApoState apo = apoState();
        sech(x, y, L"ENDPOINT", L"APO in audiodg");
        double ry = y + S(kCardPad) + S(kSechH) + S(12);

        g_ui.font(13, W400);
        g_ui.text(x + S(kCardPad), ry + rowH * 0.5, L"Output", t.text);
        g_deviceBox = { x + colW - S(kCardPad) - S(178), ry, S(178), rowH + S(4) };
        g_ui.dropdown(381, g_deviceBox,
                      g_devices.empty()
                          ? L"No output found"
                          : g_devices[std::min<size_t>(g_device,
                                g_devices.size() - 1)].name,
                      !g_devices.empty());
        ry += rowH + rowGap;

        g_ui.font(13, W400);
        g_ui.text(x + S(kCardPad), ry + rowH * 0.5, L"Effect", t.text);
        g_ui.font(12.5, W700);
        g_ui.text(x + colW - S(kCardPad), ry + rowH * 0.5, apo.label,
                  apo.lit ? t.accent : apo.tint, Align::Right);
        ry += rowH + rowGap;

        g_ui.font(13, W400);
        g_ui.text(x + S(kCardPad), ry + rowH * 0.5, L"Pause orbit when silent", t.text);
        {
            const Rect b{ x + colW - S(kCardPad) - S(44), ry, S(44), S(26) };
            bool pause = g_params.pauseWhenSilent;
            if (g_ui.switchPill(376, b, pause, false)) {
                g_params.pauseWhenSilent = !pause;
                dirty = true;
            }
        }
        ry += rowH + rowGap;

        g_ui.font(13, W400);
        g_ui.text(x + S(kCardPad), ry + rowH * 0.5, L"Reset studio to defaults", t.dim);
        {
            const Rect b{ x + colW - S(kCardPad) - S(70), ry - S(2), S(70), S(30) };
            if (g_ui.click(377, b)) resetSettings();
            g_ui.fillRound(b, kPill, g_ui.over(b)
                           ? mix(t.well, t.text, 0.08) : t.well);
            g_ui.font(12.5, W700);
            g_ui.text(b.cx(), b.cy(), L"Reset", t.accent, Align::Centre);
        }
    }
}

// ------------------------------------------------------------------- dock

// The transport floats: it belongs to the player, not to the effect.
void drawNowPlaying(const Rect& dock) {
    const bool have = g_np.has();
    const Track t = g_np.track();
    const Theme& th = g_ui.theme;

    g_ui.glow(dock.cx(), dock.cy() + S(8), dock.w * 0.4, th.accent,
              th.dark ? 0.05 : 0.03);
    g_ui.fillRound(dock, kPill, th.card);
    g_ui.strokeRound(dock, kPill, th.text, 1, 0.05);

    // the cover: a lettered gradient, as in the design
    const Rect cover{ dock.x + S(16), dock.cy() - S(26), S(52), S(52) };
    g_ui.fillRound(cover, S(15), have ? Rgb::hex(0x7A1E3C) : th.well);
    if (have) {
        g_ui.glow(cover.x + cover.w * 0.30, cover.y + cover.h * 0.25,
                  cover.w * 0.62, Rgb::hex(0xFFB36B), 0.85);
        g_ui.glow(cover.x + cover.w * 0.80, cover.y + cover.h * 0.85,
                  cover.w * 0.62, Rgb::hex(0xFF458E), 0.85);
    }
    g_ui.font(17, W700);
    g_ui.text(cover.cx(), cover.cy(), have ? t.initials() : L"—",
              have ? Rgb::hex(0xFFFFFF) : th.ghost, Align::Centre);

    // The transport is a readout here, not a control: Windows gives us the
    // session's state but this window does not drive it.
    const double tx = cover.x + cover.w + S(16);
    const double tWidth = dock.x + dock.w - S(18) - tx;
    const std::wstring tag = have && !t.player.empty()
                           ? t.player + L" · SESSION" : L"MEDIA SESSION";
    g_ui.font(10.5, W700);
    const double tagW = g_ui.trackedWidth(tag, S(1.6)) + S(14);
    g_ui.tracked(tx + tWidth, dock.cy() - S(14), tag, th.ghost, S(1.6), Align::Right);

    g_ui.font(15, W700);
    const std::wstring title = have ? t.title : L"Nothing playing";
    const double titleW = std::min(g_ui.textWidth(title),
                                   std::max(S(60), tWidth - tagW - S(140)));
    g_ui.text(tx, dock.cy() - S(14), g_ui.fit(title, titleW),
              have ? th.text : th.dim);
    g_ui.font(12.5, W400);
    g_ui.text(tx + titleW + S(10), dock.cy() - S(14),
              g_ui.fit(have ? t.artistLine() : L"Start a player and it appears here",
                       std::max(0.0, tWidth - titleW - S(10) - tagW)), th.dim);

    // NowPlaying stamps its position on the steady clock, so the elapsed time
    // has to be read from the same one -- a different epoch would put the
    // progress bar anywhere.
    const double now = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const double pos = have ? t.at(now) : 0.0;
    const double len = have ? t.length : 0.0;
    g_ui.font(11, W400);
    const std::wstring left = have ? clockText(pos) : L"--:--";
    const std::wstring right = (have && len > 0) ? clockText(len) : L"--:--";
    const double lw = g_ui.textWidth(left) + S(9), rw = g_ui.textWidth(right) + S(9);
    const double by = dock.cy() + S(13);
    g_ui.text(tx, by, left, th.faint);
    g_ui.text(tx + tWidth, by, right, th.faint, Align::Right);
    const Rect bar{ tx + lw, by - S(2), tWidth - lw - rw, S(4) };
    if (bar.w > S(10)) {
        g_ui.fillRound(bar, kPill, th.well);
        if (len > 0) {
            const double frac = std::clamp(pos / len, 0.0, 1.0);
            if (frac > 0.001)
                g_ui.fillRound({ bar.x, bar.y, bar.w * frac, bar.h }, kPill, th.motion);
            g_ui.glow(bar.x + bar.w * frac, by, S(16), th.motion, 0.5);
            g_ui.circle(bar.x + bar.w * frac, by, S(5), th.motion);
        }
    }
}

// The line along the bottom: what the effect is waiting for, or what the
// keyboard can do when it is not waiting for anything.
void drawStatus(const Rect& r) {
    const ApoState apo = apoState();
    if (!apo.lit) {
        g_ui.font(11.5, W500);
        g_ui.text(r.cx(), r.cy(), apo.label + L" — " + apo.detail, apo.tint,
                  Align::Centre);
        return;
    }
    struct Key { const wchar_t* key; const wchar_t* what; };
    const Key keys[] = { { L"B", L"bypass" }, { L"T", L"theme" },
                         { L"1-0", L"presets" },
                         { L"", L"drag the bar to move the window" } };
    const int count = int(sizeof(keys) / sizeof(keys[0]));
    double total = 0;
    for (const auto& k : keys) {
        g_ui.font(11, W600); total += g_ui.textWidth(k.key);
        g_ui.font(11, W400);
        total += (*k.key ? S(6) : 0) + g_ui.textWidth(k.what) + S(18);
    }
    total -= S(18);
    double kx = r.cx() - total * 0.5;
    for (int i = 0; i < count; ++i) {
        if (*keys[i].key) {
            g_ui.font(11, W600);
            g_ui.text(kx, r.cy(), keys[i].key, g_ui.theme.ghost);
            kx += g_ui.textWidth(keys[i].key) + S(6);
        }
        g_ui.font(11, W400);
        g_ui.text(kx, r.cy(), keys[i].what, g_ui.theme.faint);
        kx += g_ui.textWidth(keys[i].what);
        if (i + 1 < count) {
            g_ui.text(kx + S(7), r.cy(), L"·", g_ui.theme.ghost);
            kx += S(18);
        }
    }
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    const int w = client.right, h = client.bottom;

    // Double buffered: the orbit repaints many times a second and a flickering
    // window reads as a broken one.
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(dc, bmp));

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    g_ui.theme = g_dark ? Theme::darkTheme() : Theme::light();
    g_ui.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    g_ui.begin(dc, &g);
    g_ui.hot = 0;

    g_ui.fillRect({ 0, 0, double(w), double(h) }, g_ui.theme.ground);

    // The light this layout stands in, kept below the title bar so the chrome
    // does not read as part of the stage.
    const double top = S(kTopBar);
    g_ui.pushClip({ 0, top, double(w), double(h) - top });
    g_ui.glow(w * 0.30, top + S(330), S(510), g_ui.theme.accent,
              g_dark ? 0.17 : 0.08);
    g_ui.glow(w * 0.40, top + S(200), S(451), g_ui.theme.motion,
              g_dark ? 0.16 : 0.07);
    g_ui.popClip();

    bool dirty = false;
    const Rect body{ 0, top, double(w), double(h) - top };
    drawTopBar({ 0, 0, double(w), top });
    drawStage({ body.x, body.y, S(kStageW), body.h });
    drawRail({ body.x + S(kStageW), body.y, body.w - S(kStageW), body.h }, dirty);
    drawNowPlaying({ body.x + S(kDockPad),
                     body.y + body.h - S(kDockBottom) - S(kDockH),
                     body.w - S(kDockPad) * 2, S(kDockH) });
    drawStatus({ 0, double(h) - S(30), double(w), S(20) });

    // Menus paint last so their lists sit above the page.
    int countPresets = 0;
    const Preset* list = presets(countPresets);
    std::vector<std::wstring> names;
    for (int i = 0; i < countPresets; ++i) names.push_back(widen(list[i].name));
    int pick = g_ui.menuPopup(240, g_plusRect, names, g_preset, 190);
    if (pick >= 0) applyPreset(pick);

    std::vector<std::wstring> devNames;
    for (const auto& d : g_devices) devNames.push_back(d.name);
    pick = g_ui.menuPopup(381, g_deviceBox, devNames, g_device);
    if (pick >= 0) g_device = pick;

    if (dirty) publish();

    BitBlt(screen, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);

    g_ui.endFrame();
}

// The window wears no decoration, so its outline is ours to cut.
void applyRoundedCorners(HWND hwnd) {
    RECT c;
    GetClientRect(hwnd, &c);
    const int r = int(S(16) * 2);
    HRGN rgn = CreateRoundRectRgn(0, 0, c.right + 1, c.bottom + 1, r, r);
    SetWindowRgn(hwnd, rgn, TRUE);      // the window owns the region from here
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 33, nullptr);
        SetTimer(hwnd, 2, 2000, nullptr);
        return 0;

    case WM_TIMER:
        if (wp == 1) {
            // The heartbeat is the only honest answer to "is it running".
            // audiodg stops calling APOProcess when nothing plays, so a few
            // stale ticks mean idle, not dead.
            if (g_state) {
                const LONG beat = g_state->heartbeat;
                if (beat != g_lastBeat) { g_alive = true; g_staleTicks = 0; }
                else if (++g_staleTicks > 8) g_alive = false;
                g_lastBeat = beat;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == 2) {
            refreshDevices();
        }
        return 0;

    case WM_MOUSEMOVE:
        g_ui.mouseX = GET_X_LPARAM(lp);
        g_ui.mouseY = GET_Y_LPARAM(lp);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        g_ui.mouseX = GET_X_LPARAM(lp);
        g_ui.mouseY = GET_Y_LPARAM(lp);
        const ULONGLONG now = GetTickCount64();
        g_ui.doubleClick = (now - g_lastClickMs) < GetDoubleClickTime();
        g_lastClickMs = now;
        g_ui.mouseDown = true;
        g_ui.mousePressed = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_LBUTTONUP:
        g_ui.mouseX = GET_X_LPARAM(lp);
        g_ui.mouseY = GET_Y_LPARAM(lp);
        g_ui.mouseDown = false;
        g_ui.mouseReleased = true;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    // Anywhere along the bar that is not a control is a handle: press it and
    // the window moves.  Windows does the dragging; it knows about edges,
    // monitors and snapping, and we do not.
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        if (p.y < S(kTopBar) && g_ui.hot == 0 && g_ui.openMenu == 0)
            return HTCAPTION;
        return HTCLIENT;
    }

    case WM_MOUSEWHEEL: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        g_ui.mouseX = p.x;
        g_ui.mouseY = p.y;
        g_ui.wheel = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (wp >= '1' && wp <= '9')      applyPreset(int(wp - '1'));
        else if (wp == '0')              applyPreset(9);
        else if (wp == 'B') { g_params.enabled = !g_params.enabled; publish(); }
        else if (wp == 'T') { g_dark = !g_dark; }
        else if (wp == VK_ESCAPE) g_ui.openMenu = 0;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        g_ui.dpi = LOWORD(wp) / 96.0f;
        SetWindowPos(hwnd, nullptr, 0, 0,
                     int(kBaseW * g_ui.dpi), int(kBaseH * g_ui.dpi),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        applyRoundedCorners(hwnd);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_SIZE:       applyRoundedCorners(hwnd);
                        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_PAINT:      paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:    KillTimer(hwnd, 1); KillTimer(hwnd, 2); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The typeface travels with the app, as it does on Linux.
std::wstring bundledFontPath() {
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return L"";
    std::wstring dir(exe);
    const size_t cut = dir.find_last_of(L'\\');
    if (cut == std::wstring::npos) return L"";
    dir.resize(cut + 1);
    const wchar_t* tries[] = { L"Figtree.ttf", L"assets\\fonts\\Figtree.ttf",
                               L"..\\..\\cpp\\assets\\fonts\\Figtree.ttf" };
    for (const wchar_t* t : tries) {
        const std::wstring path = dir + t;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return L"";
}

} // namespace

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Gdiplus::GdiplusStartupInput gdiIn;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiIn, nullptr);

    g_logoDark  = loadPngResource(inst, IDR_LOGO_DARK);
    g_logoLight = loadPngResource(inst, IDR_LOGO_LIGHT);

    // The installer creates the block: audiodg lives in session 0, so it has to
    // cross sessions, and a non-elevated process has no SeCreateGlobalPrivilege
    // with which to make a Global\ object. See SharedState.h.
    g_state = openSharedState(true);
    if (g_state) { g_params = g_state->params; publish(); }

    refreshDevices();
    g_np.start();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // Both sizes explicitly: hIcon is the Alt+Tab and taskbar one, hIconSm the
    // small one. Letting Windows derive the small one from the large gives a
    // blurry downscale instead of the hand-tuned 16px in the .ico.
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON),
                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON),
                                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0));
    wc.lpszClassName = L"EightDMusicWindow";
    RegisterClassExW(&wc);

    g_ui.dpi = GetDpiForSystem() / 96.0f;
    g_ui.useBundledFont(bundledFontPath());

    // Never open larger than the work area: the page is one size, so if it will
    // not fit at this scale it is drawn smaller rather than cropped.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int maxW = work.right - work.left, maxH = work.bottom - work.top;
    while (g_ui.dpi > 1.0f &&
           (kBaseW * g_ui.dpi > maxW - 40 || kBaseH * g_ui.dpi > maxH - 40))
        g_ui.dpi -= 0.25f;

    const int winW = int(kBaseW * g_ui.dpi), winH = int(kBaseH * g_ui.dpi);

    // WS_POPUP: no frame, no caption, no system resize. The app draws its own
    // title bar, and WM_NCHITTEST hands the drag back to Windows.
    g_hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"8D Music",
                             WS_POPUP | WS_MINIMIZEBOX,
                             work.left + (maxW - winW) / 2,
                             work.top + (maxH - winH) / 2,
                             winW, winH,
                             nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) { Gdiplus::GdiplusShutdown(gdiToken); CoUninitialize(); return 1; }

    applyRoundedCorners(g_hwnd);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    g_np.stop();
    delete g_logoDark;
    delete g_logoLight;
    closeSharedState();
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    return 0;
}
