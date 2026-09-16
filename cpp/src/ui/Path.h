// A small SVG path renderer.
//
// Every glyph in this interface was drawn in the design as an SVG path, so the
// shortest route to "the app looks like the design" is to keep the path data
// and hand it to cairo rather than transcribe curves into C++ by eye.  This
// understands the subset the design uses: M L H V C S Q T A Z, absolute and
// relative.
#pragma once
#include <cairo/cairo.h>
#include <cmath>
#include <cstdlib>
#include <cctype>

namespace eightd {

class SvgPath {
public:
    // `box` is the viewBox side the path was drawn in; the path is placed in
    // the square (x, y, size, size).
    static void add(cairo_t* cr, const char* d, double x, double y,
                    double size, double box) {
        SvgPath p(cr, x, y, size / box);
        p.run(d);
    }

private:
    cairo_t* cr_;
    double ox_, oy_, k_;
    double cx_ = 0, cy_ = 0;        // current point, in path units
    double sx_ = 0, sy_ = 0;        // start of the current subpath
    double rx_ = 0, ry_ = 0;        // reflection of the last control point
    char last_ = 0;
    bool open_ = false;

    SvgPath(cairo_t* cr, double x, double y, double k)
        : cr_(cr), ox_(x), oy_(y), k_(k) {}

    double X(double v) const { return ox_ + v * k_; }
    double Y(double v) const { return oy_ + v * k_; }

    void moveTo(double x, double y) {
        cairo_move_to(cr_, X(x), Y(y));
        cx_ = sx_ = x; cy_ = sy_ = y; open_ = true;
    }
    void lineTo(double x, double y) {
        if (!open_) { moveTo(x, y); return; }
        cairo_line_to(cr_, X(x), Y(y));
        cx_ = x; cy_ = y;
    }
    void curveTo(double x1, double y1, double x2, double y2, double x, double y) {
        if (!open_) moveTo(cx_, cy_);
        cairo_curve_to(cr_, X(x1), Y(y1), X(x2), Y(y2), X(x), Y(y));
        rx_ = x2; ry_ = y2;
        cx_ = x; cy_ = y;
    }

    // Quadratic, promoted to the cubic cairo speaks.
    void quadTo(double qx, double qy, double x, double y) {
        const double x1 = cx_ + 2.0 / 3.0 * (qx - cx_);
        const double y1 = cy_ + 2.0 / 3.0 * (qy - cy_);
        const double x2 = x + 2.0 / 3.0 * (qx - x);
        const double y2 = y + 2.0 / 3.0 * (qy - y);
        curveTo(x1, y1, x2, y2, x, y);
        rx_ = qx; ry_ = qy;          // T reflects the quadratic control point
    }

    // Endpoint parameterisation, as in the SVG specification's appendix F.6.
    // Once the centre and the two angles are known, cairo draws the arc for us
    // under a scale, which also handles the elliptical case for free.
    void arcTo(double rx, double ry, double rot, bool large, bool sweep,
               double x, double y) {
        if (rx == 0 || ry == 0) { lineTo(x, y); return; }
        rx = std::fabs(rx); ry = std::fabs(ry);
        const double phi = rot * M_PI / 180.0;
        const double dx2 = (cx_ - x) * 0.5, dy2 = (cy_ - y) * 0.5;
        const double x1 =  std::cos(phi) * dx2 + std::sin(phi) * dy2;
        const double y1 = -std::sin(phi) * dx2 + std::cos(phi) * dy2;

        // Grow the radii if they cannot span the two points.
        const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
        if (lambda > 1) { const double s = std::sqrt(lambda); rx *= s; ry *= s; }

        const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double c = den > 0 ? std::sqrt(std::max(num / den, 0.0)) : 0.0;
        if (large == sweep) c = -c;
        const double cxp =  c * rx * y1 / ry;
        const double cyp = -c * ry * x1 / rx;
        const double ccx = std::cos(phi) * cxp - std::sin(phi) * cyp + (cx_ + x) * 0.5;
        const double ccy = std::sin(phi) * cxp + std::cos(phi) * cyp + (cy_ + y) * 0.5;

        const double th1 = std::atan2((y1 - cyp) / ry, (x1 - cxp) / rx);
        double th2 = std::atan2((-y1 - cyp) / ry, (-x1 - cxp) / rx);
        if (!sweep && th2 > th1) th2 -= 2 * M_PI;
        else if (sweep && th2 < th1) th2 += 2 * M_PI;

        if (!open_) moveTo(cx_, cy_);
        cairo_save(cr_);
        cairo_translate(cr_, X(ccx), Y(ccy));
        cairo_rotate(cr_, phi);
        cairo_scale(cr_, rx * k_, ry * k_);
        if (sweep) cairo_arc(cr_, 0, 0, 1, th1, th2);
        else       cairo_arc_negative(cr_, 0, 0, 1, th1, th2);
        cairo_restore(cr_);
        cx_ = x; cy_ = y;
        rx_ = x; ry_ = y;
    }

    // -- the scanner ------------------------------------------------------

    const char* s_ = nullptr;

    void skip() {
        while (*s_ && (std::isspace((unsigned char)*s_) || *s_ == ',')) ++s_;
    }
    bool more() {
        skip();
        return *s_ && (std::isdigit((unsigned char)*s_) || *s_ == '-' ||
                       *s_ == '+' || *s_ == '.');
    }
    double num() {
        skip();
        char* end = nullptr;
        const double v = std::strtod(s_, &end);
        s_ = end ? end : s_;
        return v;
    }
    bool flag() {
        skip();
        const bool v = (*s_ == '1');
        if (*s_) ++s_;
        return v;
    }

    void run(const char* d) {
        s_ = d;
        char cmd = 0;
        while (true) {
            skip();
            if (!*s_) break;
            if (std::isalpha((unsigned char)*s_)) { cmd = *s_++; }
            else if (!cmd) break;                       // junk before a command
            else if (cmd == 'M') cmd = 'L';             // repeats become lines
            else if (cmd == 'm') cmd = 'l';

            const bool rel = std::islower((unsigned char)cmd);
            const double px = rel ? cx_ : 0, py = rel ? cy_ : 0;

            switch (std::toupper((unsigned char)cmd)) {
            case 'M': { const double x = num() + px, y = num() + py; moveTo(x, y); break; }
            case 'L': { const double x = num() + px, y = num() + py; lineTo(x, y); break; }
            case 'H': { const double x = num() + px; lineTo(x, cy_); break; }
            case 'V': { const double y = num() + py; lineTo(cx_, y); break; }
            case 'C': {
                const double x1 = num() + px, y1 = num() + py;
                const double x2 = num() + px, y2 = num() + py;
                const double x  = num() + px, y  = num() + py;
                curveTo(x1, y1, x2, y2, x, y);
                break;
            }
            case 'S': {
                const bool smooth = last_ == 'C' || last_ == 'S';
                const double x1 = smooth ? 2 * cx_ - rx_ : cx_;
                const double y1 = smooth ? 2 * cy_ - ry_ : cy_;
                const double x2 = num() + px, y2 = num() + py;
                const double x  = num() + px, y  = num() + py;
                curveTo(x1, y1, x2, y2, x, y);
                break;
            }
            case 'Q': {
                const double qx = num() + px, qy = num() + py;
                const double x  = num() + px, y  = num() + py;
                quadTo(qx, qy, x, y);
                break;
            }
            case 'T': {
                const bool smooth = last_ == 'Q' || last_ == 'T';
                const double qx = smooth ? 2 * cx_ - rx_ : cx_;
                const double qy = smooth ? 2 * cy_ - ry_ : cy_;
                const double x = num() + px, y = num() + py;
                quadTo(qx, qy, x, y);
                break;
            }
            case 'A': {
                const double rx = num(), ry = num(), rot = num();
                const bool large = flag(), sweep = flag();
                const double x = num() + px, y = num() + py;
                arcTo(rx, ry, rot, large, sweep, x, y);
                break;
            }
            case 'Z':
                if (open_) cairo_close_path(cr_);
                cx_ = sx_; cy_ = sy_; open_ = false;
                break;
            default:
                return;                                  // unknown command
            }
            last_ = char(std::toupper((unsigned char)cmd));
            if (std::toupper((unsigned char)cmd) == 'Z') { skip(); continue; }
            if (!more()) { /* next loop reads a new command */ }
        }
    }
};

} // namespace eightd
