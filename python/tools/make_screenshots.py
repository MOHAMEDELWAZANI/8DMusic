"""Render the dark interface images used in the README.

The pictures come from the design mockups in ``8D Audio Effect Tool UI`` rather
than from a running window: the app is a Tk program on a Wayland desktop, where
the compositor refuses window grabs to anything but its own portal, so there is
no way to capture it unattended.  The mockups are the same layout the interface
was built to, so rendering them gives an honest picture of the design -- with
the caveat that Tk fakes the gradients, blur and rounded corners the browser
draws for real.

The one thing the mockups predate is the now-playing panel, so it is composed in
here from the same measurements the widget uses.

    python3 tools/make_screenshots.py

Needs a Chromium-family browser for the actual rasterising and nothing else.
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MOCKUPS = ROOT / "8D Audio Effect Tool UI" / "8D Music - Mockups.dc.html"
FONTS = ROOT / "eight_d" / "assets" / "fonts"
OUT = ROOT / "docs"

#: Snap-confined browsers cannot read hidden directories, so the scratch files
#: have to sit somewhere plainly visible.
WORK = Path.home() / "8dmusic-render"

BROWSERS = ("brave", "brave-browser", "chromium", "chromium-browser",
            "google-chrome", "google-chrome-stable")

#: The stage positions its orbit, compass and meters absolutely against this
#: exact height -- grow it and the "BACK" label lands on top of the meters -- so
#: it stays as designed and the rail simply clips at the bottom.  That is what
#: the real window does too: the rail scrolls, and a short window shows less of
#: it.
FRAME_HEIGHT = 720
TITLE_BAR = 43

INK = "#f3f2f2"
GROUND = "#1a1918"
ACCENT = "#62c5ee"
MOTION = "#ff458e"

SERIF = "'Source Serif 4',serif"
LATIN = "'OffBit',serif"
ARABIC = "'KO Methlama 8D',serif"


def _soft(alpha: float) -> str:
    return f"rgba(243,242,242,{alpha})"


FONT_FACES = f"""
@font-face {{ font-family:'OffBit'; src:url('file://{FONTS}/OffBit-101Bold.ttf'); }}
@font-face {{ font-family:'KO Methlama 8D'; src:url('file://{FONTS}/ko-methlama-8d.otf'); }}
"""


def transport(playing: bool = True) -> str:
    """The three drawn transport glyphs, as the widget paints them."""
    def svg(body: str, dim: bool = False) -> str:
        colour = _soft(0.4) if dim else INK
        return (f'<svg width="22" height="22" viewBox="0 0 22 22" fill="{colour}">'
                f'{body}</svg>')

    prev = svg('<rect x="3.3" y="4.6" width="2" height="12.8"/>'
               '<polygon points="18.7,4.6 18.7,17.4 6.4,11"/>')
    middle = ('<rect x="5.9" y="4" width="3.1" height="14"/>'
              '<rect x="13" y="4" width="3.1" height="14"/>') if playing else \
             '<polygon points="5.7,3.5 5.7,18.5 18,11"/>'
    nxt = svg('<polygon points="3.3,4.6 3.3,17.4 15.6,11"/>'
              '<rect x="16.7" y="4.6" width="2" height="12.8"/>')
    return (f'<div style="display:flex;align-items:center;gap:16px">'
            f'{prev}{svg(middle)}{nxt}</div>')


def panel(*, title: str, artist: str, cover: str, source: str,
          elapsed: str, total: str, progress: float, arabic: bool = False) -> str:
    """The now-playing block, laid out to the widget's own measurements."""
    face = ARABIC if arabic else LATIN
    # Arabic is set by ascent rather than nominal size, exactly as the app does.
    title_size, artist_size, cover_size = ("21px", "16px", "26px") if arabic \
        else ("19px", "14px", "24px")
    align = "right" if arabic else "left"
    direction = "rtl" if arabic else "ltr"
    # The widget pins each line to a row of the taller script's height so the
    # panel cannot resize between tracks; mirror that here or the two examples
    # would not line up with each other.
    rows = "display:flex;align-items:center;height:{}px;"
    title_row, artist_row = rows.format(26), rows.format(20)

    return f"""
    <div style="display:flex;flex-direction:column">
      <div style="display:flex;justify-content:space-between;align-items:baseline">
        <span style="font:600 11px/1 {SERIF};letter-spacing:.24em;
                     text-transform:uppercase;color:{ACCENT}">Now Playing</span>
        <span style="font:400 12px/1 {SERIF};color:{_soft(0.5)}">{source}</span>
      </div>
      <div style="display:flex;gap:14px;margin-top:13px">
        <div style="width:64px;height:64px;flex:none;background:{INK};color:{GROUND};
                    display:flex;align-items:center;justify-content:center;
                    font:400 {cover_size} {face}">{cover}</div>
        <div style="flex:1;min-width:0">
          <div style="{title_row}font:400 {title_size}/1 {face};color:{INK};
                      direction:{direction};text-align:{align};white-space:nowrap;
                      overflow:hidden;text-overflow:ellipsis">{title}</div>
          <div style="{artist_row}font:400 {artist_size}/1 {face};color:{_soft(0.6)};
                      margin-top:3px;direction:{direction};text-align:{align};
                      white-space:nowrap;overflow:hidden;
                      text-overflow:ellipsis">{artist}</div>
          <div style="display:flex;align-items:center;gap:9px;margin-top:11px">
            <span style="font:400 12px/1 {SERIF};color:{_soft(0.5)}">{elapsed}</span>
            <div style="flex:1;height:3px;background:{_soft(0.14)};position:relative">
              <div style="position:absolute;left:0;top:0;height:3px;
                          width:{progress * 100:.0f}%;background:{MOTION}"></div>
              <div style="position:absolute;left:{progress * 100:.0f}%;top:50%;
                          transform:translate(-50%,-50%);width:13px;height:13px;
                          border-radius:50%;background:{MOTION}"></div>
            </div>
            <span style="font:400 12px/1 {SERIF};color:{_soft(0.5)}">{total}</span>
          </div>
        </div>
      </div>
      <div style="display:flex;align-items:center;justify-content:space-between;
                  margin-top:14px">
        {transport()}
        <span style="padding:11px 34px;background:{MOTION};color:#201e1d;
                     font:600 15px/1 {SERIF};letter-spacing:.04em">Stop</span>
      </div>
    </div>
    <div style="height:1px;background:{_soft(0.12)}"></div>
    """


LATIN_TRACK = dict(title="Love The Way You Lie", artist="Eminem · Rihanna",
                   cover="Er", source="Spotify · captured",
                   elapsed="1:47", total="4:23", progress=0.41)

ARABIC_TRACK = dict(title="فكروني", artist="أم كلثوم",
                    cover="أم", source="Brave · captured",
                    elapsed="12:38", total="47:20", progress=0.27, arabic=True)


def dark_frame() -> str:
    """The 1b "Ink ground" mockup, with the now-playing panel dropped in."""
    html = MOCKUPS.read_text(encoding="utf-8")
    block = html[html.index('<div class="dv-opt" id="1b">'):
                 html.index('<div class="dv-opt" id="1c">')]
    frame = block[block.index('<div style="width:1180px'):]
    frame = frame[:frame.rindex("</div>")]

    # The mockup opens the rail with a bare Stop button; the panel now owns that
    # row, so swap the whole thing out.
    stop_row = re.search(
        r'<div style="display:flex;justify-content:flex-end">\s*<span[^>]*>Stop</span>\s*</div>',
        frame)
    if not stop_row:
        raise SystemExit("could not find the Stop button row in the mockup")
    return frame.replace(stop_row.group(0), panel(**LATIN_TRACK)) \
                .replace("height:720px", f"height:{FRAME_HEIGHT}px")


def document(body: str, background: str = GROUND, pad: int = 0) -> str:
    return f"""<!DOCTYPE html><html><head><meta charset="utf-8"><style>
{FONT_FACES}
*{{box-sizing:border-box}}
body{{margin:0;padding:{pad}px;background:{background};
     font-family:{SERIF};-webkit-font-smoothing:antialiased}}
</style></head><body>{body}</body></html>"""


def rail_card(track: dict) -> str:
    """One panel on its own, at the width it occupies in the rail."""
    return (f'<div style="width:404px;padding:26px 30px;background:{GROUND}">'
            f'{panel(**track)}</div>')


def render(browser: str, html: str, target: Path, width: int, height: int,
           scale: int = 2) -> bool:
    WORK.mkdir(parents=True, exist_ok=True)
    source = WORK / (target.stem + ".html")
    shot = WORK / target.name
    source.write_text(html, encoding="utf-8")
    shot.unlink(missing_ok=True)

    subprocess.run(
        [browser, "--headless", "--disable-gpu", "--no-sandbox", "--hide-scrollbars",
         f"--force-device-scale-factor={scale}",
         "--virtual-time-budget=4000",
         f"--screenshot={shot}", f"--window-size={width},{height}",
         f"file://{source}"],
        capture_output=True, timeout=180,
    )
    if not shot.exists():
        return False
    OUT.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(shot, target)
    return True


def main() -> int:
    browser = next((b for b in BROWSERS if shutil.which(b)), None)
    if browser is None:
        print("no Chromium-family browser found; tried: " + ", ".join(BROWSERS),
              file=sys.stderr)
        return 1
    if not MOCKUPS.exists():
        print(f"design mockups not found: {MOCKUPS}", file=sys.stderr)
        return 1

    jobs = [
        ("dark.png", document(dark_frame()), 1180, FRAME_HEIGHT + TITLE_BAR),
        ("now-playing-dark.png",
         document(f'<div style="display:flex">{rail_card(LATIN_TRACK)}'
                  f'{rail_card(ARABIC_TRACK)}</div>'), 808, 232),
    ]
    for name, html, width, height in jobs:
        target = OUT / name
        if render(browser, html, target, width, height):
            size = target.stat().st_size
            print(f"  {target.relative_to(ROOT)}  {width * 2}x{height * 2}  "
                  f"{size / 1024:.0f} KB")
        else:
            print(f"  {name}: render failed", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
