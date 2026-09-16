<p align="center">
  <a href="../README.md">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="../assets/logo-lockup-dark.png">
      <img src="../assets/logo-lockup-light.png" width="300" alt="8D Music">
    </picture>
  </a>
</p>

<h1 align="center">8D Music — C++ build</h1>

Real-time 8D spatial audio for **everything your computer plays**. The audio
path is native, the DSP runs inside PipeWire's realtime callback, and the
interface repaints only what moves.

The effect itself lives in [`src/dsp/`](src/dsp/) and is compiled in place by the
Windows and Android builds too — see the [parity table](../BENCHMARK.md).

## Run it

```bash
cd 8DMusic/cpp
./run.sh
```

The binary is already built and needs nothing but the libraries your desktop
already has. To rebuild after changing the source:

```bash
sudo apt install build-essential pkg-config libpipewire-0.3-dev \
                 libcairo2-dev libx11-dev libpango1.0-dev libfontconfig1-dev \
                 libsystemd-dev
make
```

Then pick your **output device** from the pill in the title bar, press
**Start** in the Engine card, and play something.

```bash
./8dmusic --check            # verify the system and list outputs
./8dmusic --shot out/        # render every page and both presentations to PNG
```

Full method and figures are in [`../BENCHMARK.md`](../BENCHMARK.md).

### Measured

| | |
| --- | ---: |
| Memory, idle | **38 MB** |
| Memory, running | **43 MB** |
| DSP cost | **0.4–1.1 %** of realtime |
| CPU, running with the UI open | **10 % of one core** |
| CPU, idle with the window open | **0 %** — it draws no frames |
| Frame cost | 3.1 ms at 31 fps |
| Pointer crossing controls | one 10 ms repaint per control entered |
| Binary | **662 KB** |

There are no helper processes. Audio never leaves this address space: two native
`pw_stream`s, the device list from the PipeWire registry rather than polling, and
MPRIS read directly over sd-bus.

## How it works

```
  apps ──▶ [ 8D Music virtual sink ] ──▶ DSP ──▶ your speakers
```

The virtual sink is a `pw_stream` published as `Audio/Sink`, so every
application can play into it and the system default can point at it. A second
stream feeds the device you chose. Audio never leaves this process, and the
effect runs inside PipeWire's realtime callback.

Both streams are owned by the process. If the app is killed — even with
`SIGKILL` — the sink goes with it and your previous default device comes back.

## The effect

The audio is treated as a **virtual sound source orbiting the listener**. The
position is turned into the cues a real source would produce:

| Cue | What it does |
| --- | --- |
| **Interaural time difference** | The far ear hears the sound up to ~0.7 ms later. This is what pushes the image outside your head. |
| **Interaural level difference** | Constant-power panning, so loudness stays steady as the source travels. |
| **Head shadow** | Your skull blocks high frequencies, so the far ear gets a treble roll-off. |
| **Front/back cue** | Positions behind you lose a little upper-mid. |
| **Distance** | Level, air absorption and reverb send all follow the orbit radius. |

Because the angle ramps linearly across a block, the per-sample sine and cosine
come from a rotation recurrence seeded once per block rather than a `sinf` call
per sample.

## The interface

Three pages behind one title bar, drawn to the v2 design.

- **Studio** — the whole instrument on one screen. The orbit on a lit floor
  with its readout, the level meters and the preset chips on the left;
  Movement, Space and Echo in one column of cards on the right, Character,
  Equaliser and Engine in the other; and the player floating across the foot
  of the window, because it drives whatever is playing rather than the effect.
  Character's Amount reads across as a slider — one long throw is easier to
  place than a dial. Nothing here opens another page.

  **Knobs are turned, not pulled.** Grab anywhere on a dial and move around it:
  the value follows how far *you* turn, never jumping to meet the pointer, and
  three quarters of a turn covers the range. Hold `Shift` for four times finer,
  roll the wheel for one step per notch, double-click to go back to the
  default. Values land on whole steps — a percent, a decibel, five
  milliseconds — rather than drifting, and a dot at the head of the arc marks
  where the value has reached.
- **About** — the story, both presentations, four written guides, the links and
  the donate card.
- **Account** — signed out only, for now.

A first run opens the **Welcome** presentation (five pages), and the first visit
to Studio opens the **Studio tour** (seven dialogs). Both are replayable from
About. On the welcome's first page the mark loops: the dot travels the dashed
ring once every seven seconds with its tail behind it, and the smile catches
the light as it passes the front. Only that patch repaints — the rest of the
page stays in the cached chrome. Knobs take a drag, the mouse wheel, or a double-click to reset.

The window wears **no decoration**: the app draws its own title bar and its own
rounded corners. That means it also does the three jobs the window manager used
to do — press any empty part of the bar to move the window (the drag is handed
back to the window manager, which knows about edges and monitors), and minimise
and close sit at the trailing edge of the bar. There is no maximise.

The corners are cut out of the window with the X shape extension rather than
painted with alpha, so they look the same whether or not anything is
compositing the screen, and clicks in them fall through to what is behind.

**The page is laid out in its own units and scaled to the screen.** `Xft.dpi`
from the resource database is how a desktop says what scale it is running at,
so a desktop at 200% gets a window twice the size with the drawing scaled to
match — text included, which stays sharp because it is a transform on the
drawing rather than an enlarged picture. It never grows past the screen it has
to live on. `Ctrl+plus` and `Ctrl+minus` change it by a quarter step and the
choice is remembered; `EIGHTD_SCALE=1.5` overrides the lot.

The interface is drawn in **Figtree** (SIL Open Font License), which travels
with the app in `assets/fonts/` and is registered with fontconfig for this
process only — nothing is installed. Without it the app falls back to whatever
sans the system has.

## Controls

Movement (mode, direction, speed, distance, intensity, smoothness) · Space
(reverb mix, room size, damping, stereo width) · Echo (mix, time, feedback) ·
Character and amount · Equaliser (bass, mid, treble, output) · Quality · Pause
orbit when silent · Bypass · Reset to defaults.

**Movement modes** — circular orbit, ping-pong, pendulum, linear sweep, figure
eight, spiral, random drift, static position.

**Character** — clean, *slowed & sad* (pitched down), *old radio* (mono,
band-limited, saturated, with tape wow and gated hiss).

**Presets** — Classic 8D · Slow Orbit · Ping-Pong · Wide Cinema · Subtle Motion
· Extreme Spin · Deep Space · Figure Eight · Slowed & Sad · Old Radio

### Shortcuts

| Key | Action |
| --- | --- |
| `Space` | Bypass / un-bypass the effect |
| `Ctrl+R` | Start / stop the engine |
| `Ctrl+Shift+R` | Reset every effect setting |
| `Ctrl+T` | Switch light / dark |
| `Ctrl+plus` / `Ctrl+minus` | Make the interface bigger or smaller |

## Verification

- **The effect is the same everywhere.** `tests/dsp_probe.cpp` renders a
  deterministic signal through every movement mode; the Windows and Android
  builds render the identical probe and agree to correlation **1.000000**,
  largest sample difference **0.000006**.
- **Audio really flows.** Driven end to end on an isolated sink: the stereo
  balance sweeps the full −1.00…+1.00, 420 blocks, **zero underruns**.
- **Routing is restored.** Start takes over the default sink, Stop puts it back
  — checked before, during and after.
- **The interface works.** The welcome flow, the tab pills, a movement tile, the
  output dropdown and a knob's double-click reset were driven with synthetic X
  events against the running window and confirmed from the pixels: the tile
  lights and the preset falls back to *Custom*, the list paints over the page,
  and the knob returns to its default.
- **The window really is undecorated.** `xwininfo` shows the app's window with
  no frame parent, `_NET_WM_ALLOWED_ACTIONS` carries neither resize, maximise
  nor full screen, the close button quits the app, and a press on the bar's
  drag handle leaves every other control still responding.
- **The welcome mark loops.** Two captures 1.2 s apart show the dot a step
  further round the ring, tail following, with the mark lit as it passes the
  front.
- **It matches the design.** `--shot` renders all three pages, both
  presentations and the four guides at the design's 1180×820, which is how each
  screen was put beside its frame on the canvas.
- **Now Playing reads the real bus.** Live titles, position, transport
  capabilities and the cover mark, checked case by case across 16 titles and 6
  cover marks, Arabic and Japanese among them.
- **Text truncation stays valid UTF-8** at every width across Arabic, Japanese
  and accented Latin — a string cut through a character puts cairo into a
  permanent error state and silently discards the rest of the frame.

## Now Playing

The card under the orbit shows what is actually playing: a lettered cover,
title, artist, elapsed and total time, a progress bar, and working previous /
play-pause / next buttons — which drive the player, not the effect. It comes from MPRIS on the session bus, read through **sd-bus** on a
background thread.

Titles are tidied on the way in: uploader suffixes (`- Topic`, `VEVO`) dropped,
`(Official Video)` and its relatives removed, `ft. …` moved out of the title and
into the performers.

Text is drawn with **Pango**, so scripts that need shaping and reordering get
them: an Arabic title joins up and reads right to left, and a face that lacks a
glyph falls back instead of printing a box. The cover mark works in characters
rather than bytes, so a non-Latin name gives its own initials.

## Layout

```
src/
  dsp/       Params, Filters, Reverb, Orbit, Character, Processor — the effect
  audio/     Graph (registry + metadata), Engine (the two streams, ring, routing),
             NowPlaying (MPRIS over sd-bus)
  ui/        Window (X11 + cairo), Widgets (immediate mode), Theme, Layout,
             Path + Icons (the design's SVG glyphs), Fonts,
             App (shell), Studio, Pages (About/Account), Tour (presentations)
tests/       dsp_probe (the shared parity probe), compare_f32.py,
             engine_probe (headless audio), nowplaying_probe (the session bus)
```

Settings live in `~/.config/8dmusic-cpp/settings.conf`.

Screenshots of this build: [light](../docs/cpp-light.png) · [dark](../docs/cpp-dark.png).

The design these pages are built to lives on the project canvas as
**Desktop v2 · Studio / About / Account**, plus the Welcome and Studio-tour
frames.
