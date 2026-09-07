// A small immediate-mode widget layer over cairo.
//
// Immediate mode suits this interface: every control is a pure function of a
// value the app already owns, so there is no widget tree to keep in sync and no
// per-widget allocation.  Only the identity of the control the mouse is
// currently working stays between frames.
#pragma once
#include "Theme.h"
#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <string>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <functional>

namespace eightd {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool contains(double px, double py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    Rect inset(double d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

enum class Align { Left, Centre, Right };

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
    Theme theme = Theme::light();

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

    // input state for this frame
    double mouseX = -1, mouseY = -1;
    bool mouseDown = false;      // held
    bool mousePressed = false;   // went down this frame
    bool mouseReleased = false;  // came up this frame
    int  wheel = 0;              // -1 up, +1 down

    // carried between frames
    int active = 0;              // control currently being dragged
    int openMenu = 0;            // dropdown showing its list
    int hot = 0;

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
        mousePressed = false; mouseReleased = false; wheel = 0;
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
    void strokeRound(const Rect& r, double radius, const Rgb& c, double w = 1.0) const {
        setColour(c); cairo_set_line_width(cr, w);
        roundRect(r.inset(w * 0.5), radius); cairo_stroke(cr);
    }

    void font(double size, bool bold = false, bool serif = false) const {
        if (!desc_ || !layout_) return;
        pango_font_description_set_family(desc_, serif ? "DejaVu Serif" : "DejaVu Sans");
        pango_font_description_set_weight(
            desc_, bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
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

    // `y` is the vertical centre of the line.
    void text(double x, double y, const std::string& s, const Rgb& c,
              Align align = Align::Left) const {
        if (!layout_) return;
        pango_layout_set_attributes(layout_, nullptr);
        pango_layout_set_text(layout_, s.c_str(), -1);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        double tx = x;
        if (align == Align::Centre) tx = x - w * 0.5;
        else if (align == Align::Right) tx = x - w;
        setColour(c);
        cairo_move_to(cr, tx, y - h * 0.5);
        pango_cairo_show_layout(cr, layout_);
    }

    // Letter-spaced caps, used for section headings.
    void tracked(double x, double y, const std::string& s, const Rgb& c,
                 double spacing = 1.6) const {
        if (!layout_) return;
        PangoAttrList* attrs = pango_attr_list_new();
        pango_attr_list_insert(attrs,
            pango_attr_letter_spacing_new(int(spacing * PANGO_SCALE)));
        pango_layout_set_text(layout_, s.c_str(), -1);
        pango_layout_set_attributes(layout_, attrs);
        int w = 0, h = 0;
        pango_layout_get_pixel_size(layout_, &w, &h);
        setColour(c);
        cairo_move_to(cr, x, y - h * 0.5);
        pango_cairo_show_layout(cr, layout_);
        pango_layout_set_attributes(layout_, nullptr);
        pango_attr_list_unref(attrs);
    }

    // -- controls ------------------------------------------------------------

    bool button(int id, const Rect& r, const std::string& label,
                const Rgb& bg, const Rgb& fg, bool enabled = true, double radius = 6) {
        noteHit(r);
        const bool over = enabled && r.contains(mouseX, mouseY);
        if (over) hot = id;
        bool clicked = false;
        if (over && mousePressed) active = id;
        if (over && mouseReleased && active == id) clicked = true;

        Rgb fill = bg;
        if (!enabled)      fill = mix(bg, theme.ground, 0.55);
        else if (active == id && over) fill = mix(bg, theme.ink, 0.18);
        else if (over)     fill = mix(bg, theme.ink, 0.08);

        fillRound(r, radius, fill);
        font(14.5, true);
        text(r.x + r.w * 0.5, r.y + r.h * 0.5, label,
             enabled ? fg : theme.inkGhost, Align::Centre);
        return clicked;
    }

    bool ghostButton(int id, const Rect& r, const std::string& label,
                     bool enabled = true) {
        noteHit(r);
        const bool over = enabled && r.contains(mouseX, mouseY);
        if (over) hot = id;
        bool clicked = false;
        if (over && mousePressed) active = id;
        if (over && mouseReleased && active == id) clicked = true;
        fillRound(r, 6, over ? theme.accentSoft : theme.field);
        strokeRound(r, 6, theme.line);
        font(13.5, false);
        text(r.x + r.w * 0.5, r.y + r.h * 0.5, label,
             enabled ? (over ? theme.accentText : theme.inkSoft) : theme.inkGhost,
             Align::Centre);
        return clicked;
    }

    // Returns true while the value is being changed.
    bool slider(int id, const Rect& r, const std::string& label,
                const std::string& readout, double& value, double lo, double hi,
                const std::string& hint = "", bool enabled = true) {
        const double rowH = 20, trackY = r.y + rowH + 12;
        font(14.5, false);
        text(r.x, r.y + rowH * 0.5, label, enabled ? theme.ink : theme.inkGhost);
        font(14.5, true);
        text(r.x + r.w, r.y + rowH * 0.5, readout,
             enabled ? theme.accentText : theme.inkGhost, Align::Right);

        const Rect track{r.x, trackY - 3, r.w, 6};
        fillRound(track, 3, theme.dark ? theme.line : theme.lineSoft);

        const double t = std::clamp((value - lo) / (hi - lo), 0.0, 1.0);
        const double knobX = r.x + t * r.w;
        if (t > 0.001)
            fillRound({r.x, trackY - 3, t * r.w, 6}, 3,
                      enabled ? theme.accent : theme.inkGhost);

        const Rect grab{r.x - 10, trackY - 14, r.w + 20, 28};
        noteHit(grab);
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

        const double kr = (active == id || over) ? 9 : 7.5;
        setColour(enabled ? theme.accent : theme.inkGhost);
        cairo_new_path(cr);
        cairo_arc(cr, knobX, trackY, kr, 0, 2 * M_PI);
        cairo_fill(cr);
        setColour(theme.chrome);
        cairo_new_path(cr);
        cairo_arc(cr, knobX, trackY, kr * 0.42, 0, 2 * M_PI);
        cairo_fill(cr);

        if (!hint.empty()) {
            font(12);
            text(r.x, trackY + 20, hint, theme.inkFaint);
        }
        return changed;
    }

    bool checkbox(int id, const Rect& r, const std::string& label, bool& value) {
        noteHit(r);
        const bool over = r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) active = id;
        bool clicked = false;
        if (over && mouseReleased && active == id) { value = !value; clicked = true; }

        const Rect box{r.x, r.y + r.h * 0.5 - 9, 18, 18};
        fillRound(box, 4, value ? theme.accent : theme.field);
        strokeRound(box, 4, value ? theme.accent : theme.line);
        if (value) {
            setColour(theme.onAccent);
            cairo_set_line_width(cr, 2.2);
            cairo_move_to(cr, box.x + 4.5, box.y + 9);
            cairo_line_to(cr, box.x + 7.8, box.y + 12.6);
            cairo_line_to(cr, box.x + 13.6, box.y + 5.6);
            cairo_stroke(cr);
        }
        font(14);
        text(box.x + 27, r.y + r.h * 0.5, label, theme.ink);
        return clicked;
    }

    // Draws the closed control; the list is drawn later by `menuPopup`.
    bool dropdown(int id, const Rect& r, const std::string& value, bool enabled = true) {
        noteHit(r);
        const bool over = enabled && r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) openMenu = (openMenu == id) ? 0 : id;
        fillRound(r, 6, enabled ? theme.field : mix(theme.field, theme.ground, 0.5));
        strokeRound(r, 6, (openMenu == id || over) ? theme.accent : theme.line);
        font(14);
        text(r.x + 12, r.y + r.h * 0.5, value, enabled ? theme.ink : theme.inkGhost);
        // caret
        setColour(enabled ? theme.inkFaint : theme.inkGhost);
        const double cx = r.x + r.w - 16, cy = r.y + r.h * 0.5;
        cairo_move_to(cr, cx - 4.5, cy - 2);
        cairo_line_to(cr, cx + 4.5, cy - 2);
        cairo_line_to(cr, cx, cy + 3.5);
        cairo_close_path(cr);
        cairo_fill(cr);
        return openMenu == id;
    }

    // Returns the chosen index, or -1.  Call after everything else so the list
    // paints above the rest of the frame.
    int menuPopup(int id, const Rect& anchor, const std::vector<std::string>& items,
                  int current) {
        if (openMenu != id) return -1;
        const double rowH = 30;
        const double h = rowH * double(items.size()) + 8;
        double y = anchor.y + anchor.h + 4;
        Rect box{anchor.x, y, anchor.w, h};

        // shadow, then plate
        fillRound({box.x + 1, box.y + 3, box.w, box.h}, 8, Rgb{0, 0, 0}, 0.18);
        fillRound(box, 8, theme.chrome);
        strokeRound(box, 8, theme.line);

        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect row{box.x + 4, box.y + 4 + rowH * double(i), box.w - 8, rowH};
            noteHit(row);
            const bool over = row.contains(mouseX, mouseY);
            if (over) fillRound(row, 5, theme.accentSoft);
            font(14, int(i) == current);
            text(row.x + 9, row.y + row.h * 0.5, items[i],
                 int(i) == current ? theme.accentText : theme.ink);
            if (over && mouseReleased) chosen = int(i);
        }
        // a click anywhere else dismisses
        if (mousePressed && !box.contains(mouseX, mouseY) &&
            !anchor.contains(mouseX, mouseY)) openMenu = 0;
        if (chosen >= 0) openMenu = 0;
        return chosen;
    }

    int segmented(int id, const Rect& r, const std::vector<std::string>& items,
                  int current) {
        fillRound(r, 7, theme.field);
        strokeRound(r, 7, theme.line);
        const double w = r.w / double(items.size());
        int chosen = -1;
        for (size_t i = 0; i < items.size(); ++i) {
            const Rect cell{r.x + w * double(i), r.y, w, r.h};
            noteHit(cell);
            const bool over = cell.contains(mouseX, mouseY);
            if (int(i) == current) fillRound(cell.inset(2), 5, theme.ink);
            else if (over)         fillRound(cell.inset(2), 5, theme.accentSoft);
            font(13, int(i) == current);
            text(cell.x + cell.w * 0.5, cell.y + cell.h * 0.5, items[i],
                 int(i) == current ? theme.chrome : theme.inkSoft, Align::Centre);
            if (over && mouseReleased) chosen = int(i);
        }
        (void)id;
        return chosen;
    }

    bool chip(int id, const Rect& r, const std::string& label, bool selected) {
        noteHit(r);
        const bool over = r.contains(mouseX, mouseY);
        if (over) hot = id;
        if (over && mousePressed) active = id;
        bool clicked = false;
        if (over && mouseReleased && active == id) clicked = true;
        if (selected)  fillRound(r, 15, theme.accent);
        else if (over) fillRound(r, 15, theme.accentSoft);
        else           fillRound(r, 15, theme.field);
        strokeRound(r, 15, selected ? theme.accent : theme.line);
        font(13, selected);
        text(r.x + r.w * 0.5, r.y + r.h * 0.5, label,
             selected ? theme.onAccent : theme.inkSoft, Align::Centre);
        return clicked;
    }
};

inline std::string fmt(const char* f, double v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

} // namespace eightd
