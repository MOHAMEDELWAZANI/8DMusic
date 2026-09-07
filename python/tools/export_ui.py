"""Export the interface to PNGs and a multi-page PDF.

Captures the real running window in each theme, so what lands in the PDF is
exactly what the app draws -- no mockup, no redraw.  The engine is never
started, so system audio is left alone.

    ./.venv/bin/python tools/export_ui.py [-o design]
"""

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import sys
import time
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

THEMES = (("light", "Thème clair (Light)"), ("dark", "Thème sombre (Dark)"))

#: The right-hand rail scrolls, so each theme is captured twice -- otherwise the
#: Space and Output controls never appear in the export at all.
VIEWS = ((0.0, "haut du panneau", "top"), (1.0, "bas du panneau", "bottom"))

_XWD_FIELDS = (
    "header_size file_version pixmap_format pixmap_depth pixmap_width "
    "pixmap_height xoffset byte_order bitmap_unit bitmap_bit_order "
    "bitmap_pad bits_per_pixel bytes_per_line visual_class red_mask "
    "green_mask blue_mask bits_per_rgb colormap_entries ncolors "
    "window_width window_height window_x window_y window_bdrwidth"
).split()


def decode_xwd(data: bytes) -> Image.Image:
    """Decode an X Window Dump into an RGB image."""
    for endian in (">", "<"):
        head = dict(zip(_XWD_FIELDS, struct.unpack(endian + "25I", data[:100])))
        if head["file_version"] == 7 and head["header_size"] >= 100:
            break
    else:
        raise ValueError("not an XWD file")

    w, h = head["pixmap_width"], head["pixmap_height"]
    bpl, bpp = head["bytes_per_line"], head["bits_per_pixel"]
    if bpp not in (24, 32):
        raise ValueError(f"unsupported bits_per_pixel {bpp}")

    start = head["header_size"] + head["ncolors"] * 12
    pixels = data[start:]
    need = bpl * h
    if len(pixels) < need:
        raise ValueError(f"short pixel data: {len(pixels)} < {need}")

    def shift(mask: int) -> int:
        s = 0
        while mask and not (mask >> s) & 1:
            s += 1
        return s

    rm, gm, bm = head["red_mask"], head["green_mask"], head["blue_mask"]
    rs, gs, bs = shift(rm), shift(gm), shift(bm)

    nbytes = bpp // 8
    buf = np.frombuffer(pixels[:need], dtype=np.uint8).reshape(h, bpl)
    buf = buf[:, : w * nbytes].reshape(h, w, nbytes)
    if nbytes == 3:
        buf = np.dstack([buf, np.zeros((h, w, 1), np.uint8)])
    if head["byte_order"] == 1:            # MSBFirst
        buf = buf[:, :, ::-1]
    v = buf.astype(np.uint32)
    word = v[:, :, 0] | (v[:, :, 1] << 8) | (v[:, :, 2] << 16) | (v[:, :, 3] << 24)
    rgb = np.dstack([((word & rm) >> rs).astype(np.uint8),
                     ((word & gm) >> gs).astype(np.uint8),
                     ((word & bm) >> bs).astype(np.uint8)])
    return Image.fromarray(rgb, "RGB")


def grab_window(window_id: int) -> Image.Image:
    """Capture one X window by id.

    Goes through the window rather than the root: under XWayland the root
    holds no pixels, but each X client's own window still reads back fine.
    """
    res = subprocess.run(["xwd", "-id", str(window_id), "-silent"],
                         capture_output=True, timeout=20)
    if res.returncode != 0 or not res.stdout:
        raise RuntimeError(f"xwd failed: {res.stderr.decode(errors='replace')[:200]}")
    return decode_xwd(res.stdout)


def settle(app, seconds: float) -> None:
    """Pump the event loop so fonts, canvases and animations finish drawing."""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        app.update_idletasks()
        app.update()
        time.sleep(0.02)


def load_font(size: int, bold: bool = False):
    names = ["DejaVuSans-Bold.ttf" if bold else "DejaVuSans.ttf"]
    for name in names:
        for folder in ("/usr/share/fonts/truetype/dejavu/",
                       "/usr/share/fonts/truetype/noto/"):
            path = Path(folder) / name
            if path.exists():
                return ImageFont.truetype(str(path), size)
    return ImageFont.load_default()


def make_page(shot: Image.Image, title: str, subtitle: str,
              margin: int = 90) -> Image.Image:
    """Lay one screenshot on a neutral page with a caption above it."""
    title_font = load_font(46, bold=True)
    sub_font = load_font(30)

    head = 150
    page = Image.new("RGB",
                     (shot.width + margin * 2, shot.height + margin * 2 + head),
                     "#ffffff")
    draw = ImageDraw.Draw(page)
    draw.text((margin, margin - 20), title, font=title_font, fill="#111318")
    draw.text((margin, margin + 44), subtitle, font=sub_font, fill="#6a7385")

    top = margin + head
    draw.rectangle([margin - 1, top - 1, margin + shot.width, top + shot.height],
                   outline="#d5dae3", width=2)
    page.paste(shot, (margin, top))
    return page


def write_pdf(pages: list[Image.Image], path: Path, dpi: int = 150) -> None:
    """Write the pages losslessly.

    Pillow's own PDF writer re-encodes RGB as JPEG, which frays the thin text
    and flat fills of a UI screenshot.  img2pdf embeds the PNG bytes as-is, so
    what Claude Design receives is pixel-identical to the capture.
    """
    import io

    try:
        import img2pdf
    except ImportError:
        pages[0].save(path, "PDF", resolution=float(dpi), save_all=True,
                      append_images=pages[1:])
        print("  ! img2pdf not installed -- fell back to JPEG-compressed PDF",
              file=sys.stderr)
        return

    blobs = []
    for page in pages:
        buf = io.BytesIO()
        page.save(buf, "PNG", optimize=True)
        blobs.append(buf.getvalue())
    layout = img2pdf.get_layout_fun(
        (img2pdf.px_to_pt(pages[0].width, dpi), img2pdf.px_to_pt(pages[0].height, dpi))
    )
    path.write_bytes(img2pdf.convert(blobs, layout_fun=layout))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--out", default="design",
                        help="output folder, relative to the project root")
    parser.add_argument("--display", default=os.environ.get("DISPLAY", ":0"))
    args = parser.parse_args()

    os.environ["DISPLAY"] = args.display
    out_dir = (ROOT / args.out) if not Path(args.out).is_absolute() else Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    from eight_d.ui import App

    app = App()
    app.attributes("-topmost", True)
    app.lift()
    app.update_idletasks()
    settle(app, 1.6)

    window_id = app.winfo_id()
    pages, shots = [], []

    rail = getattr(app, "_rail_canvas", None)

    for theme, label in THEMES:
        if app.theme != theme:
            app.toggle_theme()
        settle(app, 1.0)

        for fraction, view_fr, view_en in VIEWS:
            if rail is not None:
                rail.yview_moveto(fraction)
            settle(app, 0.9)

            shot = grab_window(window_id)
            lo, hi = shot.convert("L").getextrema()
            if lo == hi:
                print(f"  ! {theme}/{view_en}: window came back blank -- covered?",
                      file=sys.stderr)
                app.destroy()
                return 1

            png = out_dir / f"8dmusic-{theme}-{view_en}.png"
            shot.save(png)
            shots.append((theme, view_en, shot, png))
            pages.append(make_page(
                shot, f"8D Music — {label}",
                f"Panneau de réglages : {view_fr} · {shot.width}x{shot.height} px"
                " · capture réelle",
            ))
            print(f"  {theme:5s} {view_en:6s} -> {png.relative_to(ROOT)}"
                  f"  ({shot.width}x{shot.height})")

    # Leave the app as we found it: no config write, so the saved theme stands.
    app.destroy()

    pdf = out_dir / "8dmusic-interface.pdf"
    write_pdf(pages, pdf)
    print(f"  PDF   -> {pdf.relative_to(ROOT)}  ({len(pages)} pages)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
