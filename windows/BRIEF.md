# Brief: the Windows build

## What you are joining, not starting

This is an existing project with a working Linux build and a working Android
build. **Do not write a new DSP.** The effect exists once, in `cpp/src/dsp/`,
and every platform compiles those files in place:

| Build | Reaches the DSP by |
| --- | --- |
| `cpp/` | direct |
| `android/` | CMake → `../../../../../cpp/src/dsp` |
| `windows/` | CMake → `../cpp/src/dsp` |

Both native build files hard-fail if the DSP is missing. Keep that. The moment
Windows gets its own copy of the effect, "identical across builds" becomes a
lie that survives for months before anyone notices.

`windows/` already contains roughly 940 lines you should build on rather than
replace:

* `src/apo/EightDApo.{h,cpp}` — the APO, implementing `IAudioProcessingObject`,
  `IAudioProcessingObjectConfiguration`, `IAudioProcessingObjectRT`,
  `IAudioSystemEffects`
* `src/apo/DllMain.cpp` — COM class factory and self-registration
* `src/shared/SharedState.{h,cpp}` — the GUI↔APO channel
* `src/gui/Main.cpp` — a minimal control window
* `CMakeLists.txt`, `README.md`

**None of it has ever been compiled.** There was no Windows machine, MSVC or
Windows SDK on the host it was written on. Treat the first build as a debugging
session. Fix what is wrong; do not restart from nothing.

## The product requirement

8D Music must behave like a native Windows system-wide audio processor while
remaining **one application**.

The user must never be told to install Equalizer APO, Virtual Audio Cable or
VoiceMeeter, and must never be told to change their output device. Their
selected output stays selected. Our own installer is the only install.

```
Spotify / YouTube / Games / Discord / anything
        ↓
Windows Audio Engine
        ↓
8DMusic APO  ──►  8D DSP Core (cpp/src/dsp)
        ↓
Normal output device → headphones
```

## Feature parity with the Linux build

The Windows GUI should reach the same bar as `cpp/src/ui/` — read it. That
means, not as a wish list but as the definition of done:

* Orbit visualiser driven by the DSP's own telemetry (`Processor::angle()`,
  `distance()`, `peakL/R()`), not a UI-side clock
* All eight movement modes, and direction
* Speed, radius, depth, smoothness, manual position, pause-when-silent
* Character (clean / slowed / radio) and amount
* Stereo width, delay mix/time/feedback, reverb mix/room/damping
* Output volume, bypass
* All ten presets — they are plain data in `cpp/src/dsp/Params.h`
* Light and dark themes
* Output device selection, and a live "is it actually processing" indicator

Study `cpp/src/ui/` for how the Linux build lays this out before designing
anything new. The two should read as the same product.

## Real-time rules

`APOProcess` runs on an audio thread inside `audiodg.exe`. No allocation, no
locks, no file, registry or network I/O, no logging. `eightd::Processor` sizes
every buffer in `LockForProcess` and allocates nothing afterwards; keep any new
code to the same standard.

Parameters cross a process boundary, so they cannot be a pointer.
`SharedState.h` uses a seqlock with **bounded** retries: the audio thread never
waits on the GUI. A settings change arriving one buffer late is inaudible; a
late buffer is not.

`audiodg` runs at a different integrity level, so the mapping is created with an
explicit low-integrity SDDL label. Without it the APO loads, runs, and silently
never sees a setting — which is indistinguishable from "the GUI does nothing".
If parameters appear not to work, check this first.

## Enable / disable

The APO stays installed and in the path either way.

* **Off** — passthrough, as close to zero cost as possible
* **On** — the full chain

`Params::enabled` already short-circuits inside `Processor::process`.

## The installation problem — the real work

This is the hardest part of the project and the most likely thing to fail.
Do not assume; verify on the target Windows versions.

Questions that must be answered with evidence, not recollection:

1. **Which effect slot?** Stream (SFX), mode (MFX) or endpoint (EFX)? Each has
   different lifetime and format rules. Which one suits an effect that must
   apply to everything mixed together?
2. **How is an APO associated with an endpoint on current Windows 10 and 11?**
   The endpoint's `FxProperties` under
   `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render\{endpoint}`
   — which exact `PKEY_FX_*` properties, and do they differ by Windows build?
3. **Can an unsigned APO load at all?** Equalizer APO demonstrates that
   system-wide APO processing is achievable, so read how its installer does it.
   Establish what signing, if any, current Windows requires.
4. **What restart is needed?** `audiodg` reads this at start. Can the audio
   service be restarted without a reboot?
5. **What happens on endpoint change** — user switches speakers → headphones,
   plugs in USB, connects Bluetooth? Registration is per-endpoint.
6. **What does the APO receive?** The negotiated format may be 5.1, 24-bit, or
   96 kHz. The DSP is stereo float. Decide explicitly: convert, or decline the
   format and pass through. Declining silently is not acceptable.

Use Equalizer APO as a **reference**, never a dependency. Understand the current
model rather than copying an old architecture.

## Safety — non-negotiable

A faulty APO takes down `audiodg` and the user loses all system audio, with no
obvious way to connect that to our app.

* The uninstaller must fully remove the endpoint registration and must work
  when audio is already broken.
* Ship a documented manual recovery (registry keys to delete, service to
  restart) in case the uninstaller cannot run.
* The APO must fail safe: any unexpected state passes audio through unmodified
  rather than producing silence or noise.

## Proving it is the same effect

The project does not assert that its builds match; it measures it.

`cpp/tests/dsp_probe.cpp` renders a deterministic signal (220 Hz, 5 s, eight
modes) to raw f32. `cpp/tests/compare_f32.py` compares two sets of dumps.
Android measures **correlation 1.000000, largest sample difference 0.000006**
against the desktop C++ build.

Build the same probe for Windows and hit the same bar. Compiler flags are
matched deliberately — `-O3 -ffast-math -fno-math-errno` on GCC/Clang,
`/O2 /fp:fast` on MSVC.

## Deliverables

1. `8DMusicAPO.dll` — the effect, loading into the Windows audio engine
2. `8DMusic.exe` — the control application, at parity with the Linux UI
3. An installer that registers and configures the APO with no user steps beyond
   running it, and an uninstaller that reliably reverses it
4. A Windows parity probe, with results
5. Documentation of what was learned about current APO registration, so the
   next person does not repeat the research

## User experience, as the test

1. Download 8D Music
2. Run the installer
3. Open 8D Music
4. Turn 8D on
5. Play Spotify, YouTube, a game, Discord
6. Hear it through the output device they already had selected

If any step needs a third-party install or a device change, it is not done.
