// The same immediate-mode widget set the desktop build uses, drawn with GDI+.
//
// cpp/src/ui/Widgets.h is cairo, so it cannot be compiled here -- but every
// widget in it is reproduced below with the same geometry, the same radii and
// the same colour roles, so the two builds look like one product rather than
// two interpretations of it. The colour tokens themselves are not copied: they
// come from cpp/src/ui/Theme.h, compiled in place.
//
// GDI+ rather than plain GDI because the orbit is circles and rounded
// rectangles all the way down, and GDI does not anti-alias either.
#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "../../../cpp/src/ui/Theme.h"

namespace eightd {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    Rect inset(double d) const { return { x + d, y + d, w - d * 2, h - d * 2 }; }
};

enum class Align { Left, Centre, Right };

inline Gdiplus::Color gp(const Rgb& c, double alpha = 1.0) {
    auto q = [](double v) -> BYTE {
        const int i = static_cast<int>(v * 255.0 + 0.5);
        return static_cast<BYTE>(i < 0 ? 0 : (i > 255 ? 255 : i));
    };
    return Gdiplus::Color(q(alpha), q(c.r), q(c.g), q(c.b));
}
inline COLORREF cref(const Rgb& c) {
    auto q = [](double v) -> int {
        const int i = static_cast<int>(v * 255.0 + 0.5);
        return i < 0 ? 0 : (i > 255 ? 255 : i);
    };
    return RGB(q(c.r), q(c.g), q(c.b));
}

class Ui {
public:
    Theme theme = Theme::darkTheme();

    // Pointer state for the frame being drawn.
    double mouseX = -1, mouseY = -1;
    bool   mouseDown = false, mousePressed = false, mouseReleased = false;
    int    wheel = 0;

    int hot = 0, active = 0, openMenu = 0;
    float dpi = 1.0f;

    void begin(HDC dc, Gdiplus::Graphics* g) { dc_ = dc; g_ = g; }

    // Clipping has to be set twice over: GDI+ draws the shapes and plain GDI
    // draws the text, and neither honours the other's clip. Without the GDI
    // half, scrolled rail content prints straight over the top bar.
    //
    // The pointer is pushed out of range while clipped, so a control scrolled
    // out of sight cannot still be clicked where it is no longer drawn.
    void pushClip(const Rect& r) {
        g_->SetClip(Gdiplus::RectF(float(r.x), float(r.y), float(r.w), float(r.h)),
                    Gdiplus::CombineModeReplace);
        dcSaved_ = SaveDC(dc_);
        IntersectClipRect(dc_, int(std::floor(r.x)), int(std::floor(r.y)),
                          int(std::ceil(r.x + r.w)), int(std::ceil(r.y + r.h)));
        savedMouseX_ = mouseX;
        savedMouseY_ = mouseY;
        if (!r.contains(mouseX, mouseY)) { mouseX = -1e6; mouseY = -1e6; }
        clipped_ = true;
    }
    void popClip() {
        if (!clipped_) return;
        g_->ResetClip();
        if (dcSaved_) { RestoreDC(dc_, dcSaved_); dcSaved_ = 0; }
        mouseX = savedMouseX_;
        mouseY = savedMouseY_;
        clipped_ = false;
    }

    void image(Gdiplus::Image* img, const Rect& r) {
        if (!img) return;
        g_->DrawImage(img, Gdiplus::RectF(float(r.x), float(r.y),
                                          float(r.w), float(r.h)));
    }
    void endFrame() { mousePressed = mouseReleased = false; wheel = 0; }

    double s(double v) const { return v * dpi; }

    // ---------------------------------------------------------------- paint
    void fillRect(const Rect& r, const Rgb& c, double a = 1.0) {
        Gdiplus::SolidBrush b(gp(c, a));
        g_->FillRectangle(&b, Gdiplus::RectF(float(r.x), float(r.y),
                                             float(r.w), float(r.h)));
    }

    void fillRound(const Rect& r, double rad, const Rgb& c, double a = 1.0) {
        Gdiplus::GraphicsPath p;
        roundPath(p, r, rad);
        Gdiplus::SolidBrush b(gp(c, a));
        g_->FillPath(&b, &p);
    }

    void strokeRound(const Rect& r, double rad, const Rgb& c, double width = 1.0) {
        Gdiplus::GraphicsPath p;
        roundPath(p, r.inset(0.5), rad);
        Gdiplus::Pen pen(gp(c), float(width));
        g_->DrawPath(&pen, &p);
    }

    void circle(double cx, double cy, double r, const Rgb& c, double a = 1.0) {
        Gdiplus::SolidBrush b(gp(c, a));
        g_->FillEllipse(&b, Gdiplus::RectF(float(cx - r), float(cy - r),
                                           float(r * 2), float(r * 2)));
    }
    void ring(double cx, double cy, double r, const Rgb& c,
              double width = 1.0, double a = 1.0) {
        Gdiplus::Pen pen(gp(c, a), float(width));
        g_->DrawEllipse(&pen, Gdiplus::RectF(float(cx - r), float(cy - r),
                                             float(r * 2), float(r * 2)));
    }
    void ringDashed(double cx, double cy, double r, const Rgb& c,
                    double width, double a) {
        Gdiplus::Pen pen(gp(c, a), float(width));
        Gdiplus::REAL d[2] = { 3.0f, 6.0f };
        pen.SetDashPattern(d, 2);
        g_->DrawEllipse(&pen, Gdiplus::RectF(float(cx - r), float(cy - r),
                                             float(r * 2), float(r * 2)));
    }
    void line(double x1, double y1, double x2, double y2, const Rgb& c,
              double width = 1.0, double a = 1.0, bool dashed = false) {
        Gdiplus::Pen pen(gp(c, a), float(width));
        if (dashed) {
            Gdiplus::REAL d[2] = { 3.0f, 6.0f };
            pen.SetDashPattern(d, 2);
        }
        g_->DrawLine(&pen, float(x1), float(y1), float(x2), float(y2));
    }
    void triangle(double x1, double y1, double x2, double y2,
                  double x3, double y3, const Rgb& c) {
        Gdiplus::PointF pts[3] = { {float(x1),float(y1)}, {float(x2),float(y2)},
                                   {float(x3),float(y3)} };
        Gdiplus::SolidBrush b(gp(c));
        g_->FillPolygon(&b, pts, 3);
    }

    // ---------------------------------------------------------------- text
    void font(double size, bool bold = false) {
        fontSize_ = size; fontBold_ = bold;
    }

    double textWidth(const std::wstring& t) {
        HFONT f = pickFont();
        HGDIOBJ old = SelectObject(dc_, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc_, t.c_str(), int(t.size()), &sz);
        SelectObject(dc_, old);
        return sz.cx;
    }

    // Baseline is vertically centred on `cy`, matching the cairo build, so a
    // label and its readout sit on the same line without hand-nudging.
    void text(double x, double cy, const std::wstring& t, const Rgb& c,
              Align a = Align::Left) {
        if (t.empty()) return;
        HFONT f = pickFont();
        HGDIOBJ old = SelectObject(dc_, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc_, t.c_str(), int(t.size()), &sz);
        double tx = x;
        if (a == Align::Centre) tx = x - sz.cx * 0.5;
        else if (a == Align::Right) tx = x - sz.cx;
        SetTextColor(dc_, cref(c));
        SetBkMode(dc_, TRANSPARENT);
        TextOutW(dc_, int(tx + 0.5), int(cy - sz.cy * 0.5 + 0.5),
                 t.c_str(), int(t.size()));
        SelectObject(dc_, old);
    }

    // Letter-spaced, for the wordmark and the section headings.
    void tracked(double x, double cy, const std::wstring& t, const Rgb& c,
                 double spacing) {
        HFONT f = pickFont();
        HGDIOBJ old = SelectObject(dc_, f);
        SetTextColor(dc_, cref(c));
        SetBkMode(dc_, TRANSPARENT);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc_, &tm);
        const int y = int(cy - tm.tmHeight * 0.5 + 0.5);
        double cx = x;
        for (wchar_t ch : t) {
            TextOutW(dc_, int(cx + 0.5), y, &ch, 1);
            SIZE cs{};
            GetTextExtentPoint32W(dc_, &ch, 1, &cs);
            cx += cs.cx + spacing;
        }
        SelectObject(dc_, old);
    }

    double trackedWidth(const std::wstring& t, double spacing) {
        HFONT f = pickFont();
        HGDIOBJ old = SelectObject(dc_, f);
        double cx = 0;
        for (wchar_t ch : t) {
            SIZE cs{};
            GetTextExtentPoint32W(dc_, &ch, 1, &cs);
            cx += cs.cx + spacing;
        }
        SelectObject(dc_, old);
        return cx;
    }

    // ---------------------------------------------------------------- widgets
    bool slider(int id, const Rect& r, const std::wstring& label,
                const std::wstring& readout, double& value, double lo, double hi,
                const std::wstring& hint = L"", bool enabled = true) {
        const double rowH = s(20), trackY = r.y + rowH + s(12);
        font(14.5, false);
        text(r.x, r.y + rowH * 0.5, label, enabled ? theme.ink : theme.inkGhost);
        font(14.5, true);
        text(r.x + r.w, r.y + rowH * 0.5, readout,
             enabled ? theme.accentText : theme.inkGhost, Align::Right);

        const Rect track{ r.x, trackY - s(3), r.w, s(6) };
        fillRound(track, s(3), theme.dark ? theme.line : theme.lineSoft);

        const double t = std::clamp((value - lo) / (hi - lo), 0.0, 1.0);
        const double knobX = r.x + t * r.w;
        if (t > 0.001)
            fillRound({ r.x, trackY - s(3), t * r.w, s(6) }, s(3),
                      enabled ? theme.accent : theme.inkGhost);

        const Rect grab{ r.x - s(10), trackY - s(14), r.w + s(20), s(28) };
        const bool over = enabled && grab.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) active = id;

        bool changed = false;
        // The release counts too: if a burst of motion and the release land in
        // one frame, the knob must still end up where the pointer left it.
        if (active == id && enabled && (mouseDown || mouseReleased)) {
            const double nt = std::clamp((mouseX - r.x) / std::max(r.w, 1.0), 0.0, 1.0);
            const double nv = lo + nt * (hi - lo);
            if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
        }

        const double kr = (active == id || over) ? s(9) : s(7.5);
        circle(knobX, trackY, kr, enabled ? theme.accent : theme.inkGhost);
        circle(knobX, trackY, kr * 0.42, theme.chrome);

        if (!hint.empty()) {
            font(12);
            text(r.x, trackY + s(20), hint, theme.inkFaint);
        }
        return changed;
    }

    bool chip(int id, const Rect& r, const std::wstring& label, bool selected) {
        const bool over = r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) active = id;
        const bool clicked = over && mouseReleased && active == id;
        if (selected)  fillRound(r, s(15), theme.accent);
        else if (over) fillRound(r, s(15), theme.accentSoft);
        else           fillRound(r, s(15), theme.field);
        strokeRound(r, s(15), selected ? theme.accent : theme.line);
        font(13, selected);
        text(r.x + r.w * 0.5, r.y + r.h * 0.5, label,
             selected ? theme.onAccent : theme.inkSoft, Align::Centre);
        return clicked;
    }

    bool dropdown(int id, const Rect& r, const std::wstring& value,
                  bool enabled = true) {
        const bool over = enabled && r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) openMenu = (openMenu == id) ? 0 : id;
        fillRound(r, s(6), enabled ? theme.field : mix(theme.field, theme.ground, 0.5));
        strokeRound(r, s(6), (openMenu == id || over) ? theme.accent : theme.line);
        font(14);
        text(r.x + s(12), r.y + r.h * 0.5, value, enabled ? theme.ink : theme.inkGhost);
        const double cx = r.x + r.w - s(16), cy = r.y + r.h * 0.5;
        triangle(cx - s(4.5), cy - s(2), cx + s(4.5), cy - s(2), cx, cy + s(3.5),
                 enabled ? theme.inkFaint : theme.inkGhost);
        return openMenu == id;
    }

    // Drawn last and outside any clip, so a long list is never cut off.
    int menuPopup(int id, const Rect& anchor, const std::vector<std::wstring>& items,
                  int current) {
        if (openMenu != id || items.empty()) return -1;
        const double rowH = s(32);
        const double h = rowH * items.size() + s(8);
        Rect box{ anchor.x, anchor.y + anchor.h + s(4), anchor.w, h };

        fillRound(box, s(8), theme.chrome);
        strokeRound(box, s(8), theme.line);

        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect row{ box.x + s(4), box.y + s(4) + rowH * double(i),
                            box.w - s(8), rowH };
            const bool over = row.contains(mouseX, mouseY);
            if (over) fillRound(row, s(5), theme.accentSoft);
            else if (int(i) == current) fillRound(row, s(5), theme.field);
            font(14, int(i) == current);
            text(row.x + s(10), row.y + row.h * 0.5, items[i],
                 int(i) == current ? theme.accentText : theme.ink);
            if (over && mouseReleased) { chosen = int(i); }
        }
        if (mouseReleased) openMenu = 0;
        return chosen;
    }

    int segmented(int id, const Rect& r, const std::vector<std::wstring>& items,
                  int current) {
        (void)id;
        fillRound(r, s(7), theme.field);
        strokeRound(r, s(7), theme.line);
        const double w = r.w / double(items.size());
        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect cell{ r.x + w * double(i), r.y, w, r.h };
            const bool over = cell.contains(mouseX, mouseY);
            if (int(i) == current) fillRound(cell.inset(s(2)), s(5), theme.ink);
            else if (over)         fillRound(cell.inset(s(2)), s(5), theme.accentSoft);
            font(13, int(i) == current);
            text(cell.x + cell.w * 0.5, cell.y + cell.h * 0.5, items[i],
                 int(i) == current ? theme.chrome : theme.inkSoft, Align::Centre);
            if (over && mouseReleased) chosen = int(i);
        }
        return chosen;
    }

    bool checkbox(int id, const Rect& r, const std::wstring& label, bool& value) {
        const bool over = r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) active = id;
        bool clicked = false;
        if (over && mouseReleased && active == id) { value = !value; clicked = true; }

        const Rect box{ r.x, r.y + r.h * 0.5 - s(9), s(18), s(18) };
        fillRound(box, s(4), value ? theme.accent : theme.field);
        strokeRound(box, s(4), value ? theme.accent : theme.line);
        if (value) {
            Gdiplus::Pen pen(gp(theme.onAccent), float(s(2.2)));
            Gdiplus::PointF pts[3] = {
                { float(box.x + s(4.5)),  float(box.y + s(9)) },
                { float(box.x + s(7.8)),  float(box.y + s(12.6)) },
                { float(box.x + s(13.6)), float(box.y + s(5.6)) } };
            g_->DrawLines(&pen, pts, 3);
        }
        font(14);
        text(box.x + s(27), r.y + r.h * 0.5, label, theme.ink);
        return clicked;
    }

    bool ghostButton(int id, const Rect& r, const std::wstring& label,
                     bool enabled = true) {
        const bool over = enabled && r.contains(mouseX, mouseY);
        if (over) hot = id;
        bool clicked = false;
        if (over && mousePressed) active = id;
        if (over && mouseReleased && active == id) clicked = true;
        fillRound(r, s(6), over ? theme.accentSoft : theme.field);
        strokeRound(r, s(6), theme.line);
        font(13.5, false);
        text(r.x + r.w * 0.5, r.y + r.h * 0.5, label,
             enabled ? (over ? theme.accentText : theme.inkSoft) : theme.inkGhost,
             Align::Centre);
        return clicked;
    }

    // A solid, high-emphasis button -- the Start/Stop control in the mockup.
    bool primaryButton(int id, const Rect& r, const std::wstring& label,
                       const Rgb& fill) {
        const bool over = r.contains(mouseX, mouseY);
        if (over) hot = id;
        bool clicked = false;
        if (over && mousePressed) active = id;
        if (over && mouseReleased && active == id) clicked = true;
        fillRound(r, s(6), over ? mix(fill, theme.ink, 0.12) : fill);
        font(15, true);
        text(r.x + r.w * 0.5, r.y + r.h * 0.5, label, theme.onAccent, Align::Centre);
        return clicked;
    }

    ~Ui() { for (auto& f : fonts_) DeleteObject(f.handle); }

private:
    struct FontEntry { int px; bool bold; HFONT handle; };

    HFONT pickFont() {
        const int px = int(fontSize_ * dpi + 0.5);
        const int weight = fontBold_ ? FW_SEMIBOLD : FW_NORMAL;
        for (auto& f : fonts_)
            if (f.px == px && f.bold == fontBold_) return f.handle;
        HFONT h = CreateFontW(-px, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              VARIABLE_PITCH, L"Segoe UI");
        fonts_.push_back({ px, fontBold_, h });
        return h;
    }

    static void roundPath(Gdiplus::GraphicsPath& p, const Rect& r, double rad) {
        const double d = std::min(rad, std::min(r.w, r.h) * 0.5) * 2.0;
        if (d <= 1.0) {
            p.AddRectangle(Gdiplus::RectF(float(r.x), float(r.y),
                                          float(r.w), float(r.h)));
            return;
        }
        const Gdiplus::RectF rr(float(r.x), float(r.y), float(r.w), float(r.h));
        p.AddArc(rr.X, rr.Y, float(d), float(d), 180, 90);
        p.AddArc(rr.X + rr.Width - float(d), rr.Y, float(d), float(d), 270, 90);
        p.AddArc(rr.X + rr.Width - float(d), rr.Y + rr.Height - float(d),
                 float(d), float(d), 0, 90);
        p.AddArc(rr.X, rr.Y + rr.Height - float(d), float(d), float(d), 90, 90);
        p.CloseFigure();
    }

    HDC dc_ = nullptr;
    Gdiplus::Graphics* g_ = nullptr;
    int  dcSaved_ = 0;
    bool clipped_ = false;
    double savedMouseX_ = 0, savedMouseY_ = 0;
    double fontSize_ = 14;
    bool   fontBold_ = false;
    std::vector<FontEntry> fonts_;
};

} // namespace eightd
