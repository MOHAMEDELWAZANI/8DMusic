<p align="center">
  <img src="assets/8Dcover.png" alt="8D Music — spatial audio, live">
</p>

<p align="center">
  <img alt="Linux + PipeWire" src="https://img.shields.io/badge/Linux-PipeWire-0E7FA8">
  <img alt="Windows APO" src="https://img.shields.io/badge/Windows-native%20APO-0078D4">
  <img alt="Android" src="https://img.shields.io/badge/Android-10%2B-3DDC84">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C">
  <img alt="effect: identical across builds" src="https://img.shields.io/badge/effect-identical%20across%20builds-1C9E63">
</p>

<p align="center">
  <b>Real-time 8D spatial audio for everything your device plays.</b><br>
  No uploading, no converting, no per-file processing — YouTube, Spotify, games,
  movies, Discord and anything else are spatialised live on their way to your
  headphones.
</p>

---

## One effect, three platforms

The effect exists exactly once, in [`cpp/src/dsp/`](cpp/src/dsp/). Linux, Windows
and Android each compile those same files **in place** — nobody keeps a copy.
Both native build files fail loudly if the shared DSP is missing, because the
moment one platform forks the effect, "identical everywhere" becomes a claim
nobody can check.

That claim is measured rather than asserted. Each platform renders the same
deterministic probe — 220 Hz, five seconds, all eight movement modes — and the
raw output is compared sample by sample against the Linux build:

| | [Linux](cpp/) | [Windows](windows/) | [Android](android/) |
| --- | --- | --- | --- |
| Correlation | reference | **1.000000** | **1.000000** |
| Largest sample difference | — | **0.000006** | **0.000006** |
| Reaches system audio via | PipeWire virtual sink | native APO in `audiodg` | playback capture + Shizuku |
| Interface | cairo + Pango on X11 | Win32 + GDI | Kotlin + Compose |
| Install | one 448 KB binary | installer, one reboot | 30 MB APK |
| Needs admin / root | no | admin once | no root |

Same source, two compilers, three operating systems, three CPU architectures —
and the audio still matches to six decimal places.

<p align="center">
  <img src="docs/cpp-dark.png" width="900"
       alt="The Linux build in dark mode: the orbit visualiser fills the left of the window with the source mid-lap, and the right rail carries the now playing panel above the movement controls">
</p>
<p align="center"><sub>The Linux build. The rail scrolls — a taller window shows the rest of it.</sub></p>

## Quick start

```bash
git clone git@github.com:MOHAMEDELWAZANI/8DMusic.git
cd 8DMusic
```

**Linux** — nothing to configure, no reboot, no admin.

```bash
sudo apt install build-essential pkg-config libpipewire-0.3-dev \
                 libcairo2-dev libx11-dev libpango1.0-dev libsystemd-dev
cd cpp && make && ./8dmusic
```

Pick your **output device**, press **Start**, play something. Press **Stop**, or
close the window, and your audio goes back to normal. `--check` verifies the
system and lists outputs.

**Windows** — Visual Studio 2022 with the Desktop C++ workload.

```
cd windows
cmake -B build -A x64
cmake --build build --config Release
```

Then run the installer in [`windows/installer/`](windows/installer/). It needs
administrator rights once and a restart, after which the effect is in the audio
path permanently and `8DMusic.exe` only controls it. Your selected output device
stays selected — nothing is rerouted and no virtual cable is involved.

**Android** — Android Studio, or:

```bash
cd android && ./gradlew :app:assembleDebug
```

## How each one reaches the audio

The effect is the same everywhere. Getting *to* the audio is the part each
operating system decides for you, and all three answers are different.

**Linux** inserts a virtual output device into the PipeWire graph and makes it
the system default, so every application ends up playing into it:

```
  apps ──▶ [ 8D Music virtual sink ] ──▶ DSP ──▶ your headphones
```

The sink belongs to the running process: kill the app — even with `SIGKILL` —
and PipeWire tears the sink down and your previous default comes back.

**Windows** puts the DSP inside the audio engine itself, as an Audio Processing
Object that Windows loads into `audiodg.exe`:

```
  apps ──▶ Windows audio engine ──▶ [ 8D Music APO ] ──▶ your headphones
```

Nothing is rerouted, so the user's chosen output device stays chosen. The
control window is a separate process and talks to the APO through shared memory,
which is why closing the window cannot interrupt playback.

Getting an APO to load at all took considerable work; the full recipe — five
things that must *all* be true, or Windows skips the effect in complete silence
with no error anywhere — is written up in
[`windows/docs/APO-RESEARCH.md`](windows/docs/APO-RESEARCH.md).

**Android** has no equivalent of either, and offers three routes with different
costs, so the app asks which you want:

| Route | Setup | Reaches |
| --- | --- | --- |
| Local player | none | your own files |
| Direct capture | none | apps that permit playback capture |
| System-wide | Shizuku, once | everything, including apps that block capture |

System-wide works by capturing playback and *silencing the original stream* —
otherwise you hear the dry and spatialised versions at once, which for an effect
built on a sub-millisecond delay between the ears destroys exactly what it is
doing. Silencing needs other apps' audio session ids, which Android will not
give an ordinary app, so Shizuku grants `android.permission.DUMP` **once**. It
survives reboots and app updates; Shizuku is never needed again afterwards.

## The effect

The audio is treated as a **virtual sound source orbiting the listener**. Rather
than just swinging the stereo balance, the position is converted into the cues a
real source would produce:

| Cue | What it does |
| --- | --- |
| **Interaural time difference** | The far ear hears the sound up to ~0.7 ms later. This is what pushes the image outside your head instead of leaving it stuck between your ears. |
| **Interaural level difference** | Constant-power panning, so loudness stays steady as the source travels. |
| **Head shadow** | Your skull blocks high frequencies, so the far ear gets a gentle treble roll-off. |
| **Front/back cue** | Positions behind you lose a little upper-mid, the way the outer ear shapes sound from the rear. |
| **Distance** | Level, air absorption and reverb send all follow the orbit radius. |

Delay times and pan gains are interpolated per sample, so nothing clicks or
zippers while you move the controls.

**Movement modes** — circular orbit, ping-pong, pendulum, linear sweep, figure
eight, spiral, random drift, static position.

**Character** — clean, *slowed & sad* (pitched down), *old radio* (mono,
band-limited, softly saturated, with tape wow and gated hiss).

**Presets** — Classic 8D · Slow Orbit · Ping-Pong · Wide Cinema · Subtle Motion ·
Extreme Spin · Deep Space · Figure Eight · Slowed & Sad · Old Radio

**Controls** — movement speed, orbit radius, effect depth, smoothness, manual
position, stereo width, delay (mix / time / feedback), reverb (mix / room size /
damping), three-band tone, output volume, bypass, reset to defaults.

| Key | Action |
| --- | --- |
| `Space` | Bypass / un-bypass, for A/B |
| `Ctrl+R` | Start / stop the engine |
| `Ctrl+Shift+R` | Reset every effect setting |
| `Ctrl+T` | Switch light / dark |

## Now playing

Every build shows what you are actually listening to — title, artists, elapsed
time — with skip and play/pause that drive the player itself, not the effect.
Each platform has its own source for this, and the same panel on top:

| | Reads from |
| --- | --- |
| Linux | MPRIS over the session bus |
| Windows | `GlobalSystemMediaTransportControlsSessionManager` |
| Android | `MediaSessionManager` |

Nothing leaves the machine.

<p align="center">
  <img src="docs/now-playing-dark.png" width="840"
       alt="The now playing panel in dark mode, twice. Left: Love The Way You Lie by Eminem and Rihanna with a cover reading Er. Right: an Arabic title by أم كلثوم, running right to left with its letters joined and the cover reading أم">
</p>

Titles are tidied on the way in, because uploaders put far more than the song
name in them: `Eminem - Love The Way You Lie ft. Rihanna` from a `- Topic`
channel becomes **Love The Way You Lie** by **Eminem · Rihanna**. The cover is a
lettered tile — two artists give their initials, one artist gives its opening
pair.

Arabic needs shaping and reordering before it can be drawn; the Linux build
hands that to Pango, which also covers every other script and falls back when a
face lacks a glyph.

One limit worth stating plainly: only apps that publish a media session appear.
Spotify does, and Chromium browsers report whatever a page declares. Discord and
most games do not — they will read as *nothing playing* while plainly audible.
That is the API's boundary, not a bug.

## Measured

The parity probe is the project's own check that the builds agree, and it runs
on each platform:

```bash
# Linux — build the reference and render every mode
g++ -std=c++20 -O3 -ffast-math -fno-math-errno \
    -o dsp_probe cpp/tests/dsp_probe.cpp cpp/src/dsp/Processor.cpp -Icpp/src/dsp

# compare any two sets of dumps
python3 cpp/tests/compare_f32.py REFERENCE_DIR CANDIDATE_DIR
```

Windows has [`windows/parity.ps1`](windows/parity.ps1); Android runs the same
probe from the app and the output is pulled with `adb`.

Compiler flags are matched deliberately — `-O3 -ffast-math -fno-math-errno` on
GCC and Clang, `/O2 /fp:fast` on MSVC — because parity to six decimal places is
only meaningful if the arithmetic is allowed to be the same.

Performance and method for the Linux build are in
**[BENCHMARK.md](BENCHMARK.md)**.

## Layout

```
8DMusic/
├── README.md            this file
├── BENCHMARK.md         how the effect performs, and how that was measured
├── HANDOFF.md           where each build stands, for picking the work back up
├── assets/              cover and logo lockups
├── docs/                screenshots, and the UI exports under docs/design/
│
├── cpp/                 Linux — see cpp/README.md
│   ├── src/dsp/         THE EFFECT. shared by all three builds
│   ├── src/audio/       PipeWire graph, engine, MPRIS over sd-bus
│   ├── src/ui/          X11 + cairo + Pango
│   └── tests/           the parity probe and its comparison script
│
├── windows/             Windows — see windows/README.md
│   ├── src/apo/         the APO Windows loads into audiodg
│   ├── src/gui/         the control window
│   ├── src/shared/      shared memory between the two
│   ├── installer/       registration, and the uninstaller that reverses it
│   └── docs/            APO-RESEARCH.md and RECOVERY.md
│
└── android/             Android — see android/README.md
    ├── app/src/main/cpp/        JNI bridge, AAudio engine
    └── app/src/main/java/       Compose UI, capture service, Shizuku
```

## Requirements

Use **headphones** on every platform. The effect relies on each ear hearing a
different signal; speakers blend them together and most of it disappears.

| | |
| --- | --- |
| **Linux** | PipeWire — Ubuntu 22.10+, Fedora 34+, most current distros |
| **Windows** | Windows 10 or 11. Administrator for the install, and one restart |
| **Android** | 10 or newer. System-wide mode additionally needs Shizuku, once |
