#!/usr/bin/env python3
"""Render the 8D Music app icons from the logo specification.

The mark is four fully-rounded level bars seated on a baseline with an arc
sweeping beneath them, and a dot marking the source.  Small sizes drop bars so
the shape stays legible: four at 128 and up, three at 64 and 32, two at 16.

Run this only when the mark changes -- the PNGs it writes are committed, so the
application itself needs nothing beyond Tk to display them.

    python3 tools/make_icons.py        # needs Pillow

Geometry is given in the logo's 120x104 viewBox and scaled to each tile.
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent.parent / "eight_d" / "assets"

SS = 8                      # supersampling factor -- PIL will not antialias
VIEW_W, VIEW_H = 120.0, 104.0
CONTENT = 0.70              # fraction of the tile the mark occupies
CORNER = 0.22               # tile corner radius as a fraction of its size

INK_DARK, INK_LIGHT = "#201e1d", "#f3f2f2"

#: `sub` is the strapline: 50% ink over the lockup's ground, flattened because
#: the lockup PNGs are transparent and cannot carry the alpha themselves.
THEMES = {
    "dark": dict(bars="#f3f2f2", sweep="#62c5ee", dot="#ff458e", sub="#8a8888",
                 tile=("#2a2826", "#171615"), dot_at="start"),
    "light": dict(bars="#201e1d", sweep="#0088b0", dot="#d6006c", sub="#8a8888",
                  tile=("#fbfafa", "#eceaea"), dot_at="end"),
}

#: (bars, sweep, dot_radius) per size band.  Bars are (x, width, height) with
#: every bar seated on the baseline at y=66.
VARIANTS = {
    4: (
        [(24, 13, 32), (46, 13, 52), (68, 13, 40), (90, 13, 22)],
        ((10, 74), (60, 106), (110, 74), 8),
        9.5,
    ),
    3: (
        [(26, 16, 36), (52, 16, 54), (78, 16, 28)],
        ((10, 76), (60, 108), (110, 76), 11),
        0.0,
    ),
    2: (
        [(30, 20, 40), (70, 20, 56)],
        ((14, 80), (60, 112), (106, 80), 18),
        0.0,
    ),
}
BASELINE = 66.0


def bars_for(size: int) -> int:
    if size >= 128:
        return 4
    if size >= 32:
        return 3
    return 2


def _bezier(p0, p1, p2, steps=160):
    """Sample the quadratic the SVG path describes."""
    for i in range(steps + 1):
        t = i / steps
        u = 1.0 - t
        yield (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
               u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])


def _tile(size: int, top: str, bottom: str) -> Image.Image:
    """A rounded square with the icon's radial ground."""
    n = size * SS
    img = Image.new("RGB", (n, n))
    put = img.load()
    tr = tuple(int(top[i:i + 2], 16) for i in (1, 3, 5))
    br = tuple(int(bottom[i:i + 2], 16) for i in (1, 3, 5))
    cx, cy = n * 0.5, n * 0.40
    reach = max(math.hypot(cx - x, cy - y) for x in (0, n) for y in (0, n))
    for y in range(n):
        for x in range(n):
            t = min(math.hypot(x - cx, y - cy) / reach, 1.0)
            put[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(tr, br))

    mask = Image.new("L", (n, n), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        (0, 0, n - 1, n - 1), radius=int(CORNER * n), fill=255)
    out = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    out.paste(img, (0, 0), mask)
    return out


def _paint_mark(draw, ox: float, oy: float, scale: float, spec: dict,
                count: int) -> None:
    """Draw the mark into an existing image, its viewBox mapped to ox/oy/scale."""
    bars, (p0, p1, p2, stroke), dot_r = VARIANTS[count]

    def X(v):
        return ox + v * scale

    def Y(v):
        return oy + v * scale

    for x, w, h in bars:
        draw.rounded_rectangle(
            (X(x), Y(BASELINE - h), X(x + w), Y(BASELINE)),
            radius=w / 2.0 * scale, fill=spec["bars"])

    # The sweep, drawn as overlapping discs so the caps and joins are round.
    half = stroke / 2.0 * scale
    for x, y in _bezier(p0, p1, p2):
        draw.ellipse((X(x) - half, Y(y) - half, X(x) + half, Y(y) + half),
                     fill=spec["sweep"])

    if dot_r:
        end = p0 if spec["dot_at"] == "start" else p2
        r = dot_r * scale
        draw.ellipse((X(end[0]) - r, Y(end[1]) - r, X(end[0]) + r, Y(end[1]) + r),
                     fill=spec["dot"])


def render(size: int, theme: str) -> Image.Image:
    spec = THEMES[theme]
    img = _tile(size, *spec["tile"])
    n = size * SS
    scale = (n * CONTENT) / VIEW_W
    _paint_mark(ImageDraw.Draw(img), (n - VIEW_W * scale) / 2.0,
                (n - VIEW_H * scale) / 2.0, scale, spec, bars_for(size))
    return img.resize((size, size), Image.LANCZOS)


# --------------------------------------------------------------------------
# the primary lockup
# --------------------------------------------------------------------------

TITLE, SUBTITLE = "8D Music", "SPATIAL AUDIO, LIVE"
TITLE_PT, SUB_PT = 54, 15
SUB_TRACKING = 0.18          # em, per the identity sheet
MARK_H, GAP, PAD = 108, 24, 10

SERIF_BOLD = ("/usr/share/fonts/truetype/noto/NotoSerif-Bold.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf")
SERIF_BOOK = ("/usr/share/fonts/truetype/noto/NotoSerif-Regular.ttf",
              "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
              "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf")


def _font(candidates, size: int) -> ImageFont.FreeTypeFont:
    for path in candidates:
        if Path(path).exists():
            return ImageFont.truetype(path, size)
    raise SystemExit(f"no serif font found; looked for {candidates[0]}")


def _tracked_width(text: str, font, tracking: float) -> float:
    return sum(font.getlength(c) for c in text) + tracking * (len(text) - 1)


def _draw_tracked(draw, x, y, text, font, fill, tracking):
    """PIL has no letter-spacing, so advance a glyph at a time."""
    for ch in text:
        draw.text((x, y), ch, font=font, fill=fill)
        x += font.getlength(ch) + tracking


def lockup(theme: str) -> Image.Image:
    """Mark, wordmark and strapline on a transparent ground."""
    spec = THEMES[theme]
    title_font = _font(SERIF_BOLD, TITLE_PT * SS)
    sub_font = _font(SERIF_BOOK, SUB_PT * SS)
    tracking = SUB_TRACKING * SUB_PT * SS

    mark_w = VIEW_W / VIEW_H * MARK_H * SS
    text_x = PAD * SS + mark_w + GAP * SS
    text_w = max(title_font.getlength(TITLE),
                 _tracked_width(SUBTITLE, sub_font, tracking))

    width = int(text_x + text_w + PAD * SS)
    height = int((MARK_H + PAD * 2) * SS)
    img = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)

    _paint_mark(draw, PAD * SS, PAD * SS, MARK_H * SS / VIEW_H, spec, 4)

    # centre the two text lines against the mark
    t_top, t_bot = title_font.getbbox(TITLE)[1], title_font.getbbox(TITLE)[3]
    s_top, s_bot = sub_font.getbbox(SUBTITLE)[1], sub_font.getbbox(SUBTITLE)[3]
    block = (t_bot - t_top) + 12 * SS + (s_bot - s_top)
    top = (height - block) / 2

    draw.text((text_x, top - t_top), TITLE, font=title_font, fill=spec["bars"])
    _draw_tracked(draw, text_x, top + (t_bot - t_top) + 12 * SS - s_top,
                  SUBTITLE, sub_font, spec["sub"], tracking)

    return img.resize((width // SS, height // SS), Image.LANCZOS)


def main() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    written = []
    for size in (16, 24, 32, 48, 64, 128, 256, 512):
        path = OUT / f"icon-{size}.png"
        render(size, "dark").save(path)
        written.append(path.name)
    render(512, "light").save(OUT / "icon-light-512.png")
    written.append("icon-light-512.png")
    for theme in ("light", "dark"):
        name = f"logo-lockup-{theme}.png"
        lockup(theme).save(OUT / name)
        written.append(name)
    print(f"wrote {len(written)} files to {OUT}")
    for name in written:
        print(" ", name)


if __name__ == "__main__":
    main()
