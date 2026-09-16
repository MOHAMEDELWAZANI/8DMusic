// A small immediate-mode widget layer over cairo, in the v2 shapes.
//
// Immediate mode suits this interface: every control is a pure function of a
// value the app already owns, so there is no widget tree to keep in sync and no
// per-widget allocation.  Only the identity of the control the mouse is
// currently working stays between frames.
#pragma once
#include "Theme.h"
#include "Icons.h"
#include "Path.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <chrono>

namespace eightd {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect inset(double d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    double cx() const { return x + w * 0.5; }
    double cy() const { return y + h * 0.5; }
};

enum class Align { Left, Centre, Right };

// Weights, named as the design names them.
enum Weight { W400 = 400, W500 = 500, W600 = 600, W700 = 700, W800 = 800 };

inline constexpr double kPill = 999;       // "radius: 999px"

// Text goes through Pango rather than cairo's own "toy" text API.  The toy API
// paints the code points it is handed, in order, in one face: Arabic arrives
// unjoined and backwards, and anything the face lacks comes out as blank boxes.
// Pango shapes, applies the bidirectional algorithm and falls back per run.
class Ui {
    // One layout, reused for every string: creating one per call would put a
    // pile of allocation into each frame.  `mutable` because measuring text is
    // logically const but has to feed the layout.
    mutable PangoLayout* layout_ = nullptr;
    mutable PangoFontDescription* desc_ = nullptr;
    cairo_t* layoutCr_ = nullptr;
    mutable std::vector<Rect> hitRects_;

public:
    cairo_t* cr = nullptr;
    Theme theme = Theme::darkTheme();

    // The families, in order of preference.  Figtree ships with the app; the
    // rest are what a Linux desktop is likely to have if it is missing.
    std::string sans = "Figtree, Inter, Lato, DejaVu Sans, sans-serif";
    std::string serif = "Noto Serif, DejaVu Serif, serif";

    Ui() = default;
    Ui(const Ui&) = delete;
    Ui& operator=(const Ui&) = delete;

    ~Ui() {
        if (layout_) g_object_unref(layout_);
        if (desc_) pango_font_description_free(desc_);
    }

    // Layouts belong to a cairo context, and this UI draws into two of them
    // (the cached chrome and the window), so the layout follows the target.
    void useCr(cairo_t* target) {
        cr = target;
        if (target == layoutCr_) return;
        if (layout_) g_object_unref(layout_);
        layout_ = pango_cairo_create_layout(target);
        layoutCr_ = target;
        if (!desc_) desc_ = pango_font_description_new();
        pango_layout_set_font_description(layout_, desc_);
    }

    // Pango lays text out in user units but rasterises through the current
    // transform, so it has to be told when that transform changes -- once per
    // pass is enough, because it does not change inside one.
    void syncTransform() const {
        if (layout_ && cr) pango_cairo_update_layout(cr, layout_);
    }

    // input state for this frame
    double mouseX = -1, mouseY = -1;
    bool mouseDown = false;      // held
    bool mousePressed = false;   // went down this frame
    bool mouseReleased = false;  // came up this frame
    bool doubleClick = false;    // the press this frame was the second of two
    bool shift = false;          // held: every adjustment becomes four times finer
    int  wheel = 0;              // -1 up, +1 down

    // carried between frames
    int active = 0;              // control currently being dragged
    int openMenu = 0;            // dropdown showing its list
    int hot = 0;
    double dragA = 0, dragV = 0; // the angle a knob drag is at, and its value

    void beginFrame() { hot = 0; }

    // Every control that reacts to the pointer records its rectangle during a
    // chrome pass.  Between passes that list is enough to answer "would moving
    // here change anything?" without laying the whole interface out again --
    // repainting the chrome costs about 10 ms, a hit test costs nothing.
    void beginHitTest() const { hitRects_.clear(); }
    void noteHit(const Rect& r) const { hitRects_.push_back(r); }
    int hitIndexAt(double x, double y) const {
        for (int i = int(hitRects_.size()) - 1; i >= 0; --i)   // topmost first
            if (hitRects_[size_t(i)].contains(x, y)) return i;
        return -1;
    }
    void endFrame() {
        mousePressed = false; mouseReleased = false; wheel = 0; doubleClick = false;
        if (!mouseDown) active = 0;
    }

    // -- drawing helpers ---------------------------------------------------

    void setColour(const Rgb& c, double a = 1.0) const {
        cairo_set_source_rgba(cr, c.r, c.g, c.b, a);
    }
    void fillRect(const Rect& r, const Rgb& c, double a = 1.0) const {
        setColour(c, a);
        cairo_rectangle(cr, r.x, r.y, r.w, r.h);
        cairo_fill(cr);
    }
    void roundRect(const Rect& r, double radius) const {
        const double rr = std::min(radius, std::min(r.w, r.h) * 0.5);
        cairo_new_sub_path(cr);
        cairo_arc(cr, r.x + r.w - rr, r.y + rr,       rr, -M_PI / 2, 0);
        cairo_arc(cr, r.x + r.w - rr, r.y + r.h - rr, rr, 0, M_PI / 2);
        cairo_arc(cr, r.x + rr,       r.y + r.h - rr, rr, M_PI / 2, M_PI);
        cairo_arc(cr, r.x + rr,       r.y + rr,       rr, M_PI, 3 * M_PI / 2);
        cairo_close_path(cr);
    }
    void fillRound(const Rect& r, double radius, const Rgb& c, double a = 1.0) const {
        setColour(c, a); roundRect(r, radius); cairo_fill(cr);
    }
    void strokeRound(const Rect& r, double radius, const Rgb& c,
                     double w = 1.0, double a = 1.0) const {
        setColour(c, a); cairo_set_line_width(cr, w);
        roundRect(r.inset(w * 0.5), radius); cairo_stroke(cr);
    }
    void circle(double cx, double cy, double r, const Rgb& c, double a = 1.0) const {
        setColour(c, a);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    // The "lights" the design asks for: a soft radial bloom, painted under
    // whatever it belongs to.
    void glow(double cx, double cy, double radius, const Rgb& c, double alpha) const {
        cairo_pattern_t* p = cairo_pattern_create_radial(cx, cy, 0, cx, cy, radius);
        cairo_pattern_add_color_stop_rgba(p, 0.0, c.r, c.g, c.b, alpha);
        cairo_pattern_add_color_stop_rgba(p, 0.55, c.r, c.g, c.b, alpha * 0.35);
        cairo_pattern_add_color_stop_rgba(p, 1.0, c.r, c.g, c.b, 0.0);
        cairo_set_source(cr, p);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(p);
    }

    // Cairo has no blur, so a shadow is a handful of rounded rectangles fading
    // outwards.  At these sizes the banding is invisible and it costs nothing.
    void shadow(const Rect& r, double radius, double spread, double alpha,
                double dy = 0) const {
        const int steps = 12;
        for (int i = steps; i >= 1; --i) {
            const double t = double(i) / steps;
            const double g = spread * t;
            setColour(Rgb{0, 0, 0}, alpha * (1.0 - t) * (1.0 - t) * 0.30);
            roundRect({r.x - g, r.y - g + dy, r.w + g * 2, r.h + g * 2}, radius + g);
            cairo_fill(cr);
        }
    }

    void font(double size, int weight = W400, bool serifFace = false) const {
        if (!desc_ || !layout_) return;
        pango_font_description_set_family(desc_, serifFace ? serif.c_str() : sans.c_str());
        pango_font_description_set_weight(desc_, PangoWeight(weight));
        pango_font_description_set_absolute_size(desc_, size * PANGO_SCALE);
        pango_layout_set_font_description(layout_, desc_);
    }

    double textWidth(const std::string& s) const {
        if (!layout_) return 0;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_text(layout_, s.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        return double(w);
    }
    double textHeight(const std::string& s) const {
        if (!layout_) return 0;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_text(layout_, s.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        return double(h);
    }

    // `y` is the vertical centre of the line.
    void text(double x, double y, const std::string& s, const Rgb& c,
              Align align = Align::Left, double alpha = 1.0) const {
        if (!layout_) return;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_width(layout_, -1);
        pango_layout_set_text(layout_, s.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        double tx = x;
        if (align == Align::Centre) tx = x - w * 0.5;
        else if (align == Align::Right) tx = x - w;
        setColour(c, alpha);
        cairo_move_to(cr, tx, y - h * 0.5);
        pango_cairo_show_layout(cr, layout_);
    }

    // Wrapped body copy.  `y` is the top; returns the height it used.
    double paragraph(double x, double y, double width, const std::string& s,
                     const Rgb& c, double lineHeight = 1.55,
                     double alpha = 1.0) const {
        if (!layout_) return 0;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_width(layout_, int(width * PANGO_SCALE));
        pango_layout_set_wrap(layout_, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_text(layout_, s.c_str(), -1);
        pango_layout_set_spacing(layout_, 0);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        // Pango's own line height is tighter than the design's; add the rest.
        const int lines = pango_layout_get_line_count(layout_);
        const double natural = lines > 0 ? double(h) / lines : double(h);
        const double extra = std::max(0.0, natural * (lineHeight - 1.0) / 1.0 * 0.62);
        pango_layout_set_spacing(layout_, int(extra * PANGO_SCALE));
        pango_layout_get_pixel_size(layout_, &w, &h);
        setColour(c, alpha);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout_);
        pango_layout_set_width(layout_, -1);
        pango_layout_set_spacing(layout_, 0);
        return double(h);
    }
    double paragraphHeight(double width, const std::string& s,
                           double lineHeight = 1.55) const {
        if (!layout_) return 0;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_width(layout_, int(width * PANGO_SCALE));
        pango_layout_set_wrap(layout_, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_text(layout_, s.c_str(), -1);
        pango_layout_set_spacing(layout_, 0);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        const int lines = pango_layout_get_line_count(layout_);
        const double natural = lines > 0 ? double(h) / lines : double(h);
        const double extra = std::max(0.0, natural * (lineHeight - 1.0) * 0.62);
        pango_layout_set_spacing(layout_, int(extra * PANGO_SCALE));
        pango_layout_get_pixel_size(layout_, &w, &h);
        pango_layout_set_width(layout_, -1);
        pango_layout_set_spacing(layout_, 0);
        return double(h);
    }

    // A paragraph with inline emphasis, written as Pango markup.  The bullets
    // in About lead with a bold phrase and continue in the body colour, which
    // is one layout, not two.
    double markup(double x, double y, double width, const std::string& m,
                  const Rgb& c, double lineHeight = 1.5,
                  Align align = Align::Left) const {
        if (!layout_) return 0;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_alignment(layout_, align == Align::Centre ? PANGO_ALIGN_CENTER
                                   : (align == Align::Right ? PANGO_ALIGN_RIGHT
                                                            : PANGO_ALIGN_LEFT));
        pango_layout_set_width(layout_, int(width * PANGO_SCALE));
        pango_layout_set_wrap(layout_, PANGO_WRAP_WORD_CHAR);
        pango_layout_set_spacing(layout_, 0);
        pango_layout_set_markup(layout_, m.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        const int lines = pango_layout_get_line_count(layout_);
        const double natural = lines > 0 ? double(h) / lines : double(h);
        pango_layout_set_spacing(layout_, int(natural * (lineHeight - 1.0) * 0.62 * PANGO_SCALE));
        pango_layout_get_pixel_size(layout_, &w, &h);
        setColour(c);
        cairo_move_to(cr, x, y);
        pango_cairo_show_layout(cr, layout_);
        pango_layout_set_width(layout_, -1);
        pango_layout_set_spacing(layout_, 0);
        pango_layout_set_alignment(layout_, PANGO_ALIGN_LEFT);
        pango_layout_set_text(layout_, "", -1);
        return double(h);
    }

    // Letter-spaced caps, used for kickers and section headings.
    void tracked(double x, double y, const std::string& s, const Rgb& c,
                 double spacing = 1.6, Align align = Align::Left) const {
        if (!layout_) return;
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs,
            pango_attr_letter_spacing_new(int(spacing * PANGO_SCALE)));
        pango_layout_set_text(layout_, s.c_str(), -1);
        pango_layout_set_attributes(layout_, attrs);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        double tx = x;
        if (align == Align::Centre) tx = x - w * 0.5;
        else if (align == Align::Right) tx = x - w;
        setColour(c);
        cairo_move_to(cr, tx, y - h * 0.5);
        pango_cairo_show_layout(cr, layout_);
        pango_layout_set_attributes(layout_, nullptr);
        pango_attr_list_unref(attrs);
    }

    // -- icons ---------------------------------------------------------------

    void icon(const Icon& ic, const Rect& box, const Rgb& colour,
              double alpha = 1.0, const double* dash = nullptr, int dashN = 0) const {
        const double k = box.w / ic.box;
        if (ic.fill) {
            setColour(colour, alpha);
            cairo_new_path(cr);
            SvgPath::add(cr, ic.fill, box.x, box.y, box.w, ic.box);
            cairo_fill(cr);
        }
        if (ic.stroke) {
            setColour(colour, alpha);
            cairo_set_line_width(cr, ic.weight * k);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
            if (dash) cairo_set_dash(cr, dash, dashN, 0);
            cairo_new_path(cr);
            SvgPath::add(cr, ic.stroke, box.x, box.y, box.w, ic.box);
            cairo_stroke(cr);
            if (dash) cairo_set_dash(cr, nullptr, 0, 0);
            cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
            cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
        }
    }

    // The wordmark: four bars, a smile and the moving dot.
    void logo(const Rect& box, double smile = 1.0) const {
        static const char* kBars =
            "M170 200h0a22 22 0 0 1 22 22v48a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22v-48"
            "a22 22 0 0 1 22-22z"
            "M233 143h0a22 22 0 0 1 22 22v105a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22V165"
            "a22 22 0 0 1 22-22z"
            "M296 175h0a22 22 0 0 1 22 22v73a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22v-73"
            "a22 22 0 0 1 22-22z"
            "M361 228h0a22 22 0 0 1 22 22v20a22 22 0 0 1-22 22h0a22 22 0 0 1-22-22v-20"
            "a22 22 0 0 1 22-22z";
        setColour(theme.text);
        cairo_new_path(cr);
        SvgPath::add(cr, kBars, box.x, box.y, box.w, 512);
        cairo_fill(cr);

        setColour(theme.accent, smile);
        cairo_set_line_width(cr, 30.0 * box.w / 512.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(cr);
        SvgPath::add(cr, "M100 316C170 382 342 382 408 320", box.x, box.y, box.w, 512);
        cairo_stroke(cr);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

        const double k = box.w / 512.0;
        circle(box.x + 410 * k, box.y + 320 * k, 30 * k, theme.motion);
    }

    // -- controls ------------------------------------------------------------

    // Was this rectangle clicked?  Shared by everything below.
    bool click(int id, const Rect& r, bool enabled = true) {
        noteHit(r);
        const bool over = enabled && r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) active = id;
        return over && mouseReleased && active == id;
    }
    bool over(const Rect& r) const { return r.contains(mouseX, mouseY); }

    // A filled pill: the primary action.
    bool pill(int id, const Rect& r, const std::string& label,
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

    // The quiet one beside it.
    bool ghostPill(int id, const Rect& r, const std::string& label,
                   double size = 14.5, bool enabled = true) {
        const bool clicked = click(id, r, enabled);
        const bool hov = enabled && over(r);
        fillRound(r, kPill, hov ? mix(theme.well, theme.text, 0.07) : theme.well);
        font(size, W600);
        text(r.cx(), r.cy(), label, enabled ? theme.dim : theme.ghost, Align::Centre);
        return clicked;
    }

    // A preset chip.  `surface` is what it sits on, so it can lift off it.
    bool chip(int id, const Rect& r, const std::string& label, bool selected,
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
    bool tile(int id, const Rect& r, const std::string& label, const Icon& glyph,
              bool selected, bool dashedGlyph = false) {
        const bool clicked = click(id, r);
        const bool hov = over(r);
        if (selected) {
            fillRound(r, 14, theme.tint);
            strokeRound(r, 14, theme.accent, 1.5, 0.7);
        } else {
            fillRound(r, 14, hov ? mix(theme.well, theme.text, 0.06) : theme.well);
        }
        const Rgb c = selected ? theme.deep : theme.faint;
        const double gs = 22;
        // With no caption the glyph sits in the middle of the tile; with one it
        // makes room for it.
        const double gy = label.empty() ? r.cy() - gs * 0.5 : r.y + r.h * 0.5 - 15;
        const Rect gb{r.cx() - gs * 0.5, gy, gs, gs};
        static const double kDash[2] = {1.6, 2.4};
        icon(glyph, gb, c, 1.0, dashedGlyph ? kDash : nullptr, dashedGlyph ? 2 : 0);
        if (!label.empty()) {
            font(10.5, selected ? W600 : W400);
            text(r.cx(), r.y + r.h - 13, label, c, Align::Centre);
        }
        return clicked;
    }

    // Segmented control, pill shaped.
    int segPill(int id, const Rect& r, const std::vector<std::string>& items,
                int current, double size = 11.5) {
        fillRound(r, kPill, theme.well);
        const double pad = 3;
        const double w = (r.w - pad * 2) / double(items.size());
        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect cell{r.x + pad + w * double(i), r.y + pad, w, r.h - pad * 2};
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
        const double k = r.h - 6;
        const double kx = value ? r.x + r.w - 3 - k : r.x + 3;
        circle(kx + k * 0.5, r.cy(), k * 0.5,
               value ? Rgb::hex(0xFFFFFF) : theme.raised);
        return clicked;
    }

    // A value that reads across instead of around.  One long throw is easier
    // to place than a dial when there is width to give it.
    bool slider(int id, const Rect& track, double& value, double lo, double hi,
                bool enabled = true, double step = 0.01) {
        const Rect grab{track.x - 8, track.y - 12, track.w + 16, track.h + 24};
        noteHit(grab);
        const bool hov = enabled && grab.contains(mouseX, mouseY);
        if (hov) hot = id;
        if (hov && mousePressed) active = id;

        bool changed = false;
        // The release counts too: if a burst of motion and the release land in
        // one frame, the handle must still end up where the pointer left it.
        const double grid = shift ? step * 0.25 : step;
        auto land = [&](double v) {
            return std::clamp(lo + std::round((v - lo) / grid) * grid, lo, hi);
        };
        if (active == id && enabled && (mouseDown || mouseReleased)) {
            const double t = std::clamp((mouseX - track.x) / std::max(track.w, 1.0),
                                        0.0, 1.0);
            const double nv = land(lo + t * (hi - lo));
            if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
        }
        if (hov && wheel != 0 && enabled) {
            const double nv = land(std::clamp(value - wheel * grid, lo, hi));
            if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
        }

        const double t = std::clamp((value - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);
        const double alpha = enabled ? 1.0 : 0.4;
        fillRound(track, kPill, theme.well, alpha);
        if (t > 0.002) {
            if (enabled) glow(track.x + track.w * t, track.cy(), 18, theme.accent, 0.3);
            fillRound({track.x, track.y, track.w * t, track.h}, kPill,
                      theme.accent, alpha);
        }
        const double kr = (active == id || hov) ? 9 : 8;
        circle(track.x + track.w * t, track.cy(), kr + 2, Rgb{0, 0, 0}, 0.35 * alpha);
        circle(track.x + track.w * t, track.cy(), kr,
               enabled ? theme.text : theme.raised, alpha);
        return changed;
    }

    // A knob.  Turn it, roll the wheel over it, double-click to reset.
    //
    // Turning is relative: the value moves by however far the pointer travels
    // around the dial, so grabbing it never makes the value jump to meet the
    // pointer.  Three quarters of a turn covers the range, and the value lands
    // on whole steps rather than drifting continuously.
    //
    // `bipolar` fills from twelve o'clock in either direction, which is what a
    // tone control means; everything else fills from the start of the track.
    bool knob(int id, const Rect& box, const std::string& label,
              const std::string& readout, double& value, double lo, double hi,
              bool enabled = true, bool bipolar = false, double resetTo = 0,
              bool hasReset = false, double size = 58, double step = 0.01) {
        const double k = size / 58.0, sw = 5 * k;
        const double cx = box.cx(), cy = box.y + size * 0.5;
        const double rad = size * 0.5 - sw * 0.5 - 1 * k;
        const double alpha = enabled ? 1.0 : 0.4;
        const double a0 = M_PI * 0.75, span = M_PI * 1.5;

        // hit area: the dial plus its caption, so the grab is forgiving
        const Rect grab{cx - size * 0.5 - 4, box.y - 4, size + 8, size + 26};
        noteHit(grab);
        const bool hov = enabled && grab.contains(mouseX, mouseY);
        if (hov) hot = id;
        bool changed = false;

        const double fine = shift ? 0.25 : 1.0;
        const double grid = shift ? step * 0.25 : step;
        auto land = [&](double v) {                    // onto the nearest step
            return std::clamp(lo + std::round((v - lo) / grid) * grid, lo, hi);
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
            if (dx * dx + dy * dy > 36) {              // too near the middle to aim
                const double a = std::atan2(dy, dx);
                double delta = a - dragA;
                if (delta >  M_PI) delta -= 2 * M_PI;  // the seam at the bottom
                if (delta < -M_PI) delta += 2 * M_PI;
                dragA = a;
                dragV = std::clamp(dragV + delta / span * (hi - lo) * fine, lo, hi);
                const double nv = land(dragV);
                if (std::fabs(nv - value) > 1e-9) { value = nv; changed = true; }
            }
        }
        if (hov && wheel != 0 && enabled) {
            const double nv = std::clamp(value - wheel * grid, lo, hi);
            if (std::fabs(nv - value) > 1e-9) { value = land(nv); changed = true; }
        }

        const double t = std::clamp((value - lo) / std::max(hi - lo, 1e-9), 0.0, 1.0);

        // track
        setColour(theme.line, alpha);
        cairo_set_line_width(cr, sw);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, rad, a0, a0 + span);
        cairo_stroke(cr);

        // value, with its light behind it
        double from = a0, to = a0 + span * t;
        if (bipolar) {
            const double mid = a0 + span * 0.5;
            const double centre = (lo + hi) * 0.5;
            const double f = (value - centre) / std::max((hi - centre), 1e-9);
            if (f >= 0) { from = mid; to = mid + span * 0.5 * std::clamp(f, 0.0, 1.0); }
            else        { to = mid; from = mid - span * 0.5 * std::clamp(-f, 0.0, 1.0); }
        }
        if (to - from > 0.001) {
            if (enabled) {
                setColour(theme.accent, 0.16);
                cairo_set_line_width(cr, sw + 9 * k);
                cairo_new_path(cr);
                cairo_arc(cr, cx, cy, rad, from, to);
                cairo_stroke(cr);
            }
            setColour(theme.accent, alpha);
            cairo_set_line_width(cr, sw);
            cairo_new_path(cr);
            cairo_arc(cr, cx, cy, rad, from, to);
            cairo_stroke(cr);
        }
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

        // the cap
        const double capR = rad - 8 * k;
        cairo_pattern_t* p = cairo_pattern_create_radial(
            cx, cy - capR * 0.3, 0, cx, cy, capR * 1.4);
        const Rgb top = hov ? mix(theme.raised, theme.text, 0.10) : theme.raised;
        cairo_pattern_add_color_stop_rgba(p, 0, top.r, top.g, top.b, alpha);
        cairo_pattern_add_color_stop_rgba(p, 1, theme.well.r, theme.well.g,
                                          theme.well.b, alpha);
        cairo_set_source(cr, p);
        cairo_new_path(cr);
        cairo_arc(cr, cx, cy, capR, 0, 2 * M_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(p);

        if (bipolar)   // the mark at twelve o'clock a tone control needs
            circle(cx, box.y + sw * 0.5 + 1, 1.4 * k, theme.faint, alpha);

        // The head of the line: where the value has reached, and what the hand
        // goes for.
        const double head = a0 + span * t;
        const double hx = cx + std::cos(head) * rad, hy = cy + std::sin(head) * rad;
        if (enabled) circle(hx, hy, sw * 0.5 + 5 * k, theme.accent, 0.22);
        circle(hx, hy, sw * 0.5 + 2.5 * k, theme.accent, alpha);

        font(11.5 * k, W700);
        text(cx, cy, readout, theme.text, Align::Centre, alpha);
        font(11 * k, W500);
        text(cx, box.y + size + 10 * k, label, theme.dim, Align::Centre, alpha);
        return changed;
    }

    // A row in a list: icon, title, subtitle, chevron.
    bool listRow(int id, const Rect& r, const Icon& glyph, const Rgb& glyphColour,
                 const Rgb& glyphBg, const std::string& title,
                 const std::string& sub, const Icon& trail) {
        const bool clicked = click(id, r);
        if (over(r)) fillRound(r.inset(-4), 14, theme.text, 0.04);
        const Rect ib{r.x, r.cy() - 19, 38, 38};
        fillRound(ib, 13, glyphBg);
        icon(glyph, {ib.x + 9.5, ib.y + 9.5, 19, 19}, glyphColour);
        const double tx = ib.x + ib.w + 13;
        font(13.5, W600);
        text(tx, r.cy() - (sub.empty() ? 0 : 9), title, theme.text);
        if (!sub.empty()) {
            font(11.5, W400);
            text(tx, r.cy() + 10, sub, theme.faint);
        }
        icon(trail, {r.x + r.w - 16, r.cy() - 8, 16, 16}, theme.ghost);
        return clicked;
    }

    // Draws the closed control; the list is drawn later by `menuPopup`.
    bool dropdown(int id, const Rect& r, const std::string& value, bool enabled = true) {
        noteHit(r);
        const bool hov = enabled && r.contains(mouseX, mouseY);
        if (hov) hot = id;
        if (hov && mousePressed) openMenu = (openMenu == id) ? 0 : id;
        fillRound(r, kPill, hov ? mix(theme.well, theme.text, 0.07) : theme.well);
        font(12.5, W500);
        text(r.x + 14, r.cy(), value, enabled ? theme.dim : theme.ghost);
        setColour(enabled ? theme.faint : theme.ghost);
        const double cx = r.x + r.w - 15, cy = r.cy();
        cairo_new_path(cr);
        cairo_move_to(cr, cx - 4, cy - 1.8);
        cairo_line_to(cr, cx + 4, cy - 1.8);
        cairo_line_to(cr, cx, cy + 3.2);
        cairo_close_path(cr);
        cairo_fill(cr);
        return openMenu == id;
    }

    // Returns the chosen index, or -1.  Call after everything else so the list
    // paints above the rest of the frame.
    int menuPopup(int id, const Rect& anchor, const std::vector<std::string>& items,
                  int current, double width = 0) {
        if (openMenu != id) return -1;
        const double rowH = 32;
        const double w = width > 0 ? width : std::max(anchor.w, 200.0);
        const double h = rowH * double(items.size()) + 10;
        Rect box{anchor.x, anchor.y + anchor.h + 6, w, h};

        shadow(box, 16, 16, 0.5, 6);
        fillRound(box, 16, theme.card);
        strokeRound(box, 16, theme.line, 1, 0.8);

        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect row{box.x + 5, box.y + 5 + rowH * double(i), box.w - 10, rowH};
            noteHit(row);
            const bool hov = row.contains(mouseX, mouseY);
            if (hov) fillRound(row, 10, theme.well);
            font(12.5, int(i) == current ? W700 : W500);
            text(row.x + 11, row.cy(), items[i],
                 int(i) == current ? theme.accent : theme.dim);
            if (hov && mouseReleased) chosen = int(i);
        }
        if (mousePressed && !box.contains(mouseX, mouseY) &&
            !anchor.contains(mouseX, mouseY)) openMenu = 0;
        if (chosen >= 0) openMenu = 0;
        return chosen;
    }
};

inline std::string fmt(const char* f, double v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

// Trims to fit, dropping whole UTF-8 characters.  Handing cairo a string cut
// through a multi-byte character puts the context into a permanent error state
// and everything drawn after it is silently discarded.
inline std::string fitText(const Ui& ui, std::string s, double limit) {
    if (ui.textWidth(s) <= limit) return s;
    while (!s.empty()) {
        while (!s.empty()) {                       // drop one character
            const unsigned char c = static_cast<unsigned char>(s.back());
            s.pop_back();
            if ((c & 0xC0) != 0x80) break;         // that was the lead byte
        }
        if (ui.textWidth(s + "…") <= limit) break;
    }
    return s + "…";
}

inline std::string clockText(double seconds) {
    if (seconds < 0 || seconds > 60 * 60 * 24) return "--:--";
    const long total = long(seconds);
    char buf[32];
    if (total >= 3600)
        std::snprintf(buf, sizeof buf, "%ld:%02ld:%02ld",
                      total / 3600, (total / 60) % 60, total % 60);
    else
        std::snprintf(buf, sizeof buf, "%ld:%02ld", total / 60, total % 60);
    return buf;
}

} // namespace eightd
