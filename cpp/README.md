<p align="center">
  <a href="../README.md">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="../assets/logo-lockup-dark.png">
      <img src="../assets/logo-lockup-light.png" width="300" alt="8D Music">
    </picture>
  </a>
</p>

<h1 align="center">8D Music — C++ build</h1>

Real-time 8D spatial audio for **everything your computer plays**, rewritten in
C++. Same effect as the Python build, same presets, same numbers on the dials —
but the audio path is native and the interface repaints only what moves.

## Run it

```bash
cd 8DMusic/cpp
./run.sh
```

The binary is already built and needs nothing but the libraries your desktop
already has. To rebuild after changing the source:

```bash
sudo apt install build-essential pkg-config libpipewire-0.3-dev \
                 libcairo2-dev libx11-dev libpango1.0-dev libsystemd-dev
make
```

Then pick your **output device**, press **Start**, and play something.

```bash
./8dmusic --check     # verify the system and list outputs
```

The reference build lives in [`../python`](../python/); the full comparison
and how it was measured are in [`../BENCHMARK.md`](../BENCHMARK.md).

## What changed from the Python build

The effect is identical — that was checked numerically, not by ear (see
*Verification*). Everything that changed is in how the audio gets there.

| | Python | C++ |
| --- | --- | --- |
| Audio path | virtual sink + `pw-record` and `pw-play` **subprocesses** piped through the kernel | two native `pw_stream`s in this process |
| DSP | NumPy, per-block array allocation | in the realtime callback, zero allocation |
| Device list | `pw-dump` **subprocess** every 2 s, ~1 MB of JSON parsed each time | PipeWire registry, event-driven, no polling |
| Routing | `pw-metadata` subprocess per call | metadata proxy, in process |
| Now Playing | `busctl` **subprocess** per poll | sd-bus, spoken directly |
| Text | Tk, one face per string | Pango: shaping, bidi and font fallback |
| Interface | Tk, full-window repaint | cairo, only the orbit strip repaints |
| Helper processes while running | 4 | **0** |

### Measured

| | Python | C++ |
| --- | --- | --- |
| Memory (idle) | 124 MB | **38 MB** |
| Memory (running) | — | **43 MB** |
| DSP cost | 13–28 % of realtime | **0.4–1.1 %** |
| CPU, running with the UI open | — | **10 % of one core** |
| CPU, idle with the window open | — | **0 %** (it draws no frames) |
| Frame cost | — | 3.1 ms at 31 fps |
| Pointer crossing controls | — | one 10 ms repaint per control entered |
| Binary / install | ~223 MB `.venv` | **443 KB** |

The DSP speed-up measured **25–44×** depending on mode and how busy the
machine was; the ratio within any single run held between 25× and 31×.

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

## Controls

Movement speed · Orbit radius · Effect depth · Smoothness · Manual position ·
Stereo width · Delay (mix, time, feedback) · Reverb (mix, room size, damping) ·
Output volume · Bypass · Reset to defaults.

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

## Verification

- **The effect is unchanged.** The same signal was pushed through both builds:
  correlation **1.0000** on all seven movement modes, largest sample difference
  0.0006, identical RMS. `slowed` matches at 0.9889 (a pitch shifter drifts in
  phase); `radio` at 0.8767 because its hiss is random in both — its level
  matches to 0.999.
- **Audio really flows.** Driven end to end on an isolated sink: the stereo
  balance sweeps the full −1.00…+1.00, 420 blocks, **zero underruns**.
- **Routing is restored.** Start takes over the default sink, Stop puts it back
  — checked before, during and after.
- **The interface works.** Theme switch, preset apply, dropdown open and pick,
  and a slider drag were all driven with synthetic X events and confirmed. A
  preset takes effect on the **first** click — checked by sampling the chip's
  pixels against the theme's accent colour, not by eye — and hover highlighting
  updates as the pointer crosses controls.
- **Now Playing reads the real bus.** Live titles, position, transport
  capabilities and the cover mark, checked against the Python build: 16/16
  titles and 6/6 cover marks identical, Arabic and Japanese among them.
- **Text truncation stays valid UTF-8** at every width across Arabic, Japanese
  and accented Latin — a string cut through a character puts cairo into a
  permanent error state and silently discards the rest of the frame.

## Now Playing

The head of the rail shows what is actually playing: cover mark, title, artist,
elapsed and total time, a progress bar, and working previous / play-pause / next
buttons. It comes from MPRIS on the session bus, read through **sd-bus** on a
background thread — the Python build ran `busctl` once a second instead.

Titles are tidied the same way: uploader suffixes (`- Topic`, `VEVO`) dropped,
`(Official Video)` and its relatives removed, `ft. …` moved out of the title and
into the performers. That was checked case by case against the Python cleaner —
16 of 16 identical, Arabic included.

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
  ui/        Window (X11 + cairo), Widgets (immediate mode), App, Theme, Config
tests/       dsp_probe (cross-check against Python), engine_probe (headless audio),
             nowplaying_probe (what the session bus reports)
```

Settings live in `~/.config/8dmusic-cpp/settings.conf`.

Screenshots of this build: [light](../docs/cpp-light.png) · [dark](../docs/cpp-dark.png).
