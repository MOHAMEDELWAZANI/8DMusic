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
#include <shellapi.h>
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

// Three pages behind one title bar, as on the desktop build.
enum class Page { Studio, About, Account };

Ui           g_ui;
Page         g_page = Page::Studio;
bool         g_accountNote = false;   // said "accounts are not switched on yet"
int          g_guide = -1;            // the guide being read, or -1
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
Rect         g_readoutRect{};
Rect         g_metersRect{};
Rect         g_plusRect{};

// Only the orbit, its readout and the meters actually move.  Everything else is
// rendered once into `g_cacheDc` and blitted, so an animating window repaints a
// handful of shapes per frame instead of the whole page.
//
// Without this the window costs 94% of a core doing nothing: the timer
// invalidates at 30 Hz and a full GDI+ pass -- two 500px radial glows, a lit
// floor, fifteen knobs, every card and every string -- runs each time whether
// anything changed or not.  cpp/src/ui/App.cpp caches for the same reason.
HDC          g_cacheDc = nullptr;
HBITMAP      g_cacheBmp = nullptr, g_cacheOld = nullptr;
int          g_cacheW = 0, g_cacheH = 0;
bool         g_staticDirty = true;
long         g_shownSecond = -1;   // so the progress bar rebuilds once a second
NowPlaying   g_np;
ULONGLONG    g_lastClickMs = 0;
bool         g_tracking = false;   // asked Windows to tell us when the mouse leaves

double S(double v) { return g_ui.s(v); }

// A test hook rather than a feature.  tools/ui_probe.cpp asks the window to
// write down where it has just painted every control, then clicks those exact
// rectangles -- so the harness can never pass against a second copy of the
// layout arithmetic that has quietly drifted from this one.  It costs one file
// write, and only when something asks for it.
// WM_EIGHTD_SYNC draws one frame and does not return until it is done. The
// harness needs that: UpdateWindow() called from another process does not force
// a synchronous WM_PAINT, so without this every check read the state one action
// behind and a click looked as if it had done nothing.
constexpr UINT WM_EIGHTD_SYNC     = WM_APP + 1;
constexpr UINT WM_EIGHTD_DUMPHITS = WM_APP + 2;
bool g_dumpHits = false;

void dumpHits() {
    wchar_t dir[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, dir)) return;
    const std::wstring path = std::wstring(dir) + L"8dmusic-hits.txt";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"w") != 0 || !f) return;
    fprintf(f, "dpi %.4f\n", double(g_ui.dpi));
    fprintf(f, "dark %d\n", g_dark ? 1 : 0);
    fprintf(f, "menu %d\n", g_ui.openMenu);
    for (const auto& h : g_ui.hits)
        fprintf(f, "%d %.2f %.2f %.2f %.2f\n", h.id, h.r.x, h.r.y, h.r.w, h.r.h);
    fclose(f);
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

// Which preset these settings are, if they are one.
//
// The shared block stores the settings, not the name of the preset they came
// from, so on every restart the window read back exactly "Classic 8D" and
// labelled it Custom with no chip lit. Matching by value costs nothing and is
// honest either way: edit one field and it correctly stops being a preset.
int presetMatching(const Params& p) {
    int count = 0;
    const Preset* list = presets(count);
    auto same = [](float a, float b) { return std::fabs(a - b) < 1e-6f; };
    for (int i = 0; i < count; ++i) {
        const Params& w = list[i].p;
        if (p.mode == w.mode && p.character == w.character &&
            same(p.speed, w.speed) && same(p.radius, w.radius) &&
            same(p.depth, w.depth) && same(p.smoothness, w.smoothness) &&
            same(p.width, w.width) && same(p.delayMix, w.delayMix) &&
            same(p.delayTime, w.delayTime) && same(p.delayFeedback, w.delayFeedback) &&
            same(p.reverbMix, w.reverbMix) && same(p.reverbSize, w.reverbSize) &&
            same(p.reverbDamp, w.reverbDamp) &&
            same(p.characterAmount, w.characterAmount))
            return i;
    }
    return -1;
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
                      mix(t.ground, t.text, t.dark ? 0.07 : 0.03),
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
    // the nose: it is what says which way the listener is facing
    g_ui.fillSvg("M235 209c1.6 0 7 8 7 9s-14 1-14 0 5.4-9 7-9z", r, 470, head);
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

    // The mark and the wordmark, exactly as cpp/src/ui/App.cpp draws them: the
    // four bars, the smile and the moving dot, then the name beside it. The
    // lockup PNG is not used here because it carries the name itself, and the
    // two together set "8D Music" twice in one bar.
    g_ui.logo({ r.x + S(20), r.cy() - S(12), S(24), S(24) });
    g_ui.font(11, W700);
    g_ui.tracked(r.x + S(56), r.cy(), L"8D MUSIC", g_ui.theme.dim, S(2));

    // the three pages, as pills
    double tabsHalfWidth = 0;
    {
        struct TabDef { const wchar_t* label; const Icon* ic; Page page; int id; };
        const TabDef tabs[3] = {
            { L"Studio",  &ico::kStudio,  Page::Studio,  101 },
            { L"About",   &ico::kAbout,   Page::About,   102 },
            { L"Account", &ico::kAccount, Page::Account, 103 },
        };
        double widths[3], total = S(8);
        g_ui.font(13.5, W600);
        for (int i = 0; i < 3; ++i) {
            widths[i] = g_ui.textWidth(tabs[i].label) + S(16) + S(7) + S(36);
            total += widths[i] + (i ? S(4) : 0);
        }
        tabsHalfWidth = total * 0.5;
        const Rect bar{ r.cx() - total * 0.5, r.cy() - S(21), total, S(42) };
        g_ui.fillRound(bar, kPill, g_ui.theme.card);
        double x = bar.x + S(4);
        for (int i = 0; i < 3; ++i) {
            const Rect cell{ x, bar.y + S(4), widths[i], S(34) };
            const bool on = g_page == tabs[i].page;
            if (g_ui.click(tabs[i].id, cell)) {
                g_page = tabs[i].page;
                g_ui.openMenu = 0;
                g_guide = -1;
            }
            if (on) g_ui.fillRound(cell, kPill, g_ui.theme.raised);
            else if (g_ui.over(cell)) g_ui.fillRound(cell, kPill, g_ui.theme.well);
            const Rgb c = on ? g_ui.theme.text : g_ui.theme.faint;
            g_ui.icon(*tabs[i].ic, { cell.x + S(16), cell.cy() - S(8), S(16), S(16) }, c);
            g_ui.font(13.5, W600);
            g_ui.text(cell.x + S(16) + S(16) + S(7), cell.cy(), tabs[i].label, c);
            x += widths[i] + S(4);
        }
    }

    // the window's own buttons, at the trailing edge
    const double btn = S(36);
    const Rect closeBtn{ r.x + r.w - S(12) - btn, r.cy() - btn * 0.5, btn, btn };
    const Rect minBtn{ closeBtn.x - S(2) - btn, closeBtn.y, btn, btn };

    // what the effect is doing, and the master switch
    const ApoState apo = apoState();
    g_ui.font(13, W600);
    const double sw = g_ui.textWidth(apo.label);
    g_ui.font(13, W400);
    // Everything left of the switch, minus the tab bar and a gap. A refused
    // format spells out channels and bit depth, which is far too long to sit
    // beside three tab pills, so it is trimmed rather than allowed to collide.
    const double room = (r.cx() - tabsHalfWidth) - S(120) - sw - S(60);
    const std::wstring detail = g_ui.fit(apo.detail, std::max(S(40), room));
    const double dw = g_ui.textWidth(detail);
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
    g_ui.text(pillR.x + S(30) + sw + S(22), pillR.cy(), detail, g_ui.theme.dim);

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
    y = orbit.y + side;

    // In Static the orbit is a control: drag inside it and the source parks
    // where the pointer left it.  Same rule as cpp/src/ui/Studio.cpp -- the
    // angle is measured from the front and the rim is three metres out.
    if (g_params.mode == Mode::Static) {
        g_ui.note(299, orbit);
        if (g_ui.over(orbit) && g_ui.mousePressed) g_ui.active = 299;
        if (g_ui.active == 299 && g_ui.mouseDown) {
            const double dx = g_ui.mouseX - orbit.cx();
            const double dy = g_ui.mouseY - orbit.cy();
            const double scale = orbit.w * 0.417;      // 196/470: the 3 m rim
            g_params.manualAngle = float(std::atan2(dx, -dy));
            g_params.radius = float(std::clamp(
                std::sqrt(dx * dx + dy * dy) / scale * 3.0, 0.25, 3.0));
            g_preset = -1;
            publish();
        }
    }

    // Both of these are live: they are drawn after the cache is blitted, not
    // into it, or the cache would keep a stale angle under the new one.
    g_readoutRect = { x, y + S(10), w - S(144), S(20) };
    g_metersRect  = { x + w - S(130), y + S(10), S(130), S(20) };
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

        // A readout, not a picker.  The desktop build can move its capture to
        // another sink, but here the effect is an APO the installer attached to
        // an endpoint, and the one rule this build does not get to break is
        // that nothing changes the user's output device.  A dropdown here would
        // be a control that cannot do what it looks like it does -- and it did
        // not: refreshDevices() put the choice back two seconds later.
        g_ui.font(13, W400);
        // "Default output", not "Output": the effect runs on whichever endpoint
        // the installer attached it to, which is not always the default one.
        g_ui.text(x + S(kCardPad), ry + rowH * 0.5, L"Default output", t.text);
        {
            const Rect box{ x + colW - S(kCardPad) - S(178), ry, S(178), rowH + S(4) };
            g_ui.fillRound(box, kPill, t.well);
            g_ui.font(12.5, W500);
            g_ui.text(box.x + S(14), box.cy(),
                      g_ui.fit(g_devices.empty()
                                   ? L"No output found"
                                   : g_devices[std::min<size_t>(
                                         g_device, g_devices.size() - 1)].name,
                               box.w - S(28)),
                      g_devices.empty() ? t.ghost : t.dim);
        }
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
        // Clipped to the cover, as the cairo build clips them: unclipped they
        // bloom out across the dock and the artwork loses its edge.
        g_ui.pushClipShape(cover, S(15));
        g_ui.glow(cover.x + cover.w * 0.30, cover.y + cover.h * 0.25,
                  cover.w * 0.62, Rgb::hex(0xFFB36B), 0.85);
        g_ui.glow(cover.x + cover.w * 0.80, cover.y + cover.h * 0.85,
                  cover.w * 0.62, Rgb::hex(0xFF458E), 0.85);
        g_ui.popClipShape();
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
    // Only when something is actually wrong.  "Waiting for audio" is already
    // spelled out in the title bar, and printing it twice on one screen reads
    // as a fault rather than as the idle state it is.
    const bool wrong = !g_state || g_state->formatRejected != 0;
    if (wrong) {
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

// ------------------------------------------------------------------- pages
//
// About and Account, ported from cpp/src/ui/Pages.cpp. Two pages that do not
// move: what this is, where to find it, and the one thing an account would be
// for.
//
// Two differences from the desktop build, both because the thing behind them
// does not exist here. It has a Presentations card that replays the welcome
// flow and the studio tour -- Windows has neither -- and its first guide
// explains a PipeWire virtual sink, where this one has to explain an APO.

constexpr double kPagePad = 22, kPageGap = 18, kPageLeftW = 520;
const wchar_t* kRepoUrl = L"https://github.com/MOHAMEDELWAZANI/8DMusic";

void openUrl(const std::wstring& url) {
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

// --- guides -----------------------------------------------------------------

struct GuidePoint { const wchar_t* title; const wchar_t* body; };
struct GuideDef {
    const wchar_t* kicker; const wchar_t* title; const wchar_t* intro;
    GuidePoint points[5];
};

const GuideDef kGuides[4] = {
    { L"SETUP", L"How the effect gets into your audio",
      L"8D Music installs an Audio Processing Object — a small piece of code Windows "
      L"loads inside audiodg.exe, the process that mixes everything you hear. Nothing "
      L"is routed by hand and no output device changes.",
      {{ L"It sits on one endpoint",
         L"The installer attaches the effect to an output device and Windows loads it "
         L"from then on. The ENDPOINT card says whether it is in the path right now." },
       { L"Nothing to start",
         L"There is no capture to begin. When audio plays to that endpoint the effect "
         L"runs; when it stops, audiodg stops calling it and the orbit parks." },
       { L"It survives this window",
         L"Settings live in a shared file the effect reads, so closing this window "
         L"changes nothing about the sound." },
       { nullptr, nullptr }, { nullptr, nullptr }}},

    { L"THE EFFECT", L"How 8D works",
      L"The sound is treated as a source orbiting your head. Rather than swinging the "
      L"stereo balance left and right, the position is turned into the cues a real "
      L"sound would produce.",
      {{ L"Time between the ears",
         L"The far ear hears the sound up to about 0.7 ms later. This is what pushes the "
         L"image outside your head instead of leaving it stuck between your ears." },
       { L"Level and head shadow",
         L"Constant-power panning keeps loudness steady as the source travels, and your "
         L"skull blocks high frequencies, so the far ear gets a gentle treble roll-off." },
       { L"Front and back",
         L"Positions behind you lose a little upper-mid, the way the outer ear shapes "
         L"sound arriving from the rear." },
       { L"Distance",
         L"Level, air absorption and how much reverb is sent all follow the orbit "
         L"radius — the Distance knob in Studio." },
       { nullptr, nullptr }}},

    { L"LISTENING", L"Why headphones",
      L"8D works by giving each ear its own version of the sound: slightly different "
      L"timing, level and tone.",
      {{ L"Speakers undo it",
         L"They send both versions to both ears, which mixes them back together and "
         L"cancels the effect. Any headphones or earbuds work — they do not need to "
         L"be expensive, or to advertise spatial audio of their own." },
       { L"Turn other spatial effects off",
         L"Two effects fighting each other sound worse than either alone. If your "
         L"headphones have a spatial mode of their own, switch it off." },
       { nullptr, nullptr }, { nullptr, nullptr }, { nullptr, nullptr }}},

    { L"LIMITS", L"An app shows “Nothing playing”",
      L"The panel in Studio reads what a player publishes to the Windows media session "
      L"— the same place the volume-key flyout reads. Spotify publishes a title, and "
      L"browsers report whatever the page declares.",
      {{ L"Some apps publish nothing",
         L"Games and most chat apps say nothing at all. They read as “Nothing "
         L"playing” while you can plainly hear them. That is the boundary of the "
         L"session API, not a fault in the app." },
       { L"The effect still applies",
         L"Whether the title shows has nothing to do with whether the sound is "
         L"spatialised — that depends only on which endpoint the audio goes to." },
       { L"Exclusive mode",
         L"An app that takes the endpoint in exclusive mode bypasses the whole effect "
         L"chain, including us. Set it to share the device and it comes back." },
       { nullptr, nullptr }, { nullptr, nullptr }}},
};

void drawGuide(const Rect& full) {
    const GuideDef& g = kGuides[std::clamp(g_guide, 0, 3)];
    const Theme& t = g_ui.theme;

    g_ui.fillRect(full, Rgb{ 0, 0, 0 }, 0.55);
    const double w = std::min(S(760), full.w - S(80));
    const double h = std::min(S(660), full.h - S(60));
    const Rect dlg{ full.cx() - w * 0.5, full.y + (full.h - h) * 0.5, w, h };
    g_ui.shadow(dlg, S(22), S(26), 0.6, S(12));
    g_ui.fillRound(dlg, S(22), t.card);

    const double x = dlg.x + S(34), tw = dlg.w - S(68);
    double y = dlg.y + S(30);
    g_ui.font(11, W700);
    g_ui.tracked(x, y + S(6), g.kicker, t.accent, S(2));
    y += S(24);
    g_ui.font(28, W800);
    y += g_ui.flow(x, y, tw, g.title, t.text, 1.15) + S(14);
    g_ui.font(14, W400);
    y += g_ui.flow(x, y, tw, g.intro, t.dim, 1.55) + S(16);

    for (const GuidePoint& p : g.points) {
        if (!p.title) break;
        g_ui.font(13.5, W400);
        const double bodyH = g_ui.flowHeight(tw - S(52), p.body, 1.5);
        const double ph = S(14) + S(18) + S(4) + bodyH + S(14);
        g_ui.fillRound({ x, y, tw, ph }, S(16), t.well);
        g_ui.circle(x + S(20), y + S(23), S(4), t.accent);
        g_ui.font(13.5, W700);
        g_ui.text(x + S(34), y + S(23), p.title, t.text);
        g_ui.font(13.5, W400);
        g_ui.flow(x + S(34), y + S(30), tw - S(52), p.body, t.dim, 1.5);
        y += ph + S(10);
    }

    const Rect close{ dlg.x + dlg.w - S(34) - S(96), dlg.y + dlg.h - S(26) - S(42),
                      S(96), S(42) };
    if (g_ui.pill(560, close, L"Close", t.text, t.ground, 14)) g_guide = -1;
}

// --- about ------------------------------------------------------------------

void drawAbout(const Rect& body) {
    const Theme& t = g_ui.theme;
    const double top = body.y + S(18);
    const double bottom = body.y + body.h - S(16);
    const Rect left{ body.x + S(kPagePad), top, S(kPageLeftW), bottom - top };
    const Rect right{ left.x + S(kPageLeftW) + S(kPageGap), top,
                      body.w - S(kPagePad) * 2 - S(kPageLeftW) - S(kPageGap),
                      bottom - top };

    double y = left.y;

    // hero
    {
        const double markSide = S(132);
        const std::wstring headline = L"Real-time 8D for everything your computer plays";
        const double tx = left.x + S(22) + markSide + S(22);
        const double tw = left.x + left.w - S(22) - tx;
        g_ui.font(24, W600, true);
        const double th = g_ui.flowHeight(tw, headline, 1.2);
        const double h = std::max(markSide + S(44), S(42) + th + S(16) + S(28) + S(22));

        g_ui.fillRound({ left.x, y, left.w, h }, S(kCardR), t.card);
        const Rect mark{ left.x + S(22), y + (h - markSide) * 0.5, markSide, markSide };
        g_ui.glow(mark.cx(), mark.cy(), S(96), t.accent, t.dark ? 0.14 : 0.08);
        g_ui.ringDashed(mark.cx(), mark.cy(), S(62), t.line, 1, 1.0, S(1), S(7));
        g_ui.circle(mark.cx(), mark.cy(), S(46), t.well);
        g_ui.ring(mark.cx(), mark.cy(), S(46), t.accent, 1, 0.25);
        g_ui.logo({ mark.cx() - S(31), mark.cy() - S(31), S(62), S(62) });

        g_ui.font(10.5, W700);
        g_ui.tracked(tx, y + S(30), L"8D MUSIC", t.faint, S(3));
        g_ui.font(24, W600, true);
        g_ui.flow(tx, y + S(42), tw, headline, t.text, 1.2);
        const double chipY = y + S(42) + th + S(16);
        const wchar_t* chips[3] = { L"Version 1.0", L"Linux · Windows", L"Open source" };
        double cx = tx;
        for (int i = 0; i < 3; ++i) {
            g_ui.font(11.5, i == 2 ? W600 : W400);
            const Rect c{ cx, chipY, g_ui.textWidth(chips[i]) + S(26), S(28) };
            g_ui.fillRound(c, kPill, i == 2 ? mix(t.card, t.motion, 0.18) : t.well);
            g_ui.text(c.cx(), c.cy(), chips[i],
                      i == 2 ? mix(t.motion, t.text, 0.35) : t.dim, Align::Centre);
            cx += c.w + S(7);
        }
        y += h + S(14);
    }

    // our story
    {
        const double h = S(160);
        g_ui.fillRound({ left.x, y, left.w, h }, S(kCardR), t.card);
        const double x = left.x + S(18), w = left.w - S(36);
        g_ui.font(10.5, W700);
        g_ui.tracked(x, y + S(22), L"OUR STORY", t.accent, S(2));
        g_ui.font(17, W700);
        double used = g_ui.flow(x, y + S(33), w,
            L"One effect, written once in C++, sounding the same on Linux, "
            L"Windows and Android.", t.text, 1.35);
        used += S(9);
        g_ui.font(13, W400);
        g_ui.flow(x, y + S(33) + used, w,
            L"No uploading, no converting. Spotify, browsers, games and your own "
            L"files are spatialised live on their way to your headphones.", t.dim, 1.55);

        const Rect link{ x, y + h - S(34), S(160), S(24) };
        if (g_ui.click(501, link)) openUrl(std::wstring(kRepoUrl) + L"#readme");
        g_ui.font(13, W700);
        g_ui.text(link.x, link.cy(), L"Read the full story", t.accent);
        g_ui.icon(ico::kChevron,
                  { link.x + g_ui.textWidth(L"Read the full story") + S(6),
                    link.cy() - S(7), S(14), S(14) }, t.accent);
        y += h + S(14);
    }

    // what's new
    {
        const double h = S(168);
        g_ui.fillRound({ left.x, y, left.w, h }, S(kCardR), t.card);
        const double x = left.x + S(18), w = left.w - S(36);
        g_ui.font(10.5, W700);
        g_ui.tracked(x, y + S(22), L"WHAT'S NEW IN 1.0", t.accent, S(2));
        const Rect log{ left.x + left.w - S(18) - S(110), y + S(12), S(110), S(20) };
        if (g_ui.click(502, log)) openUrl(std::wstring(kRepoUrl) + L"/releases");
        g_ui.font(11.5, W400);
        g_ui.text(log.x + log.w - S(16), log.cy(), L"Full changelog", t.faint, Align::Right);
        g_ui.icon(ico::kExternal, { log.x + log.w - S(12), log.cy() - S(6), S(12), S(12) },
                  t.faint);

        const wchar_t* bullets[3] = {
            L"<b>One window, every control.</b> Movement, space, echo, character and "
            L"EQ all sit beside the orbit — nothing is a page away any more.",
            L"<b>Presets you can A/B.</b> Save a setting, recall it in one click, and "
            L"press B to hear the track dry.",
            L"<b>The same engine as the phone.</b> One C++20 core, so a preset sounds "
            L"identical on Linux, Windows and Android.",
        };
        double by = y + S(38);
        for (int i = 0; i < 3; ++i) {
            g_ui.font(12.5, W400);
            g_ui.text(x + S(2), by + S(9), L"·", t.accent);
            by += g_ui.flow(x + S(12), by, w - S(12), bullets[i], t.dim, 1.5) + S(8);
        }
        y += h + S(14);
    }

    // support, pinned to the foot of the column
    {
        const double h = S(92);
        const Rect c{ left.x, left.y + left.h - h, left.w, h };
        g_ui.fillRound(c, S(kCardR), t.card);
        g_ui.linearRound(c, S(kCardR), t.motion, 0.22, 0.0);

        g_ui.glow(c.x + S(44), c.cy(), S(46), t.motion, 0.35);
        g_ui.circle(c.x + S(44), c.cy(), S(24), t.motion);
        g_ui.icon(ico::kHeart, { c.x + S(33), c.cy() - S(11), S(22), S(22) },
                  Rgb::hex(0xFFFFFF));
        const Rect b{ c.x + c.w - S(20) - S(112), c.cy() - S(20), S(112), S(40) };
        g_ui.font(15, W700);
        g_ui.text(c.x + S(84), c.cy() - S(20), L"Support 8D Music", t.text);
        g_ui.font(12.5, W400);
        g_ui.flow(c.x + S(84), c.cy() - S(12), b.x - S(14) - (c.x + S(84)),
                  L"Free, no ads, open source. A donation keeps it that way.",
                  t.dim, 1.45);
        if (g_ui.pill(503, b, L"Donate", t.text, t.ground, 14)) openUrl(kRepoUrl);
    }

    // ---- what you can do from here --------------------------------------
    y = right.y;

    // guides
    {
        g_ui.font(19, W600, true);
        g_ui.text(right.x, y + S(13), L"Guides", t.text);
        y += S(37);

        struct Row { const Icon* ic; bool tinted; const wchar_t* title; const wchar_t* sub; };
        const Row rows[4] = {
            { &ico::kMonitor, true, L"How the effect gets in",
              L"An APO inside audiodg, and nothing rerouted" },
            { &ico::kStudio, false, L"How 8D works",
              L"The five cues that move sound around you" },
            { &ico::kHeadphones, false, L"Why headphones",
              L"8D needs each ear to hear its own side" },
            { &ico::kClock, false, L"An app shows “Nothing playing”",
              L"Exclusive-mode apps and what to do about them" },
        };
        const double h = 4 * S(58) + S(8);
        g_ui.fillRound({ right.x, y, right.w, h }, S(kCardR), t.card);
        for (int i = 0; i < 4; ++i) {
            const Rect row{ right.x + S(14), y + S(4) + S(58) * i, right.w - S(28), S(58) };
            if (i) g_ui.fillRect({ row.x + S(4), row.y, row.w - S(8), 1 }, t.text, 0.05);
            if (g_ui.listRow(520 + i, row, *rows[i].ic,
                             rows[i].tinted ? t.accent : t.dim,
                             rows[i].tinted ? mix(t.card, t.accent, 0.12) : t.well,
                             rows[i].title, rows[i].sub, ico::kChevron))
                g_guide = i;
        }
        y += h + S(14);
    }

    // get involved, pinned above the footer
    {
        const double h = 3 * S(58) + S(8);
        y = right.y + right.h - S(28) - h;
        g_ui.font(19, W600, true);
        g_ui.text(right.x, y - S(24), L"Get involved", t.text);
        g_ui.fillRound({ right.x, y, right.w, h }, S(kCardR), t.card);

        const Rect r0{ right.x + S(14), y + S(4), right.w - S(28), S(58) };
        if (g_ui.listRow(530, r0, ico::kGithub, t.ground, t.text, L"Source code",
                         L"github.com/MOHAMEDELWAZANI/8DMusic", ico::kExternal))
            openUrl(kRepoUrl);
        const Rect r1{ right.x + S(14), y + S(4) + S(58), right.w - S(28), S(58) };
        g_ui.fillRect({ r1.x + S(4), r1.y, r1.w - S(8), 1 }, t.text, 0.05);
        if (g_ui.listRow(531, r1, ico::kGlobe, t.dim, t.well, L"Website",
                         L"News, releases and the mobile build", ico::kExternal))
            openUrl(kRepoUrl);
        const Rect r2{ right.x + S(14), y + S(4) + S(116), right.w - S(28), S(58) };
        (void)0;
        g_ui.fillRect({ r2.x + S(4), r2.y, r2.w - S(8), 1 }, t.text, 0.05);
        if (g_ui.listRow(532, r2, ico::kUser, t.accent, mix(t.card, t.accent, 0.12),
                         L"Help build 8D Music",
                         L"Sign in to test early versions — that is the Account tab",
                         ico::kChevron))
            g_page = Page::Account;
    }

    // the footer
    {
        const double fy = right.y + right.h - S(6);
        g_ui.icon(ico::kLock, { right.x, fy - S(7), S(13), S(13) }, t.ghost);
        g_ui.font(11.5, W400);
        g_ui.text(right.x + S(20), fy,
                  L"Your audio never leaves this computer · 8D Music 1.0 · "
                  L"C++20 DSP engine · GDI+ UI", t.ghost);
    }
}

// --- account ----------------------------------------------------------------

void drawAccount(const Rect& body) {
    const Theme& t = g_ui.theme;
    g_ui.glow(body.cx(), body.y + body.h * 0.42, S(420), t.accent, t.dark ? 0.14 : 0.08);
    g_ui.glow(body.cx() + S(90), body.y + body.h * 0.22, S(320), t.motion,
              t.dark ? 0.10 : 0.06);

    const double cx = body.cx();
    double y = body.y + body.h * 0.5 - S(214);

    // the empty seat
    g_ui.ringDashed(cx, y + S(48), S(45), t.line, 1, 1.0, S(1), S(7));
    g_ui.circle(cx, y + S(48), S(34), t.card);
    g_ui.icon(ico::kUser, { cx - S(17), y + S(31), S(34), S(34) }, t.faint);
    y += S(112);

    g_ui.font(27, W600, true);
    g_ui.text(cx, y + S(17), L"You are not signed in", t.text, Align::Centre);
    y += S(46);

    g_ui.font(14, W400);
    const double bw = S(470);
    g_ui.flow(cx - bw * 0.5, y, bw,
              L"8D Music works fully without an account. Signing in is only for testing "
              L"early builds and keeping your presets on every machine.",
              t.dim, 1.55, true, Align::Centre);
    y += S(70);

    const double bwid = S(430);
    const Rect google{ cx - bwid * 0.5, y, bwid, S(52) };
    if (g_ui.click(600, google)) g_accountNote = true;
    g_ui.fillRound(google, kPill,
                   g_ui.over(google) ? mix(t.text, t.ground, 0.08) : t.text);
    {
        g_ui.font(15, W600);
        const double lw = g_ui.textWidth(L"Continue with Google");
        const double gx = google.cx() - (lw + S(11) + S(19)) * 0.5;
        // Google's mark, in its own colours
        struct Wedge { uint32_t colour; const char* d; };
        static const Wedge kG[4] = {
            { 0x4285F4, "M23 12.3c0-.8-.1-1.6-.2-2.3H12v4.5h6.2a5.3 5.3 0 0 1-2.3 3.5v2.9h3.7c2.2-2 3.4-5 3.4-8.6z" },
            { 0x34A853, "M12 23.5c3.1 0 5.7-1 7.6-2.8l-3.7-2.9c-1 .7-2.3 1.1-3.9 1.1-3 0-5.5-2-6.4-4.7H1.8v3C3.7 20.9 7.6 23.5 12 23.5z" },
            { 0xFBBC05, "M5.6 14.2a6.9 6.9 0 0 1 0-4.4v-3H1.8a11.5 11.5 0 0 0 0 10.4l3.8-3z" },
            { 0xEA4335, "M12 5.1c1.7 0 3.2.6 4.4 1.7l3.3-3.3C17.7 1.6 15.1.5 12 .5 7.6.5 3.7 3.1 1.8 6.8l3.8 3c.9-2.7 3.4-4.7 6.4-4.7z" },
        };
        for (const auto& wdg : kG)
            g_ui.fillSvg(wdg.d, { gx, google.cy() - S(9.5), S(19), S(19) }, 24,
                         Rgb::hex(wdg.colour));
        g_ui.font(15, W600);
        g_ui.text(gx + S(19) + S(11), google.cy(), L"Continue with Google", t.ground);
    }
    y += S(62);

    const Rect gh{ cx - bwid * 0.5, y, bwid, S(52) };
    if (g_ui.click(601, gh)) g_accountNote = true;
    g_ui.fillRound(gh, kPill, g_ui.over(gh) ? mix(t.card, t.text, 0.08) : t.card);
    {
        g_ui.font(15, W600);
        const double lw = g_ui.textWidth(L"Continue with GitHub");
        const double gx = gh.cx() - (lw + S(11) + S(20)) * 0.5;
        g_ui.icon(ico::kGithub, { gx, gh.cy() - S(10), S(20), S(20) }, t.text);
        g_ui.text(gx + S(20) + S(11), gh.cy(), L"Continue with GitHub", t.text);
    }
    y += S(64);

    g_ui.font(13, W400);
    g_ui.text(cx, y + S(10),
              g_accountNote
                  ? L"Accounts are not switched on yet — the app works without one."
                  : L"Keep using 8D Music without an account",
              g_accountNote ? t.warn : t.faint, Align::Centre);

    // What this page becomes, once we have talked about it.
    const double sy = body.y + body.h - S(62);
    g_ui.line(0, sy, body.w, sy, t.text, 1, 0.10);

    g_ui.font(10.5, W700);
    const Rect tag{ S(22), sy + S(16),
                    g_ui.trackedWidth(L"TO DESIGN NEXT", S(1.4)) + S(26), S(24) };
    g_ui.fillRound(tag, kPill, mix(t.ground, t.motion, 0.16));
    g_ui.tracked(tag.cx(), tag.cy(), L"TO DESIGN NEXT", mix(t.motion, t.text, 0.35),
                 S(1.4), Align::Centre);

    const wchar_t* stubs[4] = { L"Profile & plan", L"Presets synced across devices",
                                L"Early builds / beta channel", L"Contributor dashboard" };
    const double stubX = tag.x + tag.w + S(12);
    const double stubW = (body.w - S(22) - stubX - S(8) * 3) / 4.0;
    for (int i = 0; i < 4; ++i) {
        const Rect st{ stubX + (stubW + S(8)) * i, sy + S(13), stubW, S(34) };
        g_ui.strokeRound(st, S(12), t.text, 1, 0.12);
        g_ui.font(11.5, W400);
        g_ui.text(st.cx(), st.cy(), g_ui.fit(stubs[i], st.w - S(12)), t.ghost,
                  Align::Centre);
    }
}

// Everything that stays put.  Runs only when something has actually changed --
// which includes every input event, because the widgets handle input as they
// draw.
void drawChrome(int w, int h) {
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

    // Only Studio animates, so only Studio leaves anything for the live pass.
    // Clearing these on the other pages stops a stale orbit being painted over
    // About or Account.
    if (g_page != Page::Studio) {
        g_orbitRect = {}; g_readoutRect = {}; g_metersRect = {};
    }

    switch (g_page) {
    case Page::Studio:
        drawStage({ body.x, body.y, S(kStageW), body.h });
        drawRail({ body.x + S(kStageW), body.y, body.w - S(kStageW), body.h }, dirty);
        drawNowPlaying({ body.x + S(kDockPad),
                         body.y + body.h - S(kDockBottom) - S(kDockH),
                         body.w - S(kDockPad) * 2, S(kDockH) });
        drawStatus({ 0, double(h) - S(kDockBottom) + S(10), double(w), S(20) });
        break;
    case Page::About:   drawAbout(body);   break;
    case Page::Account: drawAccount(body); break;
    }

    // Menus paint last so their lists sit above the page.
    if (g_page == Page::Studio) {
        int countPresets = 0;
        const Preset* list = presets(countPresets);
        std::vector<std::wstring> names;
        for (int i = 0; i < countPresets; ++i) names.push_back(widen(list[i].name));
        const int pick = g_ui.menuPopup(240, g_plusRect, names, g_preset, 190);
        if (pick >= 0) applyPreset(pick);
    }

    // A guide is a dialog: it owns the window while it is up.
    if (g_guide >= 0) drawGuide({ 0, 0, double(w), double(h) });

    if (dirty) publish();
}

void paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC screen = BeginPaint(hwnd, &ps);
    RECT client;
    GetClientRect(hwnd, &client);
    const int w = client.right, h = client.bottom;

    g_ui.theme = g_dark ? Theme::darkTheme() : Theme::light();
    g_ui.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    g_ui.viewW = w;
    g_ui.viewH = h;

    if (!g_cacheDc || g_cacheW != w || g_cacheH != h) {
        if (g_cacheDc) {
            SelectObject(g_cacheDc, g_cacheOld);
            DeleteObject(g_cacheBmp);
            DeleteDC(g_cacheDc);
        }
        g_cacheDc = CreateCompatibleDC(screen);
        g_cacheBmp = CreateCompatibleBitmap(screen, w, h);
        g_cacheOld = static_cast<HBITMAP>(SelectObject(g_cacheDc, g_cacheBmp));
        g_cacheW = w; g_cacheH = h;
        g_staticDirty = true;
    }

    if (g_staticDirty) {
        // Cleared *before* the pass, not after: the widgets change state as they
        // draw -- a preset applied, a menu picked -- and anything they set has
        // to survive into the next frame rather than be wiped by this one.
        g_staticDirty = false;
        Gdiplus::Graphics g(g_cacheDc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g_ui.begin(g_cacheDc, &g);
        g_ui.hot = 0;
        drawChrome(w, h);
        g_ui.endFrame();
        if (g_dumpHits) { g_dumpHits = false; dumpHits(); }
        if (g_staticDirty) InvalidateRect(hwnd, nullptr, FALSE);
    }

    // Double buffered: the orbit repaints many times a second and a flickering
    // window reads as a broken one.
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(dc, bmp));
    BitBlt(dc, 0, 0, w, h, g_cacheDc, 0, 0, SRCCOPY);

    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g_ui.use(dc, &g);
        if (g_orbitRect.w > 0) drawOrbitLive(g_orbitRect);
        if (g_readoutRect.w > 0) drawReadout(g_readoutRect);
        if (g_metersRect.w > 0) drawMeters(g_metersRect);
    }

    BitBlt(screen, 0, 0, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
    EndPaint(hwnd, &ps);
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
            const bool wasAlive = g_alive;
            if (g_state) {
                const LONG beat = g_state->heartbeat;
                if (beat != g_lastBeat) { g_alive = true; g_staleTicks = 0; }
                else if (++g_staleTicks > 8) g_alive = false;
                g_lastBeat = beat;
            }
            // The state pill, the ENDPOINT row and the status line all read
            // from this, so a change is a change to the chrome.
            if (g_alive != wasAlive) g_staticDirty = true;

            // The progress bar and its clock move a pixel a second, so the
            // chrome is rebuilt when the displayed second changes, not per frame.
            if (g_np.has()) {
                const Track t = g_np.track();
                const double now = std::chrono::duration<double>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                const long second = long(t.at(now));
                if (second != g_shownSecond) { g_shownSecond = second; g_staticDirty = true; }
            } else if (g_shownSecond != -1) {
                g_shownSecond = -1; g_staticDirty = true;
            }

            // Nothing playing and no meter still falling means nothing on this
            // page is moving, and a repaint would draw the identical frame.
            const bool animating = g_alive || g_ui.active != 0 ||
                                   g_meterL > 0.002 || g_meterR > 0.002;
            if (animating || g_staticDirty) InvalidateRect(hwnd, nullptr, FALSE);
        } else if (wp == 2) {
            const size_t before = g_devices.size();
            const int was = g_device;
            refreshDevices();
            if (before != g_devices.size() || was != g_device) {
                g_staticDirty = true;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;

    // Both of these run on the window's own thread, so UpdateWindow here really
    // does paint before returning.
    case WM_EIGHTD_SYNC:
        g_staticDirty = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        return 0;

    case WM_EIGHTD_DUMPHITS:
        g_dumpHits = true;
        g_staticDirty = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        g_staticDirty = true;
        g_ui.mouseX = GET_X_LPARAM(lp);
        g_ui.mouseY = GET_Y_LPARAM(lp);
        // Windows does not say when the pointer leaves unless it is asked, and
        // without asking, whatever was last under it stays lit for as long as
        // the window is open -- a tile or knob glowing at nothing.
        if (!g_tracking) {
            TRACKMOUSEEVENT t{ sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0 };
            g_tracking = TrackMouseEvent(&t) != 0;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_MOUSELEAVE:
        g_staticDirty = true;
        g_tracking = false;
        // Not while a control is being dragged: capture keeps the pointer ours
        // even outside the window, and a knob must not let go mid-turn.
        if (!g_ui.mouseDown) { g_ui.mouseX = -1e6; g_ui.mouseY = -1e6; }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        g_staticDirty = true;
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
        g_staticDirty = true;
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
    //
    // The answer comes from the rectangles the last frame actually painted, not
    // from `hot`.  Asking `hot` looks like it should work and cannot: Windows
    // hit-tests *before* it delivers the move, and the moment this says
    // HTCAPTION it stops sending WM_MOUSEMOVE at all and sends WM_NCMOUSEMOVE
    // instead -- so the pointer position never updates over the bar, nothing up
    // there ever becomes hot, and the answer stays HTCAPTION for good.  Close,
    // minimise and the 8D switch were all unclickable, and pressing any of them
    // dragged the window.
    case WM_NCHITTEST: {
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        if (p.y >= S(kTopBar) || g_ui.openMenu != 0) return HTCLIENT;
        for (const auto& h : g_ui.hits)
            if (h.r.contains(p.x, p.y)) return HTCLIENT;
        return HTCAPTION;
    }

    case WM_MOUSEWHEEL: {
        g_staticDirty = true;
        POINT p{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(hwnd, &p);
        g_ui.mouseX = p.x;
        g_ui.mouseY = p.y;
        g_ui.wheel = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        g_staticDirty = true;
        // A guide owns the keyboard while it is up, and the presets belong to
        // Studio -- typing 3 on the About page should not silently retune the
        // effect behind it.
        if (g_guide >= 0) {
            if (wp == VK_ESCAPE || wp == VK_RETURN) g_guide = -1;
        } else if (wp == 'T') {
            g_dark = !g_dark;
        } else if (wp == VK_ESCAPE) {
            g_ui.openMenu = 0;
        } else if (g_page == Page::Studio) {
            if (wp >= '1' && wp <= '9')      applyPreset(int(wp - '1'));
            else if (wp == '0')              applyPreset(9);
            else if (wp == 'B') { g_params.enabled = !g_params.enabled; publish(); }
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DPICHANGED: {
        g_ui.dpi = LOWORD(wp) / 96.0f;
        SetWindowPos(hwnd, nullptr, 0, 0,
                     int(kBaseW * g_ui.dpi), int(kBaseH * g_ui.dpi),
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE);
        applyRoundedCorners(hwnd);
        g_staticDirty = true;
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_SIZE:       applyRoundedCorners(hwnd);
                        g_staticDirty = true;
                        InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_PAINT:      paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DESTROY:
        KillTimer(hwnd, 1); KillTimer(hwnd, 2);
        if (g_cacheDc) {
            SelectObject(g_cacheDc, g_cacheOld);
            DeleteObject(g_cacheBmp);
            DeleteDC(g_cacheDc);
            g_cacheDc = nullptr;
        }
        PostQuitMessage(0);
        return 0;
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
    // The last two are the source tree, from build\Release\ and from build\.
    // The three-dot one is not a typo and it is the one that matters: with only
    // two, the search landed in windows\build\cpp\ instead of the repo's cpp\,
    // the file was never found, and the whole interface quietly fell back to
    // Segoe UI -- which looks nothing like the design.
    const wchar_t* tries[] = { L"Figtree.ttf",
                               L"assets\\fonts\\Figtree.ttf",
                               L"..\\..\\..\\cpp\\assets\\fonts\\Figtree.ttf",
                               L"..\\..\\cpp\\assets\\fonts\\Figtree.ttf" };
    for (const wchar_t* t : tries) {
        const std::wstring path = dir + t;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return path;
    }
    return L"";
}

} // namespace

int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    // One window, not one per double-click.  Two copies would both hold the
    // shared block open and both write to it, so whichever painted last would
    // win and the other would silently undo the user's changes.  A second
    // launch brings the first one forward instead, which is what clicking the
    // shortcut again is asking for anyway.
    //
    // Local\ rather than Global\: this is per-user, and a plain interactive
    // account has no SeCreateGlobalPrivilege to make a global name with.
    HANDLE only = CreateMutexW(nullptr, TRUE, L"Local\\8DMusic.SingleInstance");
    if (only && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND running = FindWindowW(L"EightDMusicWindow", nullptr)) {
            if (IsIconic(running)) ShowWindow(running, SW_RESTORE);
            SetForegroundWindow(running);
        }
        CloseHandle(only);
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Gdiplus::GdiplusStartupInput gdiIn;
    ULONG_PTR gdiToken = 0;
    Gdiplus::GdiplusStartup(&gdiToken, &gdiIn, nullptr);

    // The installer creates the block: audiodg lives in session 0, so it has to
    // cross sessions, and a non-elevated process has no SeCreateGlobalPrivilege
    // with which to make a Global\ object. See SharedState.h.
    g_state = openSharedState(true);
    if (g_state) {
        g_params = g_state->params;
        g_preset = presetMatching(g_params);
        publish();
    }

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
    closeSharedState();
    if (only) { ReleaseMutex(only); CloseHandle(only); }
    Gdiplus::GdiplusShutdown(gdiToken);
    CoUninitialize();
    return 0;
}
