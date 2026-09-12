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
                 libcairo2-dev libx11-dev libpango1.0-dev libsystemd-dev
make
```

Then pick your **output device**, press **Start**, and play something.

```bash
./8dmusic --check     # verify the system and list outputs
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
| Binary | **448 KB** |

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

- **The effect is the same everywhere.** `tests/dsp_probe.cpp` renders a
  deterministic signal through every movement mode; the Windows and Android
  builds render the identical probe and agree to correlation **1.000000**,
  largest sample difference **0.000006**.
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
  capabilities and the cover mark, checked case by case across 16 titles and 6
  cover marks, Arabic and Japanese among them.
- **Text truncation stays valid UTF-8** at every width across Arabic, Japanese
  and accented Latin — a string cut through a character puts cairo into a
  permanent error state and silently discards the rest of the frame.

## Now Playing

The head of the rail shows what is actually playing: cover mark, title, artist,
elapsed and total time, a progress bar, and working previous / play-pause / next
buttons. It comes from MPRIS on the session bus, read through **sd-bus** on a
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
  ui/        Window (X11 + cairo), Widgets (immediate mode), App, Theme, Config
tests/       dsp_probe (the shared parity probe), compare_f32.py,
             engine_probe (headless audio), nowplaying_probe (the session bus)
```

Settings live in `~/.config/8dmusic-cpp/settings.conf`.

Screenshots of this build: [light](../docs/cpp-light.png) · [dark](../docs/cpp-dark.png).
