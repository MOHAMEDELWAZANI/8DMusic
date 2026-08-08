<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="eight_d/assets/logo-lockup-dark.png">
    <img src="eight_d/assets/logo-lockup-light.png" alt="8D Music — spatial audio, live" width="415">
  </picture>
</p>

<p align="center">
  <img alt="audio: PipeWire" src="https://img.shields.io/badge/audio-PipeWire-blue">
  <img alt="python: 3.9+" src="https://img.shields.io/badge/python-3.9%2B-green">
</p>

Real-time 8D spatial audio for **everything your computer plays**. No uploading,
no converting, no per-file processing — YouTube, Spotify, games, movies, Discord
and anything else are all spatialised live on their way to your headphones.

## Run it

```bash
cd ~/Projects/8DMUSIC
./run.sh
```

The first launch creates a local `.venv` and installs NumPy and SciPy into it;
after that it starts immediately. Then:

1. Pick your **Device** (the headphones or speakers you actually listen through).
2. Press **Start**.
3. Play something. The sound begins orbiting your head.

Press **Stop**, or just close the window, and your audio goes straight back to
normal.

Useful flags:

```bash
./run.sh --check          # verify the system is ready, list outputs
./run.sh --list-devices   # same thing
```

## How it works

The app inserts a virtual output device into the audio graph and makes it the
system default, so every application ends up playing into it:

```
  apps ──▶ [ 8D Music virtual sink ] ──▶ monitor ──▶ DSP ──▶ your speakers
```

Audio is captured from that sink's monitor, processed, and played out to the
device you chose. Because the effect sits at the very end of the chain, it
applies to all sound at once and needs no cooperation from the apps producing it.

The virtual sink is owned by the running process. If the app is killed — even
with `SIGKILL` — PipeWire tears the sink down and your previous default device
comes back automatically.

## The effect

The audio is treated as a **virtual sound source orbiting the listener**. Rather
than just swinging the stereo balance, the position is converted into the cues a
real source would produce:

| Cue | What it does |
| --- | --- |
| **Interaural time difference** | The far ear hears the sound up to ~0.7 ms later. This is what pushes the image outside your head instead of leaving it stuck between your ears. |
| **Interaural level difference** | Constant-power panning, so the loudness stays steady as the source travels. |
| **Head shadow** | Your skull blocks high frequencies, so the far ear gets a gentle treble roll-off. |
| **Front/back cue** | Positions behind you lose a little upper-mid, the way the outer ear shapes sound from the rear. |
| **Distance** | Level, air absorption and reverb send all follow the orbit radius. |

Delay times and pan gains are interpolated per sample, so nothing clicks or
zippers while you move the controls.

## Controls

| Control | What it does |
| --- | --- |
| **Movement speed** | How fast the source travels — shown both as orbits/second and seconds per lap. |
| **Orbit radius** | Virtual distance to the source. Close is louder, brighter and more extreme; far is quieter, darker and wetter. |
| **Effect depth** | How far through the stereo field it swings. At 0 the source sits still in front of you. |
| **Smoothness** | Rounds off the motion. Low is mechanical and sharp; high is gradual and natural. |
| **Movement mode** | See below. |
| **Stereo width** | Width of the source material before it is placed into the orbit. Below 100% narrows toward mono, above widens. |
| **Delay** | Cross-fed ping-pong echo — taps bounce ear to ear. |
| **Reverb** | Freeverb-style room. Room size and damping shape the tail. |
| **Output volume** | Final gain, ahead of the built-in limiter. |

### Movement modes

- **Circular orbit** — the classic 8D lap around your head.
- **Ping-pong** — constant-speed sweeps left to right with hard turnarounds.
- **Pendulum** — sine easing, slowing at the extremes.
- **Linear sweep** — travels one way, then restarts.
- **Figure eight** — swings side to side while the distance breathes.
- **Spiral** — orbits while drifting nearer and further away.
- **Random drift** — wanders unpredictably, never jumping.
- **Static position** — parks the source wherever the **Manual position** slider points.

### Presets

Classic 8D · Slow Orbit · Ping-Pong · Wide Cinema · Subtle Motion · Extreme Spin
· Deep Space · Figure Eight

### Shortcuts

| Key | Action |
| --- | --- |
| `Space` | Bypass / un-bypass the effect (handy for A/B comparison) |
| `Ctrl+R` | Start / stop the engine |

## Tips

- **Use headphones.** The effect relies on each ear hearing a different signal;
  speakers blend them together and most of the illusion is lost.
- Around **0.1 rot/s** (a 10-second lap) is the classic 8D feel. Faster starts to
  sound like a special effect.
- If the movement feels seasick, lower **Effect depth** or raise **Smoothness**.
- **Latency** only affects how quickly control changes are heard. *Balanced* is
  fine for music and video; drop to *Low* for games, raise to *Safe* if you hear
  dropouts.
- Volume keys and the system mixer keep working normally — they act on the
  virtual sink while the effect runs.

## Requirements

- Linux with **PipeWire** (Ubuntu 22.10+, Fedora 34+, and most current distros)
- Python 3.9 or newer, with Tk
- NumPy and SciPy — installed automatically into `.venv` on first run

If `./run.sh --check` reports missing pieces:

```bash
sudo apt install pipewire-bin python3-venv python3-tk
```

## Add it to your app menu

<img src="eight_d/assets/icon-128.png" alt="8D Music app icon" width="72" align="right">

```bash
cp 8dmusic.desktop ~/.local/share/applications/
```

The launcher takes its icon from `eight_d/assets/`, so the entry shows the mark
at whatever size your desktop asks for.

## Troubleshooting

**No sound after pressing Start.** Make sure **Device** points at the output you
actually listen through, not at "8D Music" itself.

**An app is still playing dry.** A few apps pin themselves to a specific output.
The engine re-checks every two seconds and pulls them in; if one refuses, set its
output to *8D Music* in your system sound settings.

**Crackling or dropouts.** Set **Latency** to *Safe*. The status line shows the
CPU cost per block — sustained readings near 100% mean the machine can't keep up.

**The app crashed and my audio is odd.** It shouldn't persist: the virtual sink
dies with the process. If your default output is wrong, reset it in system sound
settings.

## Layout

```
eight_d/
  dsp.py        the spatial engine — orbit paths, ITD/ILD, head shadow, delay, reverb
  engine.py     the real-time thread and the pw-record → DSP → pw-play pipeline
  pipewire.py   virtual sink creation, stream routing, restoring things afterwards
  ui.py         the desktop interface and orbit visualiser
  config.py     settings persistence (~/.config/8dmusic/settings.json)
  assets/       app icons and the logo lockup
tools/
  make_icons.py regenerates everything in assets/ from the logo geometry
```

## Identity

The mark is four level bars — the sound itself — seated on a baseline, with an
arc sweeping beneath them for the orbit passing under the listener, and a dot
marking where the source is. Bars drop away as the icon gets smaller: four at
128 px and up, three at 64 and 32, two at 16.

<p align="center">
  <img src="eight_d/assets/icon-128.png" alt="" width="96">&nbsp;&nbsp;
  <img src="eight_d/assets/icon-64.png" alt="" width="64">&nbsp;&nbsp;
  <img src="eight_d/assets/icon-32.png" alt="" width="32">&nbsp;&nbsp;
  <img src="eight_d/assets/icon-16.png" alt="" width="16">
</p>

| | |
| --- | --- |
| `#0088B0` | accent — the sweep, active controls, section headings |
| `#D6006C` | the source dot, and the transport when it is running |
| `#201E1D` | ink |
| `#F3F2F2` | ground |

Everything in `eight_d/assets/` is generated. If the mark changes, edit the
geometry at the top of `tools/make_icons.py` and re-run it (it needs Pillow,
which the app itself does not):

```bash
.venv/bin/pip install pillow
.venv/bin/python tools/make_icons.py
```
