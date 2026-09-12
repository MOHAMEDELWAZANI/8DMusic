// 8DMusic.exe -- the control window.
//
// The effect does not live here.  Windows loads the DSP into audiodg.exe as an
// APO; this process only writes settings into the shared block and draws what
// the orbit is doing.  That split is why audio keeps running when this window
// is closed, and why closing it cannot glitch playback.
//
// The layout is the desktop build's, not a Windows reinterpretation of it:
// a top bar, a stage on the left carrying the orbit, its readout, the meters
// and the preset chips, and a single scrolling rail on the right holding every
// parameter under four headings -- MOVEMENT, CHARACTER, SPACE, OUTPUT.
// One page. No tabs. See cpp/src/ui/App.cpp, which this mirrors function for
// function, and the mockup's 1a/1b, which are this layout in light and dark.
//
// Theme.h is compiled in place rather than copied, so the two builds cannot
// drift into two different palettes, and the presets come from Params.h.
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

// Same numbers as cpp/src/ui/App.cpp.
constexpr double kRailWidth = 430;
constexpr double kTopBar    = 66;
constexpr double kPad       = 26;
constexpr int    kBaseW     = 1240;
constexpr int    kBaseH     = 800;

Ui           g_ui;
SharedState* g_state = nullptr;
Params       g_params{};
bool         g_dark = true;
int          g_preset = -1;
LONG         g_lastBeat = -1;
int          g_staleTicks = 0;
bool         g_alive = false;
double       g_railScroll = 0;
double       g_railHeight = 0;
double       g_meterL = 0, g_meterR = 0;
std::deque<std::pair<double,double>> g_trail;
HWND         g_hwnd = nullptr;
Rect         g_orbitRect{};
NowPlaying   g_np;

// The wordmark lockup, one per theme, decoded once from the .rc resources.
Gdiplus::Image* g_logoDark  = nullptr;
Gdiplus::Image* g_logoLight = nullptr;

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
std::wstring degrees(double v){ return fmt(L"%+.0f°", v); }
std::wstring msText(double v) { return fmt(L"%.0f ms", v * 1000); }
std::wstring speedText(double v) {
    // Rotations per second is the parameter; seconds per turn is what a person
    // hears, so show both, exactly as the desktop build does.
    return fmt(L"%.2f rot/s · %.1f s", v, v > 0.0001 ? 1.0 / v : 0.0);
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
    g_preset = i;
    g_params = list[i].p;
    publish();
}

void resetSettings() {
    g_params = Params{};
    g_preset = -1;
    publish();
}

// ------------------------------------------------------------------ orbit
void drawOrbitStatic(const Rect& r) {
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
    const double scale = r.w * 0.45;

    for (double k : { 0.94, 0.66, 0.38 })
        g_ui.ring(cx, cy, scale * k, g_ui.theme.lineSoft, 1.0);

    g_ui.line(cx, r.y, cx, r.y + r.h, g_ui.theme.lineSoft);
    g_ui.line(r.x, cy, r.x + r.w, cy, g_ui.theme.lineSoft);

    g_ui.font(10, true);
    g_ui.tracked(cx - g_ui.s(18), r.y + g_ui.s(8), L"FRONT", g_ui.theme.inkGhost, g_ui.s(1.4));
    g_ui.tracked(cx - g_ui.s(14), r.y + r.h - g_ui.s(8), L"BACK", g_ui.theme.inkGhost, g_ui.s(1.4));
    g_ui.text(r.x + g_ui.s(6), cy, L"L", g_ui.theme.inkGhost);
    g_ui.text(r.x + r.w - g_ui.s(6), cy, L"R", g_ui.theme.inkGhost, Align::Right);

    // the listener
    const double head = std::max(scale * 0.11, g_ui.s(14));
    g_ui.ring(cx, cy, head, g_ui.theme.line, 2.0);
    g_ui.ring(cx - head, cy, head * 0.24, g_ui.theme.line, 2.0);
    g_ui.ring(cx + head, cy, head * 0.24, g_ui.theme.line, 2.0);
    g_ui.triangle(cx, cy - head - head * 0.42,
                  cx - head * 0.28, cy - head + head * 0.05,
                  cx + head * 0.28, cy - head + head * 0.05,
                  g_ui.theme.inkGhost);
}

void drawOrbitLive(const Rect& r) {
    const double cx = r.x + r.w * 0.5, cy = r.y + r.h * 0.5;
    const double scale = r.w * 0.45;

    // Everything here comes from the DSP's own telemetry, never a UI clock:
    // if the effect is not running the dot does not move, which is the truth.
    float angle = 0.f, dist = 1.f;
    if (g_state) { angle = g_state->angle; dist = g_state->distance; }
    const double d = std::clamp(double(dist), 0.2, 3.0);
    const double rad = d / 3.0 * scale;
    const double sx = cx + std::sin(angle) * rad;
    const double sy = cy - std::cos(angle) * rad;

    g_ui.ringDashed(cx, cy, rad, g_ui.theme.accent, 1.2, 0.5);

    g_trail.push_back({ sx, sy });
    while (g_trail.size() > 52) g_trail.pop_front();
    const size_t n = g_trail.size();
    for (size_t i = 0; i + 1 < n; ++i) {
        const double f = double(i) / double(std::max<size_t>(n - 1, 1));
        g_ui.circle(g_trail[i].first, g_trail[i].second, g_ui.s(1.0 + f * 2.6),
                    g_alive ? g_ui.theme.motion : g_ui.theme.inkGhost, f * 0.5);
    }

    g_ui.circle(sx, sy, g_ui.s(17), g_ui.theme.accent, 0.16);
    g_ui.circle(sx, sy, g_ui.s(7), g_alive ? g_ui.theme.accent : g_ui.theme.inkGhost);
    g_ui.line(cx, cy, sx, sy, g_ui.theme.accent, 1.0, 0.35, true);
}

void drawReadout(const Rect& orbit) {
    float angle = 0.f, dist = 1.f;
    UINT32 rate = 0, ch = 0;
    if (g_state) {
        angle = g_state->angle; dist = g_state->distance;
        rate = g_state->sampleRate; ch = g_state->channels;
    }
    double deg = std::fmod(angle * 180.0 / kPi + 180.0, 360.0);
    if (deg < 0) deg += 360.0;
    deg -= 180.0;
    const wchar_t* where = deg > 6 ? L"right" : (deg < -6 ? L"left" : L"centre");

    std::wstring line = g_alive
        ? fmt(L"%+.0f°  ·  %.2f m  ·  %s   ·   %u Hz, %u ch", deg, dist, where, rate, ch)
        : fmt(L"%+.0f°  ·  %.2f m  ·  %s", deg, dist, where);
    g_ui.font(13);
    g_ui.text(orbit.x, orbit.y + orbit.h + g_ui.s(20), line, g_ui.theme.inkSoft);
}

void drawMeters(const Rect& r) {
    float pL = 0.f, pR = 0.f;
    if (g_state) { pL = g_state->peakL; pR = g_state->peakR; }
    g_meterL = std::max(double(pL), g_meterL * 0.82);
    g_meterR = std::max(double(pR), g_meterR * 0.82);

    const wchar_t* names[2] = { L"L", L"R" };
    const double vals[2] = { g_meterL, g_meterR };
    for (int i = 0; i < 2; ++i) {
        const Rect row{ r.x, r.y + i * g_ui.s(14), r.w, g_ui.s(10) };
        g_ui.font(10, true);
        g_ui.text(row.x, row.y + g_ui.s(5), names[i], g_ui.theme.inkGhost);
        const Rect track{ row.x + g_ui.s(18), row.y + g_ui.s(2.5),
                          row.w - g_ui.s(18), g_ui.s(5) };
        g_ui.fillRound(track, g_ui.s(2), g_ui.theme.lineSoft);
        const double t = std::clamp(vals[i], 0.0, 1.0);
        if (t > 0.002) {
            const Rgb c = t < 0.7 ? g_ui.theme.good
                        : (t < 0.9 ? g_ui.theme.warn : g_ui.theme.motion);
            g_ui.fillRound({ track.x, track.y, track.w * t, track.h }, g_ui.s(2.5), c);
        }
    }
}

// ------------------------------------------------------------------ regions
void drawTopBar(const Rect& r) {
    g_ui.fillRect(r, g_ui.theme.chrome);
    g_ui.fillRect({ r.x, r.y + r.h - 1, r.w, 1 }, g_ui.theme.line);

    // The lockup, not a typeset wordmark: it is the same mark the icon and the
    // other builds carry. Falls back to text if the resource is missing, so a
    // stripped build still says what it is.
    Gdiplus::Image* logo = g_dark ? g_logoDark : g_logoLight;
    if (logo && logo->GetHeight() > 0) {
        const double lh = g_ui.s(40);
        const double lw = lh * double(logo->GetWidth()) / double(logo->GetHeight());
        g_ui.image(logo, { g_ui.s(kPad), r.h * 0.5 - lh * 0.5, lw, lh });
    } else {
        g_ui.font(15, true);
        g_ui.tracked(g_ui.s(kPad), r.h * 0.5, L"8D MUSIC", g_ui.theme.ink, g_ui.s(2.4));
    }

    const Rect seg{ r.w - g_ui.s(kPad) - g_ui.s(132), r.h * 0.5 - g_ui.s(15),
                    g_ui.s(132), g_ui.s(30) };
    const int pick = g_ui.segmented(900, seg, { L"Light", L"Dark" }, g_dark ? 1 : 0);
    if (pick >= 0) g_dark = (pick == 1);
}

void drawStage(const Rect& r) {
    const double side = std::min(r.w - g_ui.s(kPad) * 2, r.h - g_ui.s(210));
    const Rect orbit{ r.x + (r.w - side) * 0.5, r.y + g_ui.s(18), side, side };
    g_orbitRect = orbit;

    drawOrbitStatic(orbit);
    drawOrbitLive(orbit);
    drawReadout(orbit);
    drawMeters({ orbit.x, orbit.y + orbit.h + g_ui.s(36), orbit.w, g_ui.s(22) });

    // presets
    int count = 0;
    const Preset* list = presets(count);
    const double chipH = g_ui.s(30), gap = g_ui.s(8);
    double x = orbit.x, y = orbit.y + orbit.h + g_ui.s(74);
    g_ui.font(13);
    for (int i = 0; i < count; ++i) {
        const std::wstring name = widen(list[i].name);
        const double tw = g_ui.textWidth(name) + g_ui.s(26);
        if (x + tw > orbit.x + orbit.w) { x = orbit.x; y += chipH + gap; }
        if (g_ui.chip(1000 + i, { x, y, tw, chipH }, name, g_preset == i))
            applyPreset(i);
        x += tw + gap;
    }
}


// mm:ss, or h:mm:ss past an hour.
std::wstring clockText(double seconds) {
    if (seconds < 0 || seconds > 60 * 60 * 24) return L"--:--";
    const long total = long(seconds);
    if (total >= 3600)
        return fmt(L"%ld:%02ld:%02ld", total / 3600, (total / 60) % 60, total % 60);
    return fmt(L"%ld:%02ld", total / 60, total % 60);
}

// Trims with an ellipsis rather than letting a long title run under the rail.
std::wstring fitText(std::wstring s, double limit) {
    if (g_ui.textWidth(s) <= limit) return s;
    while (!s.empty()) {
        s.pop_back();
        if (g_ui.textWidth(s + L"…") <= limit) break;
    }
    return s + L"…";
}

// The desktop build's NOW PLAYING block, minus the transport buttons: this is
// a read-out, not a remote control.
double drawNowPlaying(const Rect& r, double y) {
    const double x = r.x + g_ui.s(kPad);
    const double w = r.w - g_ui.s(kPad) * 2 - g_ui.s(6);
    const bool have = g_np.has();
    const Track t = g_np.track();

    g_ui.font(11, true);
    g_ui.tracked(x, y + g_ui.s(6), L"NOW PLAYING", g_ui.theme.accentText, g_ui.s(1.8));
    if (have && !t.player.empty()) {
        g_ui.font(12);
        g_ui.text(x + w, y + g_ui.s(6), t.player, g_ui.theme.inkFaint, Align::Right);
    }
    y += g_ui.s(30);

    // cover mark
    const double art = g_ui.s(62);
    const Rect cover{ x, y, art, art };
    g_ui.fillRound(cover, g_ui.s(6), have ? g_ui.theme.ink : g_ui.theme.field);
    if (!have) g_ui.strokeRound(cover, g_ui.s(6), g_ui.theme.line);
    g_ui.font(21, true);
    g_ui.text(cover.x + art * 0.5, cover.y + art * 0.5, have ? t.initials() : L"—",
              have ? g_ui.theme.chrome : g_ui.theme.inkGhost, Align::Centre);

    const double tx = x + art + g_ui.s(16);
    const double tw = w - art - g_ui.s(16);
    g_ui.font(17, true);
    g_ui.text(tx, y + g_ui.s(15), have ? fitText(t.title, tw) : L"Nothing playing",
              have ? g_ui.theme.ink : g_ui.theme.inkFaint);
    g_ui.font(13.5);
    g_ui.text(tx, y + g_ui.s(38),
              have ? fitText(t.artistLine(), tw)
                   : L"Start a player and it appears here",
              g_ui.theme.inkSoft);

    // Just the length. No elapsed time and no progress bar: the position a
    // player reports is only as honest as the player, and a bar that creeps or
    // sits at zero is worse than not drawing one.
    if (have) {
        g_ui.font(12.5);
        g_ui.text(tx, y + g_ui.s(58),
                  t.length > 0 ? clockText(t.length) : L"--:--",
                  g_ui.theme.inkFaint);
    }

    return y + art + g_ui.s(18);
}

// The rail carries every parameter, so it scrolls. One page, four headings.
void drawRail(const Rect& r) {
    g_ui.fillRect(r, g_ui.theme.rail);
    g_ui.fillRect({ r.x, r.y, 1, r.h }, g_ui.theme.line);

    const double x = r.x + g_ui.s(kPad);
    const double w = r.w - g_ui.s(kPad) * 2 - g_ui.s(6);
    double y = r.y + g_ui.s(22) - g_railScroll;

    // Everything scrolls, so everything is clipped to the rail.
    g_ui.pushClip(r);

    y = drawNowPlaying(r, y);
    g_ui.fillRect({ x, y + g_ui.s(6), w, 1 }, g_ui.theme.line);
    y += g_ui.s(14);

    bool dirty = false;
    auto section = [&](const wchar_t* title) {
        y += g_ui.s(14);
        g_ui.font(11, true);
        g_ui.tracked(x, y + g_ui.s(6), title, g_ui.theme.accentText, g_ui.s(1.8));
        y += g_ui.s(30);
    };
    auto slider = [&](int id, const wchar_t* label, double& value, double lo, double hi,
                      const std::wstring& readout, const wchar_t* hint = L"",
                      bool enabled = true) {
        if (g_ui.slider(id, { x, y, w, g_ui.s(34) }, label, readout, value, lo, hi,
                        hint, enabled)) {
            g_preset = -1;
            dirty = true;
        }
        y += (hint && *hint) ? g_ui.s(74) : g_ui.s(56);
    };

    // ---- movement
    section(L"MOVEMENT");
    const Rect modeBox{ x, y, w * 0.56 - g_ui.s(5), g_ui.s(34) };
    const Rect dirBox { x + w * 0.56 + g_ui.s(5), y, w * 0.44 - g_ui.s(5), g_ui.s(34) };
    g_ui.dropdown(100, modeBox, widen(modeLabel(g_params.mode)));
    g_ui.dropdown(101, dirBox, g_params.direction >= 0 ? L"Clockwise" : L"Counter-cw");
    y += g_ui.s(48);

    double speed = g_params.speed;
    slider(102, L"Movement speed", speed, 0.01, 1.2, speedText(speed),
           L"How fast the source travels around you", g_params.mode != Mode::Static);
    g_params.speed = float(speed);

    double radius = g_params.radius;
    slider(103, L"Orbit radius", radius, 0.25, 3.0, metres(radius),
           L"Virtual distance — affects level, tone and room");
    g_params.radius = float(radius);

    double depth = g_params.depth;
    slider(104, L"Effect depth", depth, 0.0, 1.0, pct(depth),
           L"How far through the stereo field it swings");
    g_params.depth = float(depth);

    double smooth = g_params.smoothness;
    slider(105, L"Smoothness", smooth, 0.0, 1.0, pct(smooth),
           L"Rounds off the motion — higher is more gradual");
    g_params.smoothness = float(smooth);

    double manual = g_params.manualAngle * 180.0 / kPi;
    slider(106, L"Manual position", manual, -180, 180, degrees(manual),
           L"Used by the Static position mode", g_params.mode == Mode::Static);
    g_params.manualAngle = float(manual * kPi / 180.0);

    bool pause = g_params.pauseWhenSilent;
    if (g_ui.checkbox(107, { x, y, w, g_ui.s(24) },
                      L"Pause the orbit when nothing plays", pause)) {
        g_params.pauseWhenSilent = pause;
        dirty = true;
    }
    y += g_ui.s(40);

    // ---- character
    section(L"CHARACTER");
    const Rect charBox{ x, y, w, g_ui.s(34) };
    g_ui.dropdown(110, charBox, widen(characterLabel(g_params.character)));
    y += g_ui.s(48);
    double amount = g_params.characterAmount;
    slider(111, L"Character amount", amount, 0.0, 1.0, pct(amount),
           widen(characterHint(g_params.character)).c_str(),
           g_params.character != Character::Clean);
    g_params.characterAmount = float(amount);

    // ---- space
    section(L"SPACE");
    double width = g_params.width;
    slider(120, L"Stereo width", width, 0.0, 2.0, pct(width),
           L"Width of the source before it enters the orbit");
    g_params.width = float(width);

    const double halfW = w * 0.5 - g_ui.s(8);
    auto pair = [&](int idA, const wchar_t* la, double& va, double loa, double hia,
                    const std::wstring& ra,
                    int idB, const wchar_t* lb, double& vb, double lob, double hib,
                    const std::wstring& rb) {
        if (g_ui.slider(idA, { x, y, halfW, g_ui.s(34) }, la, ra, va, loa, hia))
            { g_preset = -1; dirty = true; }
        if (g_ui.slider(idB, { x + w - halfW, y, halfW, g_ui.s(34) }, lb, rb, vb, lob, hib))
            { g_preset = -1; dirty = true; }
        y += g_ui.s(56);
    };
    double dMix = g_params.delayMix, rMix = g_params.reverbMix;
    pair(121, L"Delay", dMix, 0.0, 1.0, pct(dMix),
         124, L"Reverb", rMix, 0.0, 1.0, pct(rMix));
    g_params.delayMix = float(dMix); g_params.reverbMix = float(rMix);

    double dTime = g_params.delayTime, rSize = g_params.reverbSize;
    pair(122, L"Delay time", dTime, 0.04, 1.2, msText(dTime),
         125, L"Room size", rSize, 0.0, 1.0, pct(rSize));
    g_params.delayTime = float(dTime); g_params.reverbSize = float(rSize);

    double dFb = g_params.delayFeedback, rDamp = g_params.reverbDamp;
    pair(123, L"Delay feedback", dFb, 0.0, 0.85, pct(dFb),
         126, L"Damping", rDamp, 0.0, 1.0, pct(rDamp));
    g_params.delayFeedback = float(dFb); g_params.reverbDamp = float(rDamp);

    // ---- output
    section(L"OUTPUT");
    const Rect devBox{ x, y, w - g_ui.s(92), g_ui.s(34) };
    g_ui.dropdown(130, devBox,
                  g_devices.empty() ? L"No output found"
                                    : g_devices[std::min<size_t>(g_device,
                                          g_devices.size() - 1)].name,
                  !g_devices.empty());
    if (g_ui.ghostButton(131, { x + w - g_ui.s(84), y, g_ui.s(84), g_ui.s(34) },
                         L"Refresh")) refreshDevices();
    y += g_ui.s(46);

    bool bypass = !g_params.enabled;
    if (g_ui.checkbox(133, { x, y, w, g_ui.s(34) }, L"Bypass — pass audio through untouched",
                      bypass)) {
        g_params.enabled = !bypass;
        dirty = true;
    }
    y += g_ui.s(48);

    double gain = g_params.outputGain;
    slider(134, L"Output volume", gain, 0.0, 1.5, pct(gain));
    g_params.outputGain = float(gain);

    if (g_ui.ghostButton(135, { x, y, w, g_ui.s(36) }, L"Reset to defaults"))
        resetSettings();
    y += g_ui.s(52);

    g_railHeight = y + g_railScroll - r.y;

    // Menus paint last, and outside the clip, so a long list is never cut off
    // by the bottom of the rail.
    g_ui.popClip();


    std::vector<std::wstring> modeNames;
    for (int i = 0; i < int(Mode::Count); ++i) modeNames.push_back(widen(modeLabel(Mode(i))));
    int pick = g_ui.menuPopup(100, modeBox, modeNames, int(g_params.mode));
    if (pick >= 0) { g_params.mode = Mode(pick); g_preset = -1; dirty = true; }

    pick = g_ui.menuPopup(101, dirBox, { L"Clockwise", L"Counter-cw" },
                          g_params.direction >= 0 ? 0 : 1);
    if (pick >= 0) { g_params.direction = pick == 0 ? 1 : -1; dirty = true; }

    std::vector<std::wstring> charNames;
    for (int i = 0; i < int(Character::Count); ++i)
        charNames.push_back(widen(characterLabel(Character(i))));
    pick = g_ui.menuPopup(110, charBox, charNames, int(g_params.character));
    if (pick >= 0) { g_params.character = Character(pick); g_preset = -1; dirty = true; }

    std::vector<std::wstring> devNames;
    for (const auto& d : g_devices) devNames.push_back(d.name);
    pick = g_ui.menuPopup(130, devBox, devNames, g_device);
    if (pick >= 0) g_device = pick;

    if (dirty) publish();
}

void drawStatus(const Rect& r) {
    std::wstring line;
    Rgb colour = g_ui.theme.inkFaint;

    if (!g_state) {
        line = L"Not installed — run the installer, then reopen this window";
        colour = g_ui.theme.warn;
    } else if (g_state->formatRejected) {
        // Refusing a format is legitimate; refusing it silently is not.
        line = fmt(L"Inactive — this output is %u channels / %u-bit; the effect is "
                   L"32-bit stereo", g_state->rejectedChannels, g_state->rejectedBits);
        colour = g_ui.theme.warn;
    } else if (g_alive) {
        line = L"Processing system audio";
        colour = g_ui.theme.good;
    } else {
        line = L"Waiting — play something";
    }
    g_ui.font(12.5);
    g_ui.text(r.x, r.y + r.h * 0.5, line, colour);
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
    g_ui.begin(dc, &g);
    g_ui.hot = 0;

    g_ui.fillRect({ 0, 0, double(w), double(h) }, g_ui.theme.ground);

    const double top  = g_ui.s(kTopBar);
    const double rail = g_ui.s(kRailWidth);
    drawTopBar({ 0, 0, double(w), top });
    drawStage({ 0, top, double(w) - rail, double(h) - top - g_ui.s(42) });
    drawRail({ double(w) - rail, top, rail, double(h) - top });
    drawStatus({ g_ui.s(kPad), double(h) - g_ui.s(38),
                 double(w) - rail - g_ui.s(kPad) * 2, g_ui.s(24) });

    BitBlt(screen, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);

    g_ui.endFrame();
    if (!g_ui.mouseDown) g_ui.active = 0;
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

    case WM_LBUTTONDOWN:
        g_ui.mouseX = GET_X_LPARAM(lp);
        g_ui.mouseY = GET_Y_LPARAM(lp);
        g_ui.mouseDown = true;
        g_ui.mousePressed = true;
        SetCapture(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
        g_ui.mouseX = GET_X_LPARAM(lp);
        g_ui.mouseY = GET_Y_LPARAM(lp);
        g_ui.mouseDown = false;
        g_ui.mouseReleased = true;
        ReleaseCapture();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSEWHEEL: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        RECT c; GetClientRect(hwnd, &c);
        if (p.x > c.right - g_ui.s(kRailWidth)) {
            const int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            g_railScroll = std::clamp(g_railScroll - notches * g_ui.s(54), 0.0,
                                      std::max(0.0, g_railHeight - (c.bottom - g_ui.s(kTopBar)) + g_ui.s(30)));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_KEYDOWN: {
        int count = 0;
        presets(count);
        if (wp >= '1' && wp <= '9')      applyPreset(int(wp - '1'));
        else if (wp == '0')              applyPreset(9);
        else if (wp == 'B') { g_params.enabled = !g_params.enabled; publish(); }
        else if (wp == 'T') { g_dark = !g_dark; }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        g_ui.dpi = LOWORD(wp) / 96.0f;
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top,
                     r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize.x = int(900 * g_ui.dpi);
        mmi->ptMinTrackSize.y = int(640 * g_ui.dpi);
        return 0;
    }

    case WM_SIZE:       InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_PAINT:      paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:    KillTimer(hwnd, 1); KillTimer(hwnd, 2); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
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
    // title bar's. Letting Windows derive the small one from the large gives a
    // blurry downscale instead of the hand-tuned 16px in the .ico.
    wc.hIcon = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON),
                                  IMAGE_ICON, 0, 0, LR_DEFAULTSIZE));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(IDI_APPICON),
                                    IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                    GetSystemMetrics(SM_CYSMICON), 0));
    wc.lpszClassName = L"EightDMusicWindow";
    RegisterClassExW(&wc);

    g_ui.dpi = GetDpiForSystem() / 96.0f;

    RECT want{ 0, 0, int(kBaseW * g_ui.dpi), int(kBaseH * g_ui.dpi) };
    AdjustWindowRectEx(&want, WS_OVERLAPPEDWINDOW, FALSE, 0);

    // Never open taller than the work area, or the status line and the bottom
    // of the rail end up behind the taskbar on a 1080p screen.
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int maxW = work.right - work.left;
    const int maxH = work.bottom - work.top;
    int winW = std::min<int>(want.right - want.left, maxW);
    int winH = std::min<int>(want.bottom - want.top, maxH);

    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"8D Music", WS_OVERLAPPEDWINDOW,
                             work.left + (maxW - winW) / 2,
                             work.top + (maxH - winH) / 2,
                             winW, winH,
                             nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) { Gdiplus::GdiplusShutdown(gdiToken); CoUninitialize(); return 1; }

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
