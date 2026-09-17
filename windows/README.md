# 8D Music — Windows

The same effect as the Linux and Android builds, placed where Windows keeps its
audio processing: inside the audio engine, after every application and before
the hardware.

```
                8DMusic.exe
             GUI / controls
                    │
                    │  parameters  (shared memory)
                    ▼
          ┌───────────────────┐
          │   8DMusicAPO.dll  │   loaded into audiodg.exe
          │                   │
          │  cpp/src/dsp      │   the same files the desktop
          │  Processor        │   and Android builds compile
          └─────────┬─────────┘
                    ▼
           Windows audio engine
                    │
      ┌─────────────┼─────────────┐
      ▼             ▼             ▼
   Spotify      YouTube        Games
      └─────────────┼─────────────┘
                    ▼
                🎧 output
```

## The effect is not copied here

`CMakeLists.txt` compiles `../cpp/src/dsp` in place and fails loudly if it
cannot find it. One copy of the orbit, the head model and the reverb serves
Linux, Android and Windows. A fix in one is a fix in all three.

Compiler flags are `/O2 /fp:fast`, MSVC's nearest equivalent to the
`-O3 -ffast-math` the Makefile uses.

## Why an APO rather than a virtual device

Windows lets an effect sit in the engine itself, so nothing has to be rerouted
and no extra device appears in the mixer. That is the same position the
PipeWire sink occupies on Linux — the difference is only how each system lets
you get there.

The cost is that the DLL runs inside `audiodg.exe`, a process we do not own:

* No allocation, no locks, no I/O in `APOProcess`. `Processor` sizes everything
  in `LockForProcess` and allocates nothing afterwards, so it already obeys this.
* A crash takes system audio down, not just this app.
* Parameters cannot be a pointer — hence the shared block in `src/shared`.

`audiodg` runs at a different integrity level, so the mapping is created with an
explicit low-integrity label. Without it the APO loads, runs, and silently never
sees a single setting.

## Build

Visual Studio 2022 with the Desktop C++ workload and the Windows SDK.

```
cd windows
cmake -B build -A x64
cmake --build build --config Release
```

Two artefacts: `8DMusicAPO.dll` (the effect) and `8DMusic.exe` (the window).

## Install

`installer\inno\8DMusic.iss` builds `8DMusic-Setup.exe`, which does all of it:
drops the DLL and the exe into `%ProgramFiles%\8DMusic`, registers the COM
class, creates the shared block with a DACL audiodg can read, writes the CLSID
into the chosen endpoint's `FxProperties`, and restarts. It keeps a backup of
every value it touched in `install-backup.ini` so the uninstaller can put the
machine back exactly as it found it.

What has to be true for the APO to load at all is in `docs\APO-RESEARCH.md` --
five separate conditions, and missing any one of them makes Windows skip the
effect without logging anything anywhere. `docs\RECOVERY.md` is the way back if
audio is broken and this window cannot be trusted.

## The window

`8DMusic.exe` is a control surface and nothing else: it writes parameters into
the shared block and draws what the telemetry says the orbit is doing. Changing
how it looks touches neither `src/apo` nor `src/shared`, which is why the
interface could be replaced without going near the audio path.

Three pages behind one title bar, as on Linux: **Studio**, **About** and
**Account**, ported from `cpp/src/ui/Studio.cpp` and `cpp/src/ui/Pages.cpp`,
which `src/gui/Main.cpp` mirrors function for function.

About differs in two places, because the thing behind them does not exist here.
The desktop build has a Presentations card that replays the welcome flow and the
studio tour, and Windows has neither; and its first guide explains a PipeWire
virtual sink, where this one has to explain an APO.

Studio itself:

* the orbit on a lit floor with its readout, the meters and the preset chips on
  the left;
* Movement, Space and Echo in one column of cards, Character, Equaliser and
  Endpoint in the other;
* knobs you **turn** — grab one anywhere and move around it, whole-step values,
  a dot at the head of the arc — and Character's Amount as a slider;
* the media session floating across the foot of the window;
* the app's own title bar, because the window wears no decoration: minimise and
  close at the trailing edge, and any empty part of the bar drags it.

Where the Linux build has an ENGINE card with a Start button, Windows has
ENDPOINT: there is nothing to start, only somewhere to be. It shows the default
output, whether the APO is in the path, and why not when it is not.

Two places where it deliberately does *not* follow the Linux build:

* The output is a **readout, not a picker**. Linux can move its capture to
  another sink; here the effect is an APO the installer attached to an endpoint,
  and nothing in this build may change the user's output device. It was a
  dropdown, and it was a lie -- the device refresh put the choice back two
  seconds later.
* The player has **no transport buttons**. It reports what the media session is
  playing; it does not drive it.

Three files carry the shared design rather than a copy of it:
`cpp/src/ui/Theme.h` for the colours, `cpp/src/ui/Icons.h` for the glyphs (path
data with no drawing library behind it) and `cpp/src/dsp/Params.h` for the
presets. `src/gui/Path.h` draws that path data with GDI+, as `Path.h` does with
cairo on Linux.

The interface is drawn in Figtree when `Figtree.ttf` sits beside the exe, in
`assets\fonts\` under it, or in the source tree — the same file the Linux build
loads. Without it Segoe UI takes over and everything still lays out.

## Status

Working. The APO loads into `audiodg.exe`, processes, and the window reads its
telemetry live. Parity with the Linux build is measured rather than claimed:
`parity.ps1` renders the shared probe on both and compares -- correlation
1.000000, largest sample difference 0.000006, which is the same bar Android met.

The known gap: the effect registers into the SFX slot (`,5`), not EFX (`,7`).
`,7` was tried with the working APO and does not load. The consequence is that
each application gets its own instance, so two programs playing at once orbit
independently rather than as one scene.

## Testing the window

`ui_probe.exe` drives a running `8DMusic.exe` through real window messages --
clicks, drags, the wheel, the keyboard -- and after every action reads
`%ProgramData%\8DMusic\state.bin` to check the field that control is wired to
actually moved, by the amount it should have.

```
cmake --build build --config Release --target 8DMusic ui_probe
build\Release\8DMusic.exe
build\Release\ui_probe.exe          # fast: messages posted straight to the window
build\Release\ui_probe.exe --real   # slow: drives the actual cursor
```

Run `--real` before believing a pass. The default mode posts into the client
area, which skips `WM_NCHITTEST` -- and that is where the worst bug this window
has had lived: every control in the title bar answered "caption", so clicking
close, minimise or the 8D switch dragged the window instead of pressing it, and
it was self-locking, because once Windows believes a point is caption it stops
delivering `WM_MOUSEMOVE` there and nothing up there can ever become hot again.
The harness passed the whole time. `--real` goes through SendInput, so
hit-testing, hover, capture and double-click timing are all exercised the way a
hand exercises them.

It does not guess where anything is. The window writes down the rectangle it
painted for every control and the harness clicks those, so a layout change moves
the test with it and a control that stops being drawn fails as "not found"
rather than passing because the click landed on background. Two hooks in
`src/gui/Main.cpp` serve it and nothing else: `WM_APP+1` paints one frame
synchronously, `WM_APP+2` writes the hit list.

It covers all eight modes, the direction pill, all fifteen knobs -- turned by
hand as well as by the wheel, including the ones that are meant to be inert
(Speed under Static, Echo's Time and Feedback until Delay is raised) -- the
Amount slider clicked and dragged, double-click reset, Shift for fine
adjustment, every preset both as a chip and through the `+` list, the switches,
the keyboard shortcuts, the window buttons, and that the title bar drags the
window while the controls sitting in it do not.

What it does not cover: any DPI but this machine's 100%, and the About and
Account pages beyond checking that their tabs are clickable.

## The now-playing bar

It is drawn from three numbers the Windows media session reports: Position,
PlaybackStatus and LastUpdatedTime. **Position is a reading, not a clock** —
Chromium browsers take one and then stop updating it. Brave sat at 68.33 s for
three minutes of a playing track while LastUpdatedTime aged past 195 s.

So the reading is dated with LastUpdatedTime and run forward from there. Stamping
it with "now" on each poll, which is the obvious thing to do, restarts the
extrapolation twice a second and freezes the bar at whatever second the player
last bothered to publish.

`np_watch.exe [seconds]` prints those three columns once a second, which is how
that was found rather than guessed at.
