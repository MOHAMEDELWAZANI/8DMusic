// SVG path data, drawn with GDI+.
//
// The glyphs live in cpp/src/ui/Icons.h as path strings, shared with the Linux
// build so the two cannot drift apart.  That file knows nothing about drawing;
// this is the half that turns it into a GraphicsPath, and it is a transcription
// of cpp/src/ui/Path.h rather than a second design.
//
// The subset the icons use: M L H V C S Q T A Z, absolute and relative.  No
// glyph rotates its arcs, so the x-axis rotation is read and ignored.
#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <cmath>
#include <cstdlib>
#include <cctype>

namespace eightd {

class SvgPath {
public:
    // `box` is the viewBox side the path was drawn in; the path is placed in
    // the square (x, y, size, size).
    static void add(Gdiplus::GraphicsPath& out, const char* d, double x, double y,
                    double size, double box) {
        if (!d) return;
        SvgPath p(out, x, y, size / box);
        p.run(d);
    }

private:
    Gdiplus::GraphicsPath& p_;
    double ox_, oy_, k_;
    double cx_ = 0, cy_ = 0;        // current point, in path units
    double sx_ = 0, sy_ = 0;        // start of the current subpath
    double rx_ = 0, ry_ = 0;        // reflection of the last control point
    char last_ = 0;
    bool open_ = false;

    SvgPath(Gdiplus::GraphicsPath& out, double x, double y, double k)
        : p_(out), ox_(x), oy_(y), k_(k) {}

    float X(double v) const { return float(ox_ + v * k_); }
    float Y(double v) const { return float(oy_ + v * k_); }

    void moveTo(double x, double y) {
        p_.StartFigure();
        cx_ = sx_ = x; cy_ = sy_ = y; open_ = true;
    }
    void lineTo(double x, double y) {
        if (!open_) { moveTo(x, y); return; }
        p_.AddLine(X(cx_), Y(cy_), X(x), Y(y));
        cx_ = x; cy_ = y;
    }
    void curveTo(double x1, double y1, double x2, double y2, double x, double y) {
        if (!open_) moveTo(cx_, cy_);
        p_.AddBezier(X(cx_), Y(cy_), X(x1), Y(y1), X(x2), Y(y2), X(x), Y(y));
        rx_ = x2; ry_ = y2;
        cx_ = x; cy_ = y;
    }

    // Quadratic, promoted to the cubic GDI+ speaks.
    void quadTo(double qx, double qy, double x, double y) {
        const double x1 = cx_ + 2.0 / 3.0 * (qx - cx_);
        const double y1 = cy_ + 2.0 / 3.0 * (qy - cy_);
        const double x2 = x + 2.0 / 3.0 * (qx - x);
        const double y2 = y + 2.0 / 3.0 * (qy - y);
        curveTo(x1, y1, x2, y2, x, y);
        rx_ = qx; ry_ = qy;          // T reflects the quadratic control point
    }

    // Endpoint parameterisation, as in the SVG specification's appendix F.6.
    // GDI+ measures its arc angles the same way -- degrees clockwise from three
    // o'clock, parametric on the ellipse -- so the two angles go straight in.
    void arcTo(double rx, double ry, double rot, bool large, bool sweep,
               double x, double y) {
        if (rx == 0 || ry == 0) { lineTo(x, y); return; }
        (void)rot;                       // no glyph here rotates its arcs
        rx = std::fabs(rx); ry = std::fabs(ry);
        const double dx2 = (cx_ - x) * 0.5, dy2 = (cy_ - y) * 0.5;

        // Grow the radii if they cannot span the two points.
        const double lambda = (dx2 * dx2) / (rx * rx) + (dy2 * dy2) / (ry * ry);
        if (lambda > 1) { const double s = std::sqrt(lambda); rx *= s; ry *= s; }

        const double num = rx * rx * ry * ry - rx * rx * dy2 * dy2 - ry * ry * dx2 * dx2;
        const double den = rx * rx * dy2 * dy2 + ry * ry * dx2 * dx2;
        double c = den > 0 ? std::sqrt(num / den > 0 ? num / den : 0.0) : 0.0;
        if (large == sweep) c = -c;
        const double ccx = c * rx * dy2 / ry + (cx_ + x) * 0.5;
        const double ccy = -c * ry * dx2 / rx + (cy_ + y) * 0.5;

        const double th1 = std::atan2((cy_ - ccy) / ry, (cx_ - ccx) / rx);
        double th2 = std::atan2((y - ccy) / ry, (x - ccx) / rx);
        double delta = th2 - th1;
        if (!sweep && delta > 0) delta -= 2 * kPi;
        else if (sweep && delta < 0) delta += 2 * kPi;

        if (!open_) moveTo(cx_, cy_);
        const double deg = 180.0 / kPi;
        p_.AddArc(X(ccx - rx), Y(ccy - ry), float(rx * 2 * k_), float(ry * 2 * k_),
                  float(th1 * deg), float(delta * deg));
        cx_ = x; cy_ = y;
        rx_ = x; ry_ = y;
    }

    static constexpr double kPi = 3.14159265358979323846;

    // -- the scanner ------------------------------------------------------

    const char* s_ = nullptr;

    void skip() {
        while (*s_ && (std::isspace(static_cast<unsigned char>(*s_)) || *s_ == ',')) ++s_;
    }
    bool more() {
        skip();
        return *s_ && (std::isdigit(static_cast<unsigned char>(*s_)) || *s_ == '-' ||
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
            if (std::isalpha(static_cast<unsigned char>(*s_))) { cmd = *s_++; }
            else if (!cmd) break;                       // junk before a command
            else if (cmd == 'M') cmd = 'L';             // repeats become lines
            else if (cmd == 'm') cmd = 'l';

            const bool rel = std::islower(static_cast<unsigned char>(cmd)) != 0;
            const double px = rel ? cx_ : 0, py = rel ? cy_ : 0;

            switch (std::toupper(static_cast<unsigned char>(cmd))) {
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
                if (open_) p_.CloseFigure();
                cx_ = sx_; cy_ = sy_; open_ = false;
                break;
            default:
                return;                                  // unknown command
            }
            last_ = char(std::toupper(static_cast<unsigned char>(cmd)));
            if (std::toupper(static_cast<unsigned char>(cmd)) == 'Z') { skip(); continue; }
            if (!more()) { /* next loop reads a new command */ }
        }
    }
};

} // namespace eightd
