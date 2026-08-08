"""Tkinter desktop interface.

The visual language follows the "press sheet" mockups: a warm paper ground,
serif type, hairline rules instead of boxed cards, and the orbit visualiser
promoted to a full-bleed stage with the readouts floating over it.  All the
controls live in a fixed right-hand rail.

Tk has no gradients, alpha compositing or letter-spacing, so three things are
faked: the radial ground is a stack of concentric ovals, every translucent
colour is pre-mixed against the surface it sits on (:func:`_mix`), and tracked
small-caps are spaced out character by character (:func:`_track`).
"""

from __future__ import annotations

import math
import subprocess
import tkinter as tk
import tkinter.font as tkfont
from collections import deque
from pathlib import Path

from . import config, pipewire as pw
from .dsp import (CHARACTER_LABELS, CHARACTERS, MODE_LABELS, MODES, PRESETS,
                  Params)
from .engine import DEFAULT_LATENCY, LATENCY_PROFILES, AudioEngine, describe_error

ASSETS = Path(__file__).resolve().parent / "assets"

SERIF_STACK = ("Source Serif 4", "Source Serif Pro", "Noto Serif",
               "DejaVu Serif", "Liberation Serif", "Times")

RAIL_WIDTH = 404

#: Device pixels per design pixel.  Xft renders fonts at the desktop's DPI,
#: which on a HiDPI screen is a multiple of what Tk reports for geometry, so
#: the layout has to be scaled by the same factor to stay in proportion.
SCALE = 1.0
SCALE_STEPS = (1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 3.0)


def px(value: float) -> int:
    """A design pixel in device pixels."""
    return int(round(value * SCALE))


def _xft_dpi() -> float | None:
    """The desktop's font DPI, which Tk does not expose through its own API."""
    try:
        out = subprocess.run(["xrdb", "-query"], capture_output=True,
                             text=True, timeout=2.0).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    for line in out.splitlines():
        key, _, value = line.partition(":")
        if key.strip().lower() == "xft.dpi":
            try:
                return float(value.strip())
            except ValueError:
                return None
    return None


def detect_scale(root: tk.Misc, family: str) -> float:
    """How much larger Xft draws text than Tk's geometry units imply.

    Preferred source is the desktop's Xft.dpi against the DPI Tk derives from
    the screen size.  Without it, fall back to the line height of a probe font:
    linespace is about 1.36 em for a text face, which is close enough once the
    result is snapped to the usual desktop scaling steps.
    """
    tk_dpi = 72.0 * float(root.tk.call("tk", "scaling"))
    dpi = _xft_dpi()
    if dpi and tk_dpi > 0:
        ratio = dpi / tk_dpi
    else:
        probe = 200
        linespace = tkfont.Font(root=root, family=family,
                                size=-probe).metrics("linespace")
        ratio = (linespace / probe) / 1.36
    return min(SCALE_STEPS, key=lambda step: abs(step - ratio))


# --------------------------------------------------------------------------
# colour
# --------------------------------------------------------------------------


def _rgb(colour: str) -> tuple[int, int, int]:
    return tuple(int(colour[i:i + 2], 16) for i in (1, 3, 5))


def _hex(rgb) -> str:
    return "#%02x%02x%02x" % tuple(max(0, min(255, round(v))) for v in rgb)


def _mix(top: str, bottom: str, alpha: float) -> str:
    """Flatten `alpha` of `top` over `bottom` -- Tk widgets cannot blend."""
    t, b = _rgb(top), _rgb(bottom)
    return _hex(tuple(bb + (tt - bb) * alpha for tt, bb in zip(t, b)))


def _palette(*, ground, ground_hi, ground_lo, chrome, rail, field,
             ink, accent, accent_text, motion, motion_text, on_motion, amber):
    """Build a full token set from the handful of colours a theme really has.

    Every softened value in the mockups is an alpha of the ink colour; those
    are flattened here against whichever surface the element sits on.
    """
    def i(alpha, over=ground):
        return _mix(ink, over, alpha)

    return {
        "GROUND": ground, "GROUND_HI": ground_hi, "GROUND_LO": ground_lo,
        "CHROME": chrome, "RAIL": rail, "FIELD": field,
        "INK": ink,
        "INK_SOFT": i(0.60), "INK_FAINT": i(0.50), "INK_GHOST": i(0.42),
        "LINE": i(0.14), "LINE_SOFT": i(0.08),
        "CHROME_LINE": _mix(ink, chrome, 0.14),
        "RAIL_LINE": _mix(ink, rail, 0.12),
        "FIELD_LINE": _mix(ink, field, 0.22),
        "RAIL_INK": _mix(ink, rail, 1.0),
        "RAIL_SOFT": _mix(ink, rail, 0.60),
        "RAIL_FAINT": _mix(ink, rail, 0.50),
        "RAIL_GHOST": _mix(ink, rail, 0.40),
        "TRACK": _mix(ink, rail, 0.14),
        "TRACK_OFF": _mix(ink, rail, 0.30),
        "CHIP_LINE": i(0.20), "CHIP_INK": i(0.70),
        "ACCENT": accent, "ACCENT_TEXT": accent_text,
        "ACCENT_SOFT": _mix(accent, ground, 0.10),
        "ACCENT_RING": _mix(accent, ground, 0.45),
        "MOTION": motion, "MOTION_TEXT": motion_text, "ON_MOTION": on_motion,
        "MOTION_GLOW1": _mix(motion, ground, 0.16),
        "MOTION_GLOW2": _mix(motion, ground, 0.07),
        "AMBER": amber,
        "RING_OUT": i(0.10), "RING_MID": i(0.12), "RING_IN": i(0.16),
        "AXIS": i(0.07), "COMPASS": i(0.42),
        "HEAD_LINE": i(0.30), "HEAD_INNER": i(0.35), "NOSE": i(0.40),
        "METER_BED": i(0.10),
    }


PALETTES = {
    "light": _palette(
        ground="#f3f2f2", ground_hi="#fbfafa", ground_lo="#eceaea",
        chrome="#eae9e9", rail="#f0efef", field="#fbfafa",
        ink="#201e1d",
        accent="#0088b0", accent_text="#006786",
        motion="#d6006c", motion_text="#aa0b56", on_motion="#ffffff",
        amber="#edbb00",
    ),
    "dark": _palette(
        ground="#1a1918", ground_hi="#242220", ground_lo="#141312",
        chrome="#201e1d", rail="#1c1b1a", field="#252423",
        ink="#f3f2f2",
        accent="#62c5ee", accent_text="#8fd6f4",
        motion="#ff458e", motion_text="#ff90b1", on_motion="#201e1d",
        amber="#edbb00",
    ),
}
DEFAULT_THEME = "light"

#: The live palette.  Swapped in place so every widget can read `C[...]`.
C: dict[str, str] = dict(PALETTES[DEFAULT_THEME])

TONES = {"info": "INK_FAINT", "good": "ACCENT_TEXT",
         "bad": "MOTION_TEXT", "note": "ACCENT_TEXT"}

LABEL_TO_MODE = {MODE_LABELS[m]: m for m in MODES}
LABEL_TO_CHARACTER = {CHARACTER_LABELS[c]: c for c in CHARACTERS}

CHARACTER_HINTS = {
    "clean": "The source passes through untouched",
    "slowed": "Drops the pitch about three semitones — a live stream's "
              "tempo cannot be stretched",
    "radio": "Mono, band-limited to 450 Hz – 3 kHz, valve drive and tape wow",
}


def _track(text: str) -> str:
    """Fake CSS letter-spacing by spacing the characters out."""
    return " ".join(text)


# --------------------------------------------------------------------------
# small widgets
# --------------------------------------------------------------------------


class PressButton(tk.Label):
    """A flat label that behaves like a button, with full colour control."""

    def __init__(self, master, text, command, *, bg, fg, hover,
                 font, padx=34, pady=11, cursor="hand2"):
        super().__init__(master, text=text, font=font, bd=0, cursor=cursor,
                         padx=px(padx), pady=px(pady), highlightthickness=0)
        self.command = command
        self._tokens = (bg, fg, hover)
        self._enabled = True
        self.retheme()
        self.bind("<Enter>", lambda _: self._hover(True))
        self.bind("<Leave>", lambda _: self._hover(False))
        self.bind("<Button-1>", self._click)

    def set_tokens(self, bg, fg, hover):
        self._tokens = (bg, fg, hover)
        self.retheme()

    def retheme(self):
        bg, fg, _ = self._tokens
        self.configure(bg=C[bg],
                       fg=C[fg] if self._enabled else C["INK_GHOST"])

    def set_enabled(self, enabled: bool):
        self._enabled = enabled
        self.configure(cursor="hand2" if enabled else "")
        self.retheme()

    def _hover(self, on):
        if not self._enabled:
            return
        bg, _, hover = self._tokens
        self.configure(bg=C[hover if on else bg])

    def _click(self, _event):
        if self._enabled:
            self.command()


class Segmented(tk.Canvas):
    """The Light / Dark switch in the title strip."""

    PAD_X, HEIGHT = 11, 23

    def __init__(self, master, options, value, on_change, font):
        self.options = options
        self.value = value
        self.on_change = on_change
        self.font = font
        metrics = tkfont.Font(font=font)
        self._widths = [metrics.measure(_track(o)) + px(self.PAD_X) * 2
                        for o in options]
        self._height = px(self.HEIGHT)
        super().__init__(master, width=sum(self._widths) + 1, height=self._height,
                         highlightthickness=0, bd=0, cursor="hand2")
        self.bind("<Button-1>", self._click)
        self.retheme()

    def retheme(self):
        self.configure(bg=C["CHROME"])
        self.delete("all")
        x = 0
        for option, width in zip(self.options, self._widths):
            active = option == self.value
            self.create_rectangle(
                x, 0, x + width, self._height - 1,
                fill=C["INK"] if active else C["CHROME"],
                outline=C["CHROME_LINE"], width=px(1),
            )
            self.create_text(
                x + width / 2, self._height / 2, text=_track(option),
                fill=C["GROUND"] if active else C["INK_FAINT"], font=self.font,
            )
            x += width

    def set(self, value):
        self.value = value
        self.retheme()

    def _click(self, event):
        x = 0
        for option, width in zip(self.options, self._widths):
            if x <= event.x < x + width:
                if option != self.value:
                    self.on_change(option)
                return
            x += width


class CheckBox(tk.Frame):
    """A 13px hollow square plus a label, per the mockup."""

    BOX = 13

    def __init__(self, master, text, command, font):
        super().__init__(master, bd=0, highlightthickness=0)
        self.command = command
        self.value = False
        self._box = px(self.BOX)
        self.box = tk.Canvas(self, width=self._box + 1, height=self._box + 1,
                             highlightthickness=0, bd=0, cursor="hand2")
        self.box.pack(side="left")
        self.label = tk.Label(self, text=text, font=font, bd=0,
                              highlightthickness=0, padx=px(7), cursor="hand2")
        self.label.pack(side="left")
        for widget in (self, self.box, self.label):
            widget.bind("<Button-1>", lambda _: self.toggle())
        self.retheme()

    def retheme(self):
        self.configure(bg=C["RAIL"])
        self.label.configure(bg=C["RAIL"], fg=C["RAIL_SOFT"])
        self.box.configure(bg=C["RAIL"])
        self._draw()

    def _draw(self):
        b = self._box
        self.box.delete("all")
        self.box.create_rectangle(0, 0, b, b, outline=C["FIELD_LINE"], width=px(1),
                                  fill=C["ACCENT"] if self.value else C["RAIL"])
        if self.value:
            self.box.create_line(b * 0.23, b * 0.54, b * 0.46, b * 0.77,
                                 b * 0.77, b * 0.23,
                                 fill=C["GROUND"], width=px(2))

    def get(self) -> bool:
        return self.value

    def set(self, value: bool):
        self.value = bool(value)
        self._draw()

    def toggle(self):
        self.set(not self.value)
        self.command()


class LogoMark(tk.Canvas):
    """The 8D Music mark: level bars over a sweeping arc.

    This is the three-bar cut the identity specifies for small sizes, drawn
    live so it can follow the theme.  Geometry is the logo's 120x104 viewBox.
    """

    VIEW_W, VIEW_H = 120.0, 104.0
    BARS = ((26, 18, 36), (52, 18, 54), (78, 18, 28))   # x, width, height
    BASELINE = 66.0
    SWEEP = ((12, 78), (60, 110), (108, 78))
    SWEEP_W = 14.0

    def __init__(self, master, height=21, surface="CHROME"):
        self.surface = surface
        self._scale = px(height) / self.VIEW_H
        super().__init__(master, width=int(self.VIEW_W * self._scale) + 1,
                         height=px(height) + 1, highlightthickness=0, bd=0)
        self.retheme()

    def retheme(self):
        self.configure(bg=C[self.surface])
        self.delete("all")
        s = self._scale

        for x, w, h in self.BARS:
            cx = (x + w / 2.0) * s
            cap = w / 2.0 * s
            self.create_line(cx, (self.BASELINE - h) * s + cap,
                             cx, self.BASELINE * s - cap,
                             width=w * s, capstyle="round", fill=C["INK"])

        (x0, y0), (x1, y1), (x2, y2) = self.SWEEP
        points = []
        for i in range(25):
            t = i / 24.0
            u = 1.0 - t
            points += [(u * u * x0 + 2 * u * t * x1 + t * t * x2) * s,
                       (u * u * y0 + 2 * u * t * y1 + t * t * y2) * s]
        self.create_line(*points, width=self.SWEEP_W * s, fill=C["ACCENT"],
                         capstyle="round", joinstyle="round")


class ScrollHint(tk.Canvas):
    """A hairline thumb on the rail's edge, drawn only when it can scroll."""

    WIDTH = 3

    def __init__(self, master):
        super().__init__(master, width=px(self.WIDTH) + px(4),
                         highlightthickness=0, bd=0)
        self._span = (0.0, 1.0)

    def retheme(self):
        self.configure(bg=C["RAIL"])
        self.update_span(*self._span)

    def update_span(self, first, last):
        self._span = (float(first), float(last))
        self.delete("all")
        first, last = self._span
        if last - first >= 0.999:
            return
        h = max(self.winfo_height(), 1)
        w = px(self.WIDTH)
        self.create_rectangle(0, first * h, w, last * h,
                              fill=C["TRACK_OFF"], outline="")


class RailSlider(tk.Frame):
    """Label + value readout above a 3px track with a round handle.

    `compact` is the two-column grid variant from the mockup: smaller type and
    no handle, and the fill greys out while the parameter sits at its minimum.
    """

    TRACK_H = 15

    def __init__(self, master, text, lo, hi, value, on_change, *, fonts,
                 fmt=None, hint="", compact=False):
        super().__init__(master, bd=0, highlightthickness=0)
        self._fonts = fonts
        self.lo, self.hi = float(lo), float(hi)
        self.value = float(value)
        self.fmt = fmt or (lambda v: f"{v:.2f}")
        self.on_change = on_change
        self.compact = compact
        self.enabled = True

        head = tk.Frame(self, bd=0, highlightthickness=0)
        head.pack(fill="x")
        self.name = tk.Label(head, text=text, bd=0, highlightthickness=0, anchor="w")
        self.name.pack(side="left")
        self.readout = tk.Label(head, text=self.fmt(self.value), bd=0,
                                highlightthickness=0, anchor="e")
        self.readout.pack(side="right")

        self._track_h = px(self.TRACK_H)
        self.track = tk.Canvas(self, height=self._track_h, highlightthickness=0,
                               bd=0, cursor="hand2")
        self.track.pack(fill="x", pady=(px(5), 0))
        self.track.bind("<Button-1>", self._drag)
        self.track.bind("<B1-Motion>", self._drag)
        self.track.bind("<Configure>", lambda _: self._draw())

        self.hint = None
        if hint:
            self.hint = tk.Label(self, text=hint, bd=0, highlightthickness=0,
                                 anchor="w", justify="left", wraplength=px(330))
            self.hint.pack(fill="x", pady=(px(5), 0))

        self.retheme()

    # -- painting ---------------------------------------------------------

    def retheme(self, fonts=None):
        if fonts is not None:
            self._fonts = fonts
        f = self._fonts
        self.configure(bg=C["RAIL"])
        for widget in self.winfo_children():
            if isinstance(widget, tk.Frame):
                widget.configure(bg=C["RAIL"])
        self.name.configure(bg=C["RAIL"], font=f["label"],
                            fg=C["RAIL_INK"] if self.enabled else C["RAIL_GHOST"])
        self.readout.configure(bg=C["RAIL"], font=f["value"], fg=self._value_ink())
        self.track.configure(bg=C["RAIL"])
        if self.hint is not None:
            self.hint.configure(bg=C["RAIL"], fg=C["RAIL_FAINT"], font=f["hint"])
        self._draw()

    def _at_floor(self) -> bool:
        return self.value <= self.lo + (self.hi - self.lo) * 0.005

    def _value_ink(self) -> str:
        if not self.enabled:
            return C["RAIL_GHOST"]
        if self.compact and self._at_floor():
            return C["RAIL_FAINT"]
        return C["ACCENT_TEXT"]

    def _draw(self):
        self.track.delete("all")
        width = max(self.track.winfo_width(), 1)
        y = self._track_h / 2
        half = px(1.5)
        frac = (self.value - self.lo) / (self.hi - self.lo or 1.0)
        frac = min(max(frac, 0.0), 1.0)

        self.track.create_rectangle(0, y - half, width, y + half,
                                    fill=C["TRACK"], outline="")
        if self.enabled:
            fill = C["TRACK_OFF"] if (self.compact and self._at_floor()) else C["ACCENT"]
        else:
            fill = C["TRACK_OFF"]
        if frac > 0:
            self.track.create_rectangle(0, y - half, width * frac, y + half,
                                        fill=fill, outline="")
        if not self.compact:
            x = width * frac
            r = px(6.5)
            self.track.create_oval(x - r, y - r, x + r, y + r, fill=fill, outline="")

    # -- interaction -------------------------------------------------------

    def _drag(self, event):
        if not self.enabled:
            return
        width = max(self.track.winfo_width(), 1)
        frac = min(max(event.x / width, 0.0), 1.0)
        self.value = self.lo + frac * (self.hi - self.lo)
        self.readout.configure(text=self.fmt(self.value), fg=self._value_ink())
        self._draw()
        self.on_change(self.value)

    def set(self, value):
        self.value = min(max(float(value), self.lo), self.hi)
        self.readout.configure(text=self.fmt(self.value), fg=self._value_ink())
        self._draw()

    def set_hint(self, text: str):
        if self.hint is not None:
            self.hint.configure(text=text)

    def set_enabled(self, enabled: bool):
        self.enabled = bool(enabled)
        self.track.configure(cursor="hand2" if enabled else "")
        self.retheme()


class Dropdown(tk.Frame):
    """A bordered field with a caret that posts a themed menu.

    ttk's combobox cannot be pushed all the way to the mockup's flat 1px field,
    so this draws the closed state itself and uses a plain tk menu for the list.
    """

    def __init__(self, master, values, value, on_change, font):
        super().__init__(master, bd=1, highlightthickness=0)
        self.values = list(values)
        self.value = value
        self.on_change = on_change
        self.font = font

        self.text = tk.Label(self, text=value, font=font, bd=0, anchor="w",
                             padx=px(11), pady=px(8), highlightthickness=0,
                             cursor="hand2")
        self.text.pack(side="left", fill="x", expand=True)
        self.caret = tk.Label(self, text="▾", font=font, bd=0, padx=px(9),
                              pady=px(8), highlightthickness=0, cursor="hand2")
        self.caret.pack(side="right")

        self.menu = tk.Menu(self, tearoff=0)
        for widget in (self, self.text, self.caret):
            widget.bind("<Button-1>", self._post)
        self.retheme()

    def retheme(self):
        self.configure(bg=C["FIELD"], highlightbackground=C["FIELD_LINE"],
                       highlightcolor=C["FIELD_LINE"], highlightthickness=px(1), bd=0)
        self.text.configure(bg=C["FIELD"], fg=C["RAIL_INK"])
        self.caret.configure(bg=C["FIELD"], fg=C["RAIL_GHOST"])
        self.menu.configure(
            bg=C["FIELD"], fg=C["RAIL_INK"], font=self.font,
            activebackground=C["ACCENT"], activeforeground=C["GROUND"],
            bd=0, relief="flat", activeborderwidth=0,
        )
        self._rebuild_menu()

    def _rebuild_menu(self):
        self.menu.delete(0, "end")
        for option in self.values:
            self.menu.add_command(label=option,
                                  command=lambda o=option: self._pick(o))

    def configure_values(self, values):
        self.values = list(values)
        self._rebuild_menu()

    def set(self, value):
        self.value = value
        self.text.configure(text=value)

    def get(self) -> str:
        return self.value

    def _pick(self, option):
        # Tk's own click binding unposts the menu, but the command can be
        # reached other ways and a leftover global grab would freeze the
        # window, so tear it down here too.  Both calls are idempotent.
        self.menu.unpost()
        self.menu.grab_release()
        if option == self.value:
            return
        self.set(option)
        self.on_change(option)

    def _post(self, _event):
        if not self.values:
            return
        # tk_popup takes a pointer grab, which is what lets a click anywhere
        # else dismiss the list; post() does not, so the menu used to stay open
        # until an item was chosen.  Do NOT release the grab on the way out --
        # the menu owns it until it unposts itself.
        try:
            self.menu.tk_popup(self.winfo_rootx(),
                               self.winfo_rooty() + self.winfo_height())
        except tk.TclError:
            self.menu.grab_release()


# --------------------------------------------------------------------------
# the stage
# --------------------------------------------------------------------------


class OrbitStage(tk.Canvas):
    """Full-bleed visualiser with the readouts floating over it.

    Three layers: `bed` (radial ground, rings, listener, headings) is rebuilt
    only on resize or a theme change, `chips` whenever the preset selection
    moves, and `live` on every animation frame.
    """

    MARGIN_X, TOP, BOTTOM = 34, 30, 28
    OVERLAY_W = 340

    def __init__(self, master, fonts, on_preset):
        super().__init__(master, highlightthickness=0, bd=0)
        self.fonts = fonts
        self.on_preset = on_preset
        self.trail: deque[tuple[float, float]] = deque(maxlen=48)
        self.peak_l = self.peak_r = 0.0
        self.active_preset: str | None = None
        self.state_label = "Ready"
        self.status_text = ""
        self.status_tone = "info"
        self._pulse = 0.0
        self._geometry = (0, 0)
        self._resize_job = None
        self.bind("<Configure>", self._on_configure)

    # -- geometry ----------------------------------------------------------

    def _orbit_box(self):
        w, h = max(self.winfo_width(), 1), max(self.winfo_height(), 1)
        span = max(px(200), min(px(560), min(w * 0.68, h * 0.80)))
        return w * 0.46, h * 0.5, span / 2.0

    def _on_configure(self, _event):
        if self._resize_job is not None:
            self.after_cancel(self._resize_job)
        self._resize_job = self.after(60, self._rebuild)

    def _rebuild(self):
        self._resize_job = None
        self._geometry = (self.winfo_width(), self.winfo_height())
        self.delete("bed")
        self._draw_bed()
        self.tag_lower("bed")
        self.render_chips()

    def retheme(self, fonts=None):
        if fonts is not None:
            self.fonts = fonts
        self.configure(bg=C["GROUND"])
        self._rebuild()

    # -- the bed -----------------------------------------------------------

    def _ground_at(self, t: float) -> str:
        """Sample the radial gradient: hi at the centre, lo at the corners."""
        knee = 0.61
        if t <= knee:
            return _mix(C["GROUND"], C["GROUND_HI"], t / knee)
        return _mix(C["GROUND_LO"], C["GROUND"], (t - knee) / (1.0 - knee))

    def _draw_bed(self):
        w, h = max(self.winfo_width(), 1), max(self.winfo_height(), 1)
        cx, cy, r = self._orbit_box()

        # radial ground, painted as concentric bands from the outside in
        reach = max(math.hypot(cx - x, cy - y)
                    for x in (0, w) for y in (0, h))
        steps = 44
        self.create_rectangle(0, 0, w, h, fill=self._ground_at(1.0),
                              outline="", tags="bed")
        for i in range(steps, 0, -1):
            t = i / steps
            rad = reach * t
            self.create_oval(cx - rad, cy - rad, cx + rad, cy + rad,
                             fill=self._ground_at(t), outline="", tags="bed")

        # rings and axes
        hair = px(1)
        for factor, colour, dash in ((1.00, C["RING_OUT"], None),
                                     (0.75, C["RING_MID"], None),
                                     (0.50, C["ACCENT_RING"], (px(4), px(5))),
                                     (0.157, C["RING_IN"], None)):
            rr = r * factor
            self.create_oval(cx - rr, cy - rr, cx + rr, cy + rr, outline=colour,
                             width=hair, dash=dash, tags="bed")
        self.create_line(cx, cy - r, cx, cy + r, fill=C["AXIS"],
                         width=hair, tags="bed")
        self.create_line(cx - r, cy, cx + r, cy, fill=C["AXIS"],
                         width=hair, tags="bed")

        # the listener
        head = r * 0.186
        self.create_oval(cx - head, cy - head, cx + head, cy + head,
                         outline=C["HEAD_LINE"], width=hair, tags="bed")
        inner = head * 0.5
        self.create_oval(cx - inner, cy - inner, cx + inner, cy + inner,
                         outline=C["HEAD_INNER"], width=hair, tags="bed")
        nose, wing = inner + px(9), px(6)
        self.create_polygon(cx, cy - nose, cx - wing, cy - nose + px(9),
                            cx + wing, cy - nose + px(9),
                            fill=C["NOSE"], outline="", tags="bed")

        # headings
        small = self.fonts["compass"]
        gap = px(26)
        for text, x, y in (("FRONT", cx, cy - r - gap), ("BACK", cx, cy + r + gap)):
            self.create_text(x, y, text=_track(text), fill=C["COMPASS"],
                             font=small, tags="bed")
        for text, x in (("L", cx - r - px(24)), ("R", cx + r + px(24))):
            self.create_text(x, cy, text=text, fill=C["COMPASS"],
                             font=small, tags="bed")

        # masthead -- each line is placed off the measured box of the one above
        x = px(self.MARGIN_X)
        hero = self.create_text(x, px(self.TOP), text="8D Music", anchor="nw",
                                fill=C["INK"], font=self.fonts["hero"], tags="bed")
        tagline = self.create_text(
            x, self.bbox(hero)[3] + px(4),
            text="Real-time spatial audio for everything your system plays",
            anchor="nw", fill=C["INK_SOFT"], font=self.fonts["tagline"],
            width=px(self.OVERLAY_W), tags="bed")
        self._state_y = self.bbox(tagline)[3] + px(20)

    # -- preset chips ------------------------------------------------------

    def render_chips(self):
        self.delete("chip")
        h = max(self.winfo_height(), 1)
        font = self.fonts["chip"]
        font_on = self.fonts["chip_on"]
        metrics = tkfont.Font(font=font)
        pad_x, pad_y, gap = px(11), px(5), px(6)
        line_h = metrics.metrics("linespace") + pad_y * 2

        rows, row, width = [], [], 0.0
        for name in PRESETS:
            chip_w = metrics.measure(name) + pad_x * 2
            if row and width + gap + chip_w > px(self.OVERLAY_W):
                rows.append(row)
                row, width = [], 0.0
            row.append((name, chip_w))
            width += (gap if width else 0) + chip_w
        if row:
            rows.append(row)

        bottom = h - px(self.BOTTOM)
        y = bottom - len(rows) * line_h - (len(rows) - 1) * gap
        self._chip_top = y
        for line in rows:
            x = px(self.MARGIN_X)
            for name, chip_w in line:
                on = name == self.active_preset
                box = self.create_rectangle(
                    x, y, x + chip_w, y + line_h,
                    fill=C["ACCENT_SOFT"] if on else C["GROUND"],
                    outline=C["ACCENT"] if on else C["CHIP_LINE"],
                    width=px(1), tags=("chip", f"chip:{name}"),
                )
                label = self.create_text(
                    x + chip_w / 2, y + line_h / 2, text=name,
                    fill=C["ACCENT_TEXT"] if on else C["CHIP_INK"],
                    font=font_on if on else font, tags=("chip", f"chip:{name}"),
                )
                for item in (box, label):
                    self.tag_bind(item, "<Button-1>",
                                  lambda _e, n=name: self.on_preset(n))
                    self.tag_bind(item, "<Enter>",
                                  lambda _e: self.configure(cursor="hand2"))
                    self.tag_bind(item, "<Leave>",
                                  lambda _e: self.configure(cursor=""))
                x += chip_w + gap
            y += line_h + gap

    def set_active_preset(self, name: str | None):
        if name != self.active_preset:
            self.active_preset = name
            self.render_chips()

    # -- the live layer ----------------------------------------------------

    def refresh(self, angle, radius, running, levels, cpu):
        if self._geometry != (self.winfo_width(), self.winfo_height()):
            self._rebuild()
        self.delete("live")
        cx, cy, r = self._orbit_box()

        self.trail.append((angle, radius))
        self._pulse = (self._pulse + 0.055) % 1.0

        # comet tail, fading back into the ground
        n = len(self.trail)
        tail_ink = C["MOTION"] if running else C["INK_GHOST"]
        for i, (a, rad) in enumerate(self.trail):
            if i == n - 1:
                continue
            frac = i / max(n - 1, 1)
            x, y = self._place(cx, cy, r, a, rad)
            size = px(1.0 + frac * 3.2)
            shade = _mix(tail_ink, C["GROUND"], frac * 0.8)
            self.create_oval(x - size, y - size, x + size, y + size,
                             fill=shade, outline="", tags="live")

        x, y = self._place(cx, cy, r, angle, radius)
        self.create_line(cx, cy, x, y, fill=C["ACCENT_RING"] if running else C["AXIS"],
                         dash=(px(2), px(4)), width=px(1), tags="live")

        dot = C["MOTION"] if running else C["INK_GHOST"]
        core = max(px(6), r * 0.036)
        for grow, shade in ((core + px(18), C["MOTION_GLOW2"]),
                            (core + px(8), C["MOTION_GLOW1"])):
            if running:
                self.create_oval(x - grow, y - grow, x + grow, y + grow,
                                 fill=shade, outline="", tags="live")
        self.create_oval(x - core, y - core, x + core, y + core,
                         fill=dot, outline="", tags="live")

        self._draw_state(running)
        self._draw_readout(angle, radius, running, levels, cpu)

    @staticmethod
    def _place(cx, cy, r, angle, radius):
        rr = r * 0.5 * math.sqrt(min(max(radius, 0.05), 4.0))
        return cx + math.sin(angle) * rr, cy - math.cos(angle) * rr

    def _draw_state(self, running):
        x = px(self.MARGIN_X)
        y = getattr(self, "_state_y", px(self.TOP + 110))
        colour = C["MOTION"] if running else C["INK_GHOST"]
        if running:
            alpha = 0.5 + 0.5 * abs(math.sin(self._pulse * math.pi))
            colour = _mix(C["MOTION"], C["GROUND"], alpha)
        dot = px(4)
        self.create_oval(x, y - dot, x + dot * 2, y + dot,
                         fill=colour, outline="", tags="live")
        self.create_text(x + px(17), y, text=_track(self.state_label.upper()),
                         anchor="w",
                         fill=C["MOTION_TEXT"] if running else C["INK_GHOST"],
                         font=self.fonts["state"], tags="live")
        if self.status_text:
            self.create_text(x, y + px(18), text=self.status_text, anchor="nw",
                             fill=C[TONES[self.status_tone]], width=px(self.OVERLAY_W),
                             font=self.fonts["hint"], tags="live")

    def _draw_readout(self, angle, radius, running, levels, cpu):
        h = max(self.winfo_height(), 1)
        x = px(self.MARGIN_X)
        meter_bottom = getattr(self, "_chip_top", h - px(self.BOTTOM)) - px(12)
        degrees = (math.degrees(angle) + 180) % 360 - 180
        side = "centre" if abs(degrees) <= 6 else ("right" if degrees > 6 else "left")

        left, right = levels
        self.peak_l = max(left, self.peak_l * 0.82)
        self.peak_r = max(right, self.peak_r * 0.82)

        inset, half, step = px(17), px(2.5), px(13)
        bar_w = px(self.OVERLAY_W) - inset
        y = meter_bottom - step
        for label, peak in (("R", self.peak_r), ("L", self.peak_l)):
            self.create_text(x, y, text=label, anchor="w", fill=C["INK_FAINT"],
                             font=self.fonts["meter"], tags="live")
            self.create_rectangle(x + inset, y - half, x + inset + bar_w, y + half,
                                  fill=C["METER_BED"], outline="", tags="live")
            filled = bar_w * min(peak, 1.0)
            if filled > 0:
                self.create_rectangle(x + inset, y - half, x + inset + filled,
                                      y + half, fill=C["ACCENT"], outline="",
                                      tags="live")
            y -= step

        y -= px(9)
        bearing = f"{degrees:+.0f}°".replace("-", "−")   # a real minus sign
        parts = [(bearing, C["ACCENT_TEXT"], self.fonts["read_on"]),
                 (f"{radius:.2f} m", C["INK_SOFT"], self.fonts["read"]),
                 (side, C["INK_SOFT"], self.fonts["read"])]
        if running:
            parts.append((f"cpu {cpu * 100:.0f}%", C["INK_SOFT"], self.fonts["read"]))
        for text, colour, font in parts:
            item = self.create_text(x, y, text=text, anchor="w", fill=colour,
                                    font=font, tags="live")
            x = self.bbox(item)[2] + px(22)


# --------------------------------------------------------------------------
# the window
# --------------------------------------------------------------------------


class App(tk.Tk):
    def __init__(self):
        global SCALE
        super().__init__()
        self.title("8D Music — Real-Time Spatial Audio")

        self.engine = AudioEngine()
        params, self.prefs = config.load()
        self.engine.set_params(params)

        self.theme = self._use_theme(self.prefs.get("theme", DEFAULT_THEME))
        family = self._serif_family()
        SCALE = detect_scale(self, family)
        self._fonts = self._build_fonts(family)

        # Fit the design to the screen once it has been scaled up.  The rail is
        # taller than the mockup's -- it carries every parameter, not the subset
        # the mockup drew -- so open a little taller and let the rest scroll.
        width = min(px(1180), int(self.winfo_screenwidth() * 0.94))
        height = min(px(860), int(self.winfo_screenheight() * 0.88))
        self.geometry(f"{width}x{height}")
        self.minsize(min(px(880), width), min(px(560), height))
        self._set_window_icon()
        self._painted: list[tuple[tk.Misc, dict[str, str]]] = []
        self._themed: list = []
        self.sinks: list[pw.Sink] = []
        self._output_moves = 0

        self._build_layout()
        self._sync_widgets(params)
        self.refresh_devices()
        self.retheme()

        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.bind("<space>", lambda _: self.toggle_bypass())
        self.bind("<Control-r>", lambda _: self.toggle_engine())
        self.bind("<Control-t>", lambda _: self.toggle_theme())

        missing = pw.missing_tools()
        if missing:
            self.set_status(
                f"Missing PipeWire tools: {', '.join(missing)}. "
                "Install with: sudo apt install pipewire-bin", "bad")
            self.start_button.set_enabled(False)

        self.after(33, self._tick)

    # -- theme -------------------------------------------------------------

    @staticmethod
    def _use_theme(name: str) -> str:
        name = name if name in PALETTES else DEFAULT_THEME
        C.clear()
        C.update(PALETTES[name])
        return name

    def _set_window_icon(self):
        """Hand the window manager the app icon at every size it might want."""
        images = []
        for size in (16, 24, 32, 48, 64, 128, 256):
            path = ASSETS / f"icon-{size}.png"
            if not path.exists():
                continue
            try:
                images.append(tk.PhotoImage(master=self, file=str(path)))
            except tk.TclError:
                pass  # a Tk without PNG support -- the icon is cosmetic
        if images:
            self._icons = images          # Tk keeps only a weak reference
            self.iconphoto(True, *images)

    def _serif_family(self) -> str:
        families = set(tkfont.families())
        return next((f for f in SERIF_STACK if f in families),
                    tkfont.nametofont("TkDefaultFont").cget("family"))

    def _build_fonts(self, family):
        # Negative sizes are pixels in Tk, and Xft then enlarges them by the
        # same factor `px()` applies to the layout, so both stay in step.
        return {
            "hero": (family, -44, "bold"),
            "tagline": (family, -15),
            "state": (family, -11, "bold"),
            "hint": (family, -12),
            "read": (family, -13),
            "read_on": (family, -13, "bold"),
            "meter": (family, -11),
            "compass": (family, -11, "bold"),
            "chip": (family, -12),
            "chip_on": (family, -12, "bold"),
            "strip": (family, -12, "bold"),
            "switch": (family, -11),
            "section": (family, -11, "bold"),
            "field": (family, -14),
            "label": (family, -15),
            "value": (family, -14, "bold"),
            "label_sm": (family, -14),
            "value_sm": (family, -13, "bold"),
            "button": (family, -15, "bold"),
            "ghost": (family, -13),
        }

    def _paint(self, widget, **tokens):
        """Register `widget` so a theme switch can re-apply these options."""
        self._painted.append((widget, tokens))
        widget.configure(**{opt: C[key] for opt, key in tokens.items()})

    def toggle_theme(self):
        self.theme = self._use_theme("dark" if self.theme == "light" else "light")
        self.retheme()

    def retheme(self):
        for widget, tokens in self._painted:
            widget.configure(**{opt: C[key] for opt, key in tokens.items()})
        for widget in self._themed:
            widget.retheme()
        self.switch.set("Light" if self.theme == "light" else "Dark")
        self.stage.retheme()

    # -- chrome ------------------------------------------------------------

    def _section(self, parent, title):
        label = tk.Label(parent, text=_track(title.upper()), anchor="w", bd=0,
                         highlightthickness=0, font=self._fonts["section"])
        self._paint(label, bg="RAIL", fg="ACCENT_TEXT")
        return label

    def _slider(self, parent, *args, compact=False, **kw):
        f = self._fonts
        slider = RailSlider(parent, *args, compact=compact, **kw, fonts={
            "label": f["label_sm" if compact else "label"],
            "value": f["value_sm" if compact else "value"],
            "hint": f["hint"],
        })
        self._themed.append(slider)
        return slider

    def _dropdown(self, parent, values, value, on_change):
        box = Dropdown(parent, values, value, on_change, self._fonts["field"])
        self._themed.append(box)
        return box

    def _build_layout(self):
        self._paint(self, bg="GROUND")

        # ---- title strip ---------------------------------------------------
        strip = tk.Frame(self, bd=0, highlightthickness=0)
        strip.pack(fill="x")
        self._paint(strip, bg="CHROME")
        inner = tk.Frame(strip, bd=0, highlightthickness=0)
        inner.pack(fill="x", padx=px(14), pady=px(10))
        self._paint(inner, bg="CHROME")

        self.mark = LogoMark(inner)
        self.mark.pack(side="left")
        self._themed.append(self.mark)

        self.switch = Segmented(inner, ("Light", "Dark"),
                                "Light" if self.theme == "light" else "Dark",
                                self._pick_theme, self._fonts["switch"])
        self.switch.pack(side="right")
        self._themed.append(self.switch)

        caption = tk.Label(inner, text=_track("8D MUSIC — REAL-TIME SPATIAL AUDIO"),
                           bd=0, highlightthickness=0, font=self._fonts["strip"])
        caption.pack(side="left", expand=True)
        self._paint(caption, bg="CHROME", fg="INK_SOFT")

        rule = tk.Frame(self, height=px(1), bd=0, highlightthickness=0)
        rule.pack(fill="x")
        self._paint(rule, bg="CHROME_LINE")

        # ---- body ------------------------------------------------------------
        body = tk.Frame(self, bd=0, highlightthickness=0)
        body.pack(fill="both", expand=True)
        self._paint(body, bg="GROUND")

        rail_rule = tk.Frame(body, width=px(1), bd=0, highlightthickness=0)
        rail_rule.pack(side="right", fill="y")
        self._paint(rail_rule, bg="RAIL_LINE")

        rail = self._build_rail(body)
        rail.pack(side="right", fill="y")

        self.stage = OrbitStage(body, self._fonts, self.apply_preset)
        self.stage.pack(side="left", fill="both", expand=True)
        self._paint(self.stage, bg="GROUND")

    def _pick_theme(self, option):
        if (option == "Light") != (self.theme == "light"):
            self.toggle_theme()

    # -- the rail ----------------------------------------------------------

    def _build_rail(self, parent):
        """A scrolling column, so a short window clips nothing."""
        holder = tk.Frame(parent, width=px(RAIL_WIDTH), bd=0, highlightthickness=0)
        holder.pack_propagate(False)
        self._paint(holder, bg="RAIL")

        hint = ScrollHint(holder)
        hint.pack(side="right", fill="y")
        self._themed.append(hint)

        canvas = tk.Canvas(holder, highlightthickness=0, bd=0, width=px(RAIL_WIDTH),
                           yscrollcommand=hint.update_span)
        canvas.pack(side="left", fill="both", expand=True)
        self._paint(canvas, bg="RAIL")

        rail = tk.Frame(canvas, bd=0, highlightthickness=0)
        self._paint(rail, bg="RAIL")
        window = canvas.create_window((0, 0), window=rail, anchor="nw",
                                      width=px(RAIL_WIDTH))

        def on_content(_event=None):
            canvas.configure(scrollregion=canvas.bbox("all"))

        def on_canvas(event):
            canvas.itemconfigure(window, width=event.width)

        def on_wheel(event):
            # bind_all is the only way to catch the wheel over every descendant,
            # so ignore anything that did not happen inside the rail.
            widget = event.widget
            while widget is not None and widget is not canvas:
                widget = getattr(widget, "master", None)
            if widget is None:
                return
            span = canvas.bbox("all")
            if not span or span[3] - span[1] <= canvas.winfo_height():
                return
            canvas.yview_scroll(-1 if event.num == 4 else 1, "units")

        rail.bind("<Configure>", on_content)
        canvas.bind("<Configure>", on_canvas)
        hint.bind("<Configure>", lambda _: hint.update_span(*canvas.yview()))
        canvas.bind_all("<Button-4>", on_wheel, add="+")
        canvas.bind_all("<Button-5>", on_wheel, add="+")

        pad = tk.Frame(rail, bd=0, highlightthickness=0)
        pad.pack(fill="both", expand=True, padx=px(30), pady=(px(26), px(24)))
        self._paint(pad, bg="RAIL")
        self._fill_rail(pad)
        return holder

    def _fill_rail(self, rail):
        fonts = self._fonts
        pct = lambda v: f"{v * 100:.0f}%"

        head = tk.Frame(rail, bd=0, highlightthickness=0)
        head.pack(fill="x")
        self._paint(head, bg="RAIL")
        self.start_button = PressButton(
            head, "Start", self.toggle_engine, bg="ACCENT", fg="ON_MOTION",
            hover="ACCENT_TEXT", font=fonts["button"])
        self.start_button.pack(side="right")
        self._themed.append(self.start_button)

        # ---- movement ------------------------------------------------------
        move = self._block(rail, "Movement", (22, 0))

        row = tk.Frame(move, bd=0, highlightthickness=0)
        row.pack(fill="x", pady=(0, px(13)))
        self._paint(row, bg="RAIL")
        self.mode_box = self._dropdown(
            row, [MODE_LABELS[m] for m in MODES], MODE_LABELS["circular"], self.on_mode)
        self.mode_box.pack(side="left", fill="x", expand=True)
        self.dir_box = self._dropdown(
            row, ["Clockwise", "Counter-clockwise"], "Clockwise", self.on_direction)
        self.dir_box.pack(side="left", padx=(px(8), 0))

        self.s_speed = self._slider(
            move, "Movement speed", 0.01, 1.2, 0.12, self.on_speed,
            fmt=lambda v: f"{v:.2f} rot/s · {1 / max(v, 0.01):.1f} s",
            hint="How fast the source travels around you")
        self.s_speed.pack(fill="x", pady=(0, px(13)))

        self.s_radius = self._slider(
            move, "Orbit radius", 0.25, 3.0, 1.0, self.on_radius,
            fmt=lambda v: f"{v:.2f} m",
            hint="Virtual distance to the source — affects level, tone and room")
        self.s_radius.pack(fill="x", pady=(0, px(13)))

        self.s_depth = self._slider(
            move, "Effect depth", 0.0, 1.0, 0.85, self.on_depth,
            fmt=lambda v: f"{v * 100:.0f}%")
        self.s_depth.pack(fill="x", pady=(0, px(13)))

        self.s_smooth = self._slider(
            move, "Smoothness", 0.0, 1.0, 0.35, self.on_smooth,
            fmt=lambda v: f"{v * 100:.0f}%")
        self.s_smooth.pack(fill="x", pady=(0, px(13)))

        self.s_angle = self._slider(
            move, "Manual position", -180, 180, 0.0, self.on_angle,
            fmt=lambda v: f"{v:+.0f}°", hint="Used by the Static position mode")
        self.s_angle.pack(fill="x")

        # ---- character -------------------------------------------------------
        tone = self._block(rail, "Character", (22, 0))
        self.character_box = self._dropdown(
            tone, [CHARACTER_LABELS[c] for c in CHARACTERS],
            CHARACTER_LABELS["clean"], self.on_character)
        self.character_box.pack(fill="x", pady=(0, px(13)))
        self.s_character = self._slider(
            tone, "Character amount", 0.0, 1.0, 1.0, self.on_character_amount,
            fmt=pct, hint=CHARACTER_HINTS["clean"])
        self.s_character.pack(fill="x")

        # ---- space ---------------------------------------------------------
        space = self._block(rail, "Space", (22, 0))

        self.s_width = self._slider(
            space, "Stereo width", 0.0, 2.0, 1.0, self.on_width,
            fmt=lambda v: f"{v * 100:.0f}%")
        self.s_width.pack(fill="x", pady=(0, px(14)))

        grid = tk.Frame(space, bd=0, highlightthickness=0)
        grid.pack(fill="x")
        self._paint(grid, bg="RAIL")
        grid.columnconfigure(0, weight=1, uniform="pair")
        grid.columnconfigure(1, weight=1, uniform="pair")

        cells = [
            ("s_delay", "Delay", 0.0, 1.0, 0.0, self.on_delay_mix, pct),
            ("s_reverb", "Reverb", 0.0, 1.0, 0.18, self.on_reverb_mix, pct),
            ("s_delay_time", "Delay time", 0.04, 1.2, 0.28, self.on_delay_time,
             lambda v: f"{v * 1000:.0f} ms"),
            ("s_reverb_size", "Room size", 0.0, 1.0, 0.6, self.on_reverb_size, pct),
            ("s_delay_fb", "Delay feedback", 0.0, 0.85, 0.35, self.on_delay_fb, pct),
            ("s_reverb_damp", "Damping", 0.0, 1.0, 0.45, self.on_reverb_damp, pct),
        ]
        for i, (attr, text, lo, hi, value, cb, fmt) in enumerate(cells):
            slider = self._slider(grid, text, lo, hi, value, cb, fmt=fmt, compact=True)
            slider.grid(row=i // 2, column=i % 2, sticky="ew",
                        padx=(0, px(11)) if i % 2 == 0 else (px(11), 0),
                        pady=(0, px(14)))
            setattr(self, attr, slider)

        # ---- output ---------------------------------------------------------
        out = self._block(rail, "Output", (8, 0))

        dev_row = tk.Frame(out, bd=0, highlightthickness=0)
        dev_row.pack(fill="x", pady=(0, px(11)))
        self._paint(dev_row, bg="RAIL")
        self.device_box = self._dropdown(dev_row, [], "", self.on_device)
        self.device_box.pack(side="left", fill="x", expand=True)
        refresh = PressButton(dev_row, "Refresh", self.refresh_devices,
                              bg="RAIL", fg="ACCENT_TEXT", hover="ACCENT_SOFT",
                              font=fonts["ghost"], padx=11, pady=9)
        refresh.pack(side="left", padx=(px(8), 0))
        self._themed.append(refresh)

        lat_row = tk.Frame(out, bd=0, highlightthickness=0)
        lat_row.pack(fill="x", pady=(0, px(11)))
        self._paint(lat_row, bg="RAIL")
        latency = self.prefs.get("latency", DEFAULT_LATENCY)
        if latency not in LATENCY_PROFILES:
            latency = DEFAULT_LATENCY
        self.latency_box = self._dropdown(lat_row, list(LATENCY_PROFILES), latency,
                                          lambda _v: None)
        self.latency_box.pack(side="left", fill="x", expand=True)
        self.bypass = CheckBox(lat_row, "Bypass", self.on_bypass, fonts["field"])
        self.bypass.pack(side="left", padx=(px(11), 0))
        self._themed.append(self.bypass)

        self.s_gain = self._slider(out, "Output volume", 0.0, 1.5, 0.9, self.on_gain,
                                   fmt=pct)
        self.s_gain.pack(fill="x")

    def _block(self, rail, title, pady):
        wrapper = tk.Frame(rail, bd=0, highlightthickness=0)
        wrapper.pack(fill="x", pady=tuple(px(v) for v in pady))
        self._paint(wrapper, bg="RAIL")
        self._section(wrapper, title).pack(fill="x", pady=(0, px(13)))
        return wrapper

    # -- parameter callbacks ------------------------------------------------

    def on_speed(self, v): self.engine.update(speed=float(v))
    def on_radius(self, v): self.engine.update(radius=float(v))
    def on_depth(self, v): self.engine.update(depth=float(v))
    def on_smooth(self, v): self.engine.update(smoothness=float(v))
    def on_width(self, v): self.engine.update(width=float(v))
    def on_delay_mix(self, v): self.engine.update(delay_mix=float(v))
    def on_delay_time(self, v): self.engine.update(delay_time=float(v))
    def on_delay_fb(self, v): self.engine.update(delay_feedback=float(v))
    def on_reverb_mix(self, v): self.engine.update(reverb_mix=float(v))
    def on_reverb_size(self, v): self.engine.update(reverb_size=float(v))
    def on_reverb_damp(self, v): self.engine.update(reverb_damp=float(v))
    def on_gain(self, v): self.engine.update(output_gain=float(v))
    def on_angle(self, v): self.engine.update(manual_angle=math.radians(float(v)))
    def on_device(self, _label=None):
        """Switching output while running moves playback there and then."""
        sink = self._selected_sink()
        if sink is None or not self.engine.status.running:
            return
        if self.engine.retarget(sink.name):
            self._output_moves = self.engine.status.output_moves
            self.set_status(f"Output moved to {sink}.", "good")

    def on_mode(self, _label=None):
        mode = LABEL_TO_MODE.get(self.mode_box.get(), "circular")
        self.engine.update(mode=mode)
        self._update_mode_dependent(mode)

    def on_direction(self, _label=None):
        self.engine.update(direction=1 if self.dir_box.get() == "Clockwise" else -1)

    def on_character(self, _label=None):
        character = LABEL_TO_CHARACTER.get(self.character_box.get(), "clean")
        self.engine.update(character=character)
        self._update_character_dependent(character)

    def on_character_amount(self, v):
        self.engine.update(character_amount=float(v))

    def _update_character_dependent(self, character: str):
        self.s_character.set_hint(CHARACTER_HINTS.get(character, ""))
        self.s_character.set_enabled(character != "clean")

    def on_bypass(self):
        self.engine.update(enabled=not self.bypass.get())

    def toggle_bypass(self):
        self.bypass.set(not self.bypass.get())
        self.on_bypass()

    def _update_mode_dependent(self, mode: str):
        self.s_angle.set_enabled(mode == "static")
        self.s_speed.set_enabled(mode != "static")

    def apply_preset(self, name: str):
        values = PRESETS.get(name)
        if not values:
            return
        params = self.engine.update(**values)
        self._sync_widgets(params)
        self.stage.set_active_preset(name)
        self.set_status(f"Preset applied: {name}", "note")

    def _sync_widgets(self, p: Params):
        self.mode_box.set(MODE_LABELS.get(p.mode, MODE_LABELS["circular"]))
        self.dir_box.set("Clockwise" if p.direction >= 0 else "Counter-clockwise")
        self.s_speed.set(p.speed)
        self.s_radius.set(p.radius)
        self.s_depth.set(p.depth)
        self.s_smooth.set(p.smoothness)
        self.s_width.set(p.width)
        self.s_angle.set(math.degrees(p.manual_angle))
        self.s_delay.set(p.delay_mix)
        self.s_delay_time.set(p.delay_time)
        self.s_delay_fb.set(p.delay_feedback)
        self.s_reverb.set(p.reverb_mix)
        self.s_reverb_size.set(p.reverb_size)
        self.s_reverb_damp.set(p.reverb_damp)
        self.s_gain.set(p.output_gain)
        self.character_box.set(CHARACTER_LABELS.get(p.character,
                                                    CHARACTER_LABELS["clean"]))
        self.s_character.set(p.character_amount)
        self.bypass.set(not p.enabled)
        self._update_mode_dependent(p.mode)
        self._update_character_dependent(p.character)

    # -- devices & engine ---------------------------------------------------

    def refresh_devices(self):
        try:
            self.sinks = pw.list_sinks()
        except pw.PipeWireError as exc:
            self.sinks = []
            self.set_status(f"Could not read audio devices: {exc}", "bad")
            return

        labels = [str(s) for s in self.sinks]
        self.device_box.configure_values(labels)
        if not labels:
            self.device_box.set("")
            self.set_status("No audio output devices found.", "bad")
            return

        saved = self.prefs.get("device")
        chosen = next((str(s) for s in self.sinks if s.name == saved), None)
        if self.device_box.get() in labels:
            chosen = chosen or self.device_box.get()
        self.device_box.set(chosen or labels[0])

    def _selected_sink(self) -> pw.Sink | None:
        label = self.device_box.get()
        return next((s for s in self.sinks if str(s) == label), None)

    def toggle_engine(self):
        if self.engine.status.running:
            self.stop_engine()
        else:
            self.start_engine()

    def start_engine(self):
        sink = self._selected_sink()
        if sink is None:
            self.set_status("Pick an output device first.", "bad")
            return
        try:
            self.engine.start(sink.name, LATENCY_PROFILES[self.latency_box.get()])
        except Exception as exc:  # noqa: BLE001 - surfaced in the UI
            self.set_status(f"Could not start: {exc}", "bad")
            return
        self._output_moves = self.engine.status.output_moves
        self.start_button.configure(text="Stop")
        self.start_button.set_tokens("MOTION", "ON_MOTION", "MOTION_TEXT")
        self.stage.state_label = "Processing system audio"
        self.set_status(f"Routed through 8D and out to {sink}.", "good")

    def stop_engine(self, message="Audio is back to normal."):
        self.engine.stop()
        self.start_button.configure(text="Start")
        self.start_button.set_tokens("ACCENT", "ON_MOTION", "ACCENT_TEXT")
        self.stage.trail.clear()
        self.stage.state_label = "Stopped"
        self.set_status(message, "info")

    def set_status(self, text: str, tone: str = "info"):
        self.stage.status_text = text
        self.stage.status_tone = tone if tone in TONES else "info"

    # -- animation ----------------------------------------------------------

    def _tick(self):
        status = self.engine.status
        running = status.running
        self.stage.refresh(self.engine.angle, self.engine.distance, running,
                           self.engine.levels(), status.load)

        if status.output_moves != self._output_moves:
            self._output_moves = status.output_moves
            self._follow_engine_output(status.output)

        if not running and self.start_button.cget("text") == "Stop":
            detail = describe_error(self.engine) or status.error
            self.stop_engine(f"Audio stream stopped: {detail}" if detail
                             else "Audio stream stopped.")

        self.after(33, self._tick)

    def _follow_engine_output(self, node: str | None):
        """The engine moved playback on its own -- show where it went."""
        self.refresh_devices()
        sink = next((s for s in self.sinks if s.name == node), None)
        if sink is not None:
            self.device_box.set(str(sink))
            self.set_status(f"Output followed the system to {sink}.", "good")

    # -- shutdown ------------------------------------------------------------

    def on_close(self):
        sink = self._selected_sink()
        prefs = {
            "device": sink.name if sink else None,
            "latency": self.latency_box.get(),
            "theme": self.theme,
        }
        try:
            self.engine.stop()
        finally:
            config.save(self.engine.params, prefs)
            self.destroy()


def main() -> int:
    app = App()
    try:
        app.mainloop()
    except KeyboardInterrupt:
        app.on_close()
    return 0
