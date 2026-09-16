// The v2 widget set, drawn with GDI+.
//
// cpp/src/ui/Widgets.h is cairo, so it cannot be compiled here -- but every
// widget in it is reproduced below with the same geometry, the same radii and
// the same behaviour, so the two builds are one product rather than two
// interpretations of it. Neither the colours nor the glyphs are copied: the
// tokens come from cpp/src/ui/Theme.h and the icons from cpp/src/ui/Icons.h,
// both compiled in place.
//
// GDI+ rather than plain GDI because this interface is circles and rounded
// rectangles all the way down, and GDI anti-aliases neither.
#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>

#include "Path.h"
#include "../../../cpp/src/ui/Theme.h"
#include "../../../cpp/src/ui/Icons.h"

namespace eightd {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
    Rect inset(double d) const { return { x + d, y + d, w - d * 2, h - d * 2 }; }
    double cx() const { return x + w * 0.5; }
    double cy() const { return y + h * 0.5; }
};

enum class Align { Left, Centre, Right };

// Weights, named as the design names them.
enum Weight { W400 = 400, W500 = 500, W600 = 600, W700 = 700, W800 = 800 };

inline constexpr double kPill = 999;       // "radius: 999px"
inline constexpr double kPiUi = 3.14159265358979323846;

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
    bool   doubleClick = false;      // this press was the second of two
    bool   shift = false;            // held: every adjustment is four times finer
    int    wheel = 0;

    int hot = 0, active = 0, openMenu = 0;
    double dragA = 0, dragV = 0;     // the angle a knob turn is at, and its value
    float dpi = 1.0f;

    void begin(HDC dc, Gdiplus::Graphics* g) { dc_ = dc; g_ = g; }
    void endFrame() {
        mousePressed = mouseReleased = doubleClick = false;
        wheel = 0;
        if (!mouseDown) active = 0;
    }

    double s(double v) const { return v * dpi; }

    // Clipping has to be set twice over: GDI+ draws the shapes and plain GDI
    // draws the text, and neither honours the other's clip.
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
    void strokeRound(const Rect& r, double rad, const Rgb& c,
                     double width = 1.0, double a = 1.0) {
        Gdiplus::GraphicsPath p;
        roundPath(p, r.inset(width * 0.5), rad);
        Gdiplus::Pen pen(gp(c, a), float(width));
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
                    double width, double a, double on = 1.0, double off = 7.0) {
        Gdiplus::Pen pen(gp(c, a), float(width));
        Gdiplus::REAL d[2] = { float(on), float(off) };
        pen.SetDashPattern(d, 2);
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g_->DrawEllipse(&pen, Gdiplus::RectF(float(cx - r), float(cy - r),
                                             float(r * 2), float(r * 2)));
    }
    void line(double x1, double y1, double x2, double y2, const Rgb& c,
              double width = 1.0, double a = 1.0) {
        Gdiplus::Pen pen(gp(c, a), float(width));
        g_->DrawLine(&pen, float(x1), float(y1), float(x2), float(y2));
    }
    void arc(double cx, double cy, double r, double fromDeg, double sweepDeg,
             const Rgb& c, double width, double a = 1.0, bool roundCap = true) {
        Gdiplus::Pen pen(gp(c, a), float(width));
        if (roundCap) {
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
        }
        g_->DrawArc(&pen, Gdiplus::RectF(float(cx - r), float(cy - r),
                                         float(r * 2), float(r * 2)),
                    float(fromDeg), float(sweepDeg));
    }

    // The "lights" the design asks for: a soft bloom, painted under whatever it
    // belongs to.  A path gradient is GDI+'s radial fill.
    void glow(double cx, double cy, double radius, const Rgb& c, double alpha) {
        if (radius <= 0 || alpha <= 0) return;
        Gdiplus::GraphicsPath p;
        p.AddEllipse(Gdiplus::RectF(float(cx - radius), float(cy - radius),
                                    float(radius * 2), float(radius * 2)));
        Gdiplus::PathGradientBrush b(&p);
        b.SetCenterPoint(Gdiplus::PointF(float(cx), float(cy)));
        b.SetCenterColor(gp(c, alpha));
        Gdiplus::Color edge = gp(c, 0.0);
        int count = 1;
        b.SetSurroundColors(&edge, &count);
        g_->FillPath(&b, &p);
    }

    // A filled disc lit from a point inside it -- the orbit's floor.  GDI+'s
    // path gradient is its radial fill; the centre may sit off-centre, which is
    // what makes the stage read as lit from above.
    void discGradient(double cx, double cy, double r, double lightY,
                      const Rgb& centre, const Rgb& edge) {
        Gdiplus::GraphicsPath p;
        p.AddEllipse(Gdiplus::RectF(float(cx - r), float(cy - r),
                                    float(r * 2), float(r * 2)));
        Gdiplus::PathGradientBrush b(&p);
        b.SetCenterPoint(Gdiplus::PointF(float(cx), float(lightY)));
        b.SetCenterColor(gp(centre));
        Gdiplus::Color rim = gp(edge);
        int count = 1;
        b.SetSurroundColors(&rim, &count);
        g_->FillPath(&b, &p);
    }

    // ---------------------------------------------------------------- text
    void font(double size, int weight = W400, bool serifFace = false) {
        fontSize_ = size; fontWeight_ = weight; fontSerif_ = serifFace;
    }

    double textWidth(const std::wstring& t) {
        HFONT f = pickFont();
        HGDIOBJ old = SelectObject(dc_, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc_, t.c_str(), int(t.size()), &sz);
        SelectObject(dc_, old);
        return sz.cx;
    }

    // `cy` is the vertical centre of the line, matching the cairo build, so a
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

    // Letter-spaced caps, used for kickers and section headings.
    void tracked(double x, double cy, const std::wstring& t, const Rgb& c,
                 double spacing, Align a = Align::Left) {
        HFONT f = pickFont();
        HGDIOBJ old = SelectObject(dc_, f);
        double total = 0;
        for (wchar_t ch : t) {
            SIZE cs{};
            GetTextExtentPoint32W(dc_, &ch, 1, &cs);
            total += cs.cx + spacing;
        }
        double cx = x;
        if (a == Align::Centre) cx = x - total * 0.5;
        else if (a == Align::Right) cx = x - total;

        SetTextColor(dc_, cref(c));
        SetBkMode(dc_, TRANSPARENT);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc_, &tm);
        const int y = int(cy - tm.tmHeight * 0.5 + 0.5);
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

    // Trims to fit, so a long title cannot run into the control beside it.
    std::wstring fit(std::wstring t, double limit) {
        if (textWidth(t) <= limit) return t;
        while (!t.empty()) {
            t.pop_back();
            if (textWidth(t + L"…") <= limit) break;
        }
        return t + L"…";
    }

    // ---------------------------------------------------------------- icons
    void icon(const Icon& ic, const Rect& box, const Rgb& colour,
              double alpha = 1.0, bool dashed = false) {
        const double k = box.w / ic.box;
        if (ic.fill) {
            Gdiplus::GraphicsPath p;
            p.SetFillMode(Gdiplus::FillModeWinding);
            SvgPath::add(p, ic.fill, box.x, box.y, box.w, ic.box);
            Gdiplus::SolidBrush b(gp(colour, alpha));
            g_->FillPath(&b, &p);
        }
        if (ic.stroke) {
            Gdiplus::GraphicsPath p;
            SvgPath::add(p, ic.stroke, box.x, box.y, box.w, ic.box);
            Gdiplus::Pen pen(gp(colour, alpha), float(ic.weight * k));
            pen.SetStartCap(Gdiplus::LineCapRound);
            pen.SetEndCap(Gdiplus::LineCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            if (dashed) {
                Gdiplus::REAL d[2] = { 1.6f, 2.4f };
                pen.SetDashPattern(d, 2);
            }
            g_->DrawPath(&pen, &p);
        }
    }

    // The wordmark: four bars, a smile and the moving dot.
    void logo(const Rect& box, double smile = 1.0) {
        static const char* kBars =
            "M170 200h0a22 22 0 0 1 22 22v48a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22v-48"
            "a22 22 0 0 1 22-22z"
            "M233 143h0a22 22 0 0 1 22 22v105a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22V165"
            "a22 22 0 0 1 22-22z"
            "M296 175h0a22 22 0 0 1 22 22v73a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22v-73"
            "a22 22 0 0 1 22-22z"
            "M361 228h0a22 22 0 0 1 22 22v20a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22v-20"
            "a22 22 0 0 1 22-22z";
        Gdiplus::GraphicsPath bars;
        bars.SetFillMode(Gdiplus::FillModeWinding);
        SvgPath::add(bars, kBars, box.x, box.y, box.w, 512);
        Gdiplus::SolidBrush ink(gp(theme.text));
        g_->FillPath(&ink, &bars);

        Gdiplus::GraphicsPath curve;
        SvgPath::add(curve, "M100 316C170 382 342 382 408 320", box.x, box.y, box.w, 512);
        Gdiplus::Pen pen(gp(theme.accent, smile), float(30.0 * box.w / 512.0));
        pen.SetStartCap(Gdiplus::LineCapRound);
        pen.SetEndCap(Gdiplus::LineCapRound);
        g_->DrawPath(&pen, &curve);

        const double k = box.w / 512.0;
        circle(box.x + 410 * k, box.y + 320 * k, 30 * k, theme.motion);
    }

    // -------------------------------------------------------------- controls

    bool over(const Rect& r) const { return r.contains(mouseX, mouseY); }

    // Was this rectangle clicked?  Shared by everything below.
    bool click(int id, const Rect& r, bool enabled = true) {
        const bool o = enabled && r.contains(mouseX, mouseY);
        if (o) hot = id;
        if (o && mousePressed) active = id;
        return o && mouseReleased && active == id;
    }

    // A filled pill: the primary action.
    bool pill(int id, const Rect& r, const std::wstring& label,
              const Rgb& bg, const Rgb& fg, double size = 15, int weight = W700,
              bool enabled = true) {
        const bool clicked = click(id, r, enabled);
        const bool hov = enabled && over(r);
        Rgb fillCol = bg;
        if (!enabled) fillCol = mix(bg, theme.ground, 0.55);
        else if (active == id && hov) fillCol = mix(bg, theme.ground, 0.18);
        else if (hov) fillCol = mix(bg, theme.text, theme.dark ? 0.10 : 0.06);
        fillRound(r, kPill, fillCol);
        font(size, weight);
        text(r.cx(), r.cy(), label, enabled ? fg : theme.ghost, Align::Centre);
        return clicked;
    }

    bool ghostPill(int id, const Rect& r, const std::wstring& label,
                   double size = 14.5, bool enabled = true) {
        const bool clicked = click(id, r, enabled);
        const bool hov = enabled && over(r);
        fillRound(r, kPill, hov ? mix(theme.well, theme.text, 0.07) : theme.well);
        font(size, W600);
        text(r.cx(), r.cy(), label, enabled ? theme.dim : theme.ghost, Align::Centre);
        return clicked;
    }

    // A preset chip.  `surface` is what it sits on, so it can lift off it.
    bool chip(int id, const Rect& r, const std::wstring& label, bool selected,
              const Rgb& surface) {
        const bool clicked = click(id, r);
        const bool hov = over(r);
        if (selected) fillRound(r, kPill, theme.accent);
        else fillRound(r, kPill, hov ? mix(surface, theme.text, 0.07) : surface);
        font(12.5, selected ? W700 : W500);
        text(r.cx(), r.cy(), label, selected ? theme.onAccent : theme.dim, Align::Centre);
        return clicked;
    }

    // A tile from a grid of choices: glyph over a caption.
    bool tile(int id, const Rect& r, const std::wstring& label, const Icon& glyph,
              bool selected, bool dashedGlyph = false) {
        const bool clicked = click(id, r);
        const bool hov = over(r);
        if (selected) {
            fillRound(r, s(14), theme.tint);
            strokeRound(r, s(14), theme.accent, s(1.5), 0.7);
        } else {
            fillRound(r, s(14), hov ? mix(theme.well, theme.text, 0.06) : theme.well);
        }
        const Rgb c = selected ? theme.deep : theme.faint;
        const double gs = s(22);
        const double gy = label.empty() ? r.cy() - gs * 0.5 : r.cy() - s(15);
        icon(glyph, { r.cx() - gs * 0.5, gy, gs, gs }, c, 1.0, dashedGlyph);
        if (!label.empty()) {
            font(10.5, selected ? W600 : W400);
            text(r.cx(), r.y + r.h - s(13), label, c, Align::Centre);
        }
        return clicked;
    }

    // Segmented control, pill shaped.
    int segPill(int id, const Rect& r, const std::vector<std::wstring>& items,
                int current, double size = 11.5) {
        fillRound(r, kPill, theme.well);
        const double pad = s(3);
        const double w = (r.w - pad * 2) / double(items.size());
        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect cell{ r.x + pad + w * double(i), r.y + pad, w, r.h - pad * 2 };
            if (click(id + int(i), cell)) chosen = int(i);
            const bool on = int(i) == current;
            if (on) fillRound(cell, kPill, theme.raised);
            else if (over(cell)) fillRound(cell, kPill, mix(theme.well, theme.text, 0.06));
            font(size, on ? W600 : W500);
            text(cell.cx(), cell.cy(), items[i], on ? theme.text : theme.faint,
                 Align::Centre);
        }
        return chosen;
    }

    // The switch: 44x26 with a 20px knob, lit when it is on.
    bool switchPill(int id, const Rect& r, bool value, bool glowWhenOn = true) {
        const bool clicked = click(id, r);
        if (value && glowWhenOn)
            glow(r.cx(), r.cy(), r.h * 1.5, theme.accent, theme.dark ? 0.35 : 0.22);
        fillRound(r, kPill, value ? theme.accent
                                  : (over(r) ? mix(theme.well, theme.text, 0.08)
                                             : theme.well));
        const double k = r.h - s(6);
        const double kx = value ? r.x + r.w - s(3) - k : r.x + s(3);
        circle(kx + k * 0.5, r.cy(), k * 0.5,
               value ? Rgb::hex(0xFFFFFF) : theme.raised);
        return clicked;
    }

    // A knob.  Turn it, roll the wheel over it, double-click to reset.
    //
    // Turning is relative: the value moves by however far the pointer travels
    // around the dial, so grabbing it never makes the value jump to meet the
    // pointer.  Three quarters of a turn covers the range, and the value lands
    // on whole steps rather than drifting continuously.
    bool knob(int id, const Rect& box, const std::wstring& label,
              const std::wstring& readout, double& value, double lo, double hi,
              bool enabled = true, bool bipolar = false, double resetTo = 0,
              bool hasReset = false, double size = 58, double step = 0.01) {
        const double sz = s(size), sw = s(5 * size / 58.0);
        const double cx = box.cx(), cy = box.y + sz * 0.5;
        const double rad = sz * 0.5 - sw * 0.5 - s(1);
        const double alpha = enabled ? 1.0 : 0.4;

        // hit area: the dial plus its caption, so the grab is forgiving
        const Rect grab{ cx - sz * 0.5 - s(4), box.y - s(4), sz + s(8), sz + s(26) };
        const bool hov = enabled && grab.contains(mouseX, mouseY);
        if (hov) hot = id;
        bool changed = false;

        const double fine = shift ? 0.25 : 1.0;
        const double grid = shift ? step * 0.25 : step;
        auto land = [&](double v) {                    // onto the nearest step
            return std::clamp(lo + std::floor((v - lo) / grid + 0.5) * grid, lo, hi);
        };

        if (hov && mousePressed) {
            active = id;
            dragA = std::atan2(mouseY - cy, mouseX - cx);
            dragV = value;
            if (doubleClick && hasReset && std::fabs(value - resetTo) > 1e-9) {
                value = resetTo; changed = true; dragV = value;
            }
        }
        if (active == id && enabled && (mouseDown || mouseReleased)) {
            const double dx = mouseX - cx, dy = mouseY - cy;
            if (dx * dx + dy * dy > s(6) * s(6)) {     // too near the middle to aim
                const double a = std::atan2(dy, dx);
                double delta = a - dragA;
                if (delta >  kPiUi) delta -= 2 * kPiUi; // the seam at the bottom
                if (delta < -kPiUi) delta += 2 * kPiUi;
                dragA = a;
                dragV = std::clamp(dragV + delta / (kPiUi * 1.5) * (hi - lo) * fine,
                                   lo, hi);
                const double nv = land(dragV);
                if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
            }
        }
        if (hov && wheel != 0 && enabled) {
            const double nv = std::clamp(value + wheel * grid, lo, hi);
            if (std::fabs(nv - value) > 1e-9) { value = land(nv); changed = true; }
        }

        const double t = std::clamp((value - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);

        // track, then the value with its light behind it
        arc(cx, cy, rad, 135, 270, theme.line, sw, alpha);
        double from = 135, sweep = 270 * t;
        if (bipolar) {
            const double centre = (lo + hi) * 0.5;
            const double f = (value - centre) / std::max(hi - centre, 1e-9);
            if (f >= 0) { from = 270; sweep = 135 * std::clamp(f, 0.0, 1.0); }
            else { sweep = 135 * std::clamp(-f, 0.0, 1.0); from = 270 - sweep; }
        }
        if (sweep > 0.4) {
            if (enabled) arc(cx, cy, rad, from, sweep, theme.accent, sw + s(9), 0.16);
            arc(cx, cy, rad, from, sweep, theme.accent, sw, alpha);
        }

        // the cap, lit from above
        const double capR = rad - s(8 * size / 58.0);
        {
            Gdiplus::GraphicsPath p;
            p.AddEllipse(Gdiplus::RectF(float(cx - capR), float(cy - capR),
                                        float(capR * 2), float(capR * 2)));
            Gdiplus::PathGradientBrush b(&p);
            b.SetCenterPoint(Gdiplus::PointF(float(cx), float(cy - capR * 0.3)));
            const Rgb top = hov ? mix(theme.raised, theme.text, 0.10) : theme.raised;
            b.SetCenterColor(gp(top, alpha));
            Gdiplus::Color edge = gp(theme.well, alpha);
            int count = 1;
            b.SetSurroundColors(&edge, &count);
            g_->FillPath(&b, &p);
        }

        if (bipolar)   // the mark at twelve o'clock a tone control needs
            circle(cx, box.y + sw * 0.5 + s(1), s(1.4), theme.faint, alpha);

        // The head of the line: where the value has reached, and what the hand
        // goes for.
        const double head = (from + sweep) * kPiUi / 180.0;
        const double hx = cx + std::cos(head) * rad, hy = cy + std::sin(head) * rad;
        if (enabled) circle(hx, hy, sw * 0.5 + s(5), theme.accent, 0.22);
        circle(hx, hy, sw * 0.5 + s(2.5), theme.accent, alpha);

        font(11.5 * size / 58.0, W700);
        text(cx, cy, readout, enabled ? theme.text : theme.ghost, Align::Centre);
        font(11 * size / 58.0, W500);
        text(cx, box.y + sz + s(10), label, enabled ? theme.dim : theme.ghost,
             Align::Centre);
        return changed;
    }

    // A value that reads across instead of around.
    bool slider(int id, const Rect& track, double& value, double lo, double hi,
                bool enabled = true, double step = 0.01) {
        const Rect grab{ track.x - s(8), track.y - s(12),
                         track.w + s(16), track.h + s(24) };
        const bool hov = enabled && grab.contains(mouseX, mouseY);
        if (hov) hot = id;
        if (hov && mousePressed) active = id;

        bool changed = false;
        const double grid = shift ? step * 0.25 : step;
        auto land = [&](double v) {
            return std::clamp(lo + std::floor((v - lo) / grid + 0.5) * grid, lo, hi);
        };
        if (active == id && enabled && (mouseDown || mouseReleased)) {
            const double t = std::clamp((mouseX - track.x) / std::max(track.w, 1.0),
                                        0.0, 1.0);
            const double nv = land(lo + t * (hi - lo));
            if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
        }
        if (hov && wheel != 0 && enabled) {
            const double nv = land(std::clamp(value + wheel * grid, lo, hi));
            if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
        }

        const double t = std::clamp((value - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);
        const double alpha = enabled ? 1.0 : 0.4;
        fillRound(track, kPill, theme.well, alpha);
        if (t > 0.002) {
            if (enabled) glow(track.x + track.w * t, track.cy(), s(18), theme.accent, 0.3);
            fillRound({ track.x, track.y, track.w * t, track.h }, kPill,
                      theme.accent, alpha);
        }
        const double kr = (active == id || hov) ? s(9) : s(8);
        circle(track.x + track.w * t, track.cy(), kr + s(2), Rgb{ 0, 0, 0 }, 0.35 * alpha);
        circle(track.x + track.w * t, track.cy(), kr,
               enabled ? theme.text : theme.raised, alpha);
        return changed;
    }

    // Draws the closed control; the list is drawn later by `menuPopup`.
    bool dropdown(int id, const Rect& r, const std::wstring& value,
                  bool enabled = true) {
        const bool hov = enabled && r.contains(mouseX, mouseY);
        if (hov) hot = id;
        if (hov && mousePressed) openMenu = (openMenu == id) ? 0 : id;
        fillRound(r, kPill, hov ? mix(theme.well, theme.text, 0.07) : theme.well);
        font(12.5, W500);
        text(r.x + s(14), r.cy(), fit(value, r.w - s(34)),
             enabled ? theme.dim : theme.ghost);
        const double cx = r.x + r.w - s(15), cy = r.cy();
        Gdiplus::PointF pts[3] = {
            { float(cx - s(4)), float(cy - s(1.8)) },
            { float(cx + s(4)), float(cy - s(1.8)) },
            { float(cx),        float(cy + s(3.2)) } };
        Gdiplus::SolidBrush b(gp(enabled ? theme.faint : theme.ghost));
        g_->FillPolygon(&b, pts, 3);
        return openMenu == id;
    }

    // Returns the chosen index, or -1.  Drawn last and outside any clip, so a
    // long list is never cut off.
    int menuPopup(int id, const Rect& anchor, const std::vector<std::wstring>& items,
                  int current, double width = 0) {
        if (openMenu != id || items.empty()) return -1;
        const double rowH = s(32);
        const double w = width > 0 ? s(width) : std::max(anchor.w, s(200));
        const double h = rowH * double(items.size()) + s(10);
        Rect box{ anchor.x, anchor.y + anchor.h + s(6), w, h };

        fillRound(box, s(16), theme.card);
        strokeRound(box, s(16), theme.line, 1, 0.8);

        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect row{ box.x + s(5), box.y + s(5) + rowH * double(i),
                            box.w - s(10), rowH };
            const bool hov = row.contains(mouseX, mouseY);
            if (hov) fillRound(row, s(10), theme.well);
            font(12.5, int(i) == current ? W700 : W500);
            text(row.x + s(11), row.cy(), fit(items[i], row.w - s(22)),
                 int(i) == current ? theme.accent : theme.dim);
            if (hov && mouseReleased) chosen = int(i);
        }
        if (mousePressed && !box.contains(mouseX, mouseY) &&
            !anchor.contains(mouseX, mouseY)) openMenu = 0;
        if (chosen >= 0) openMenu = 0;
        return chosen;
    }

    // The interface is drawn in Figtree, as the design is.  The file travels
    // with the app and is loaded for this process only; without it Windows'
    // own Segoe UI takes over and everything still lays out.
    void useBundledFont(const std::wstring& path) {
        if (path.empty()) return;
        if (AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr) > 0) {
            fontFace_ = L"Figtree";
            loadedFont_ = path;
        }
    }

    ~Ui() {
        for (auto& f : fonts_) DeleteObject(f.handle);
        if (!loadedFont_.empty())
            RemoveFontResourceExW(loadedFont_.c_str(), FR_PRIVATE, nullptr);
    }

private:
    struct FontEntry { int px; int weight; bool serif; HFONT handle; };

    HFONT pickFont() {
        const int px = int(fontSize_ * dpi + 0.5);
        for (auto& f : fonts_)
            if (f.px == px && f.weight == fontWeight_ && f.serif == fontSerif_)
                return f.handle;
        const wchar_t* face = fontSerif_ ? L"Georgia" : fontFace_.c_str();
        HFONT h = CreateFontW(-px, 0, 0, 0, fontWeight_, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              VARIABLE_PITCH, face);
        fonts_.push_back({ px, fontWeight_, fontSerif_, h });
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
    int    fontWeight_ = W400;
    bool   fontSerif_ = false;
    std::wstring fontFace_ = L"Segoe UI";
    std::wstring loadedFont_;
    std::vector<FontEntry> fonts_;
};

} // namespace eightd
