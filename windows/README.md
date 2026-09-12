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

**This is the hard part, and it is not solved by building.** Registering a
custom APO against an audio endpoint takes more than `regsvr32`:

1. `regsvr32 8DMusicAPO.dll` — registers the COM class. Needs an elevated prompt.
2. The endpoint must then be told to load it, by writing the CLSID into that
   device's `FxProperties` under
   `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}`.
3. Restart the audio service (or reboot) — `audiodg` only reads this at start.

**Equalizer APO ships a whole device-installer utility for step 2**, and it is
worth reading its approach before writing our own; the exact property keys vary
by Windows version and getting them wrong silently does nothing.

Until that installer exists, the DLL builds and registers but will not be in the
audio path, and `8DMusic.exe` will honestly report `WAITING` rather than
pretend otherwise.

## Status

Written, not yet compiled. There is no Windows machine, MSVC or Windows SDK on
the development host, so unlike the Android build — which was tested on real
hardware — none of this has been through a compiler. Expect build errors on the
first pass.

## The alternative worth considering

A standalone `.exe` using WASAPI loopback needs no APO, no registry, no admin
and no reboot: set a virtual cable as the default output, capture its loopback,
process, and render to the real device. That is *exactly* the Linux
architecture, it is perhaps 300 lines, and it can be tested the moment it
compiles.

The cost is that the user installs a virtual audio cable. The benefit is that
it will actually work this week.
