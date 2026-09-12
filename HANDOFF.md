# Where the project stands

Written 2026-09-09, at the point where the Android build works on real hardware
and the Windows build has never been compiled.

## The one rule everything else follows

**The effect exists once**, in `cpp/src/dsp/`. Every platform compiles those
files in place; nobody copies them.

| Build | How it reaches the DSP |
| --- | --- |
| `cpp/` | direct |
| `android/` | `CMakeLists.txt` → `../../../../../cpp/src/dsp` |
| `windows/` | `CMakeLists.txt` → `../cpp/src/dsp` |

Both native build files **hard-fail** if they cannot find the DSP. Keep it that
way: the moment one platform gets its own copy, "identical across builds" stops
being true and nobody notices for months.

Compiler flags are matched deliberately — `-O3 -ffast-math -fno-math-errno` on
GCC/Clang, `/O2 /fp:fast` on MSVC.

## Proving they are the same effect

`cpp/tests/dsp_probe.cpp` renders a deterministic signal (220 Hz, 5 s, eight
modes) to raw f32. Android has the identical probe behind the parity button.
`cpp/tests/compare_f32.py` compares two directories of dumps.

Measured Android vs desktop C++, on device:

```
correlation 1.000000 across all eight modes
largest sample difference 0.000006
```

That is one source file compiled twice, not two implementations agreeing — which
is why it lands three orders of magnitude tighter than a reimplementation would.

Do this again for Windows once it builds. It is the only honest way to claim the
three builds sound the same.

## Android — works, tested on hardware

Redmi Note 12, Android 14, arm64. Built, installed, and heard.

**Working:** local player, direct capture, system-wide via Shizuku, all ten
presets, four control tabs, light/dark, orbit driven by DSP telemetry.

**How system-wide actually works** — this took a while to get right:

1. Shizuku runs `pm grant com.eightd.music android.permission.DUMP`
2. That permission is an **install permission**; it persists across reboots and
   app updates. Verified surviving an update on the test device.
3. With DUMP we read `dumpsys media.audio_flinger` ourselves and parse its
   session table — no Shizuku involved at runtime
4. Each other app's session gets a `DynamicsProcessing` effect at
   `Int.MAX_VALUE` priority with −200 dB input gain, silencing the original so
   only the spatialised copy is heard
5. An `AudioPlaybackCallback` fires the rescan instantly when a new track starts

**Therefore: Shizuku is needed once, ever — not once per boot.** The setup
screen does not yet say this. It should. Unverified: behaviour after a full
reboot, and after uninstalling Shizuku entirely.

**Known gaps, in the user's words:** the interface is not the intended design
(controls scattered across too many places), and there is no way to build
playlists. `8D Audio Effect Tool UI/plan/8D Music App.dc.html` is the real
design — 17 screens. Only TURN 1 exists, roughly. All of TURN 2 (sign-in, three
intro pages, four guides, About, the capture-refused screen) is unbuilt.

**Apps that block capture cannot be processed at all** — Spotify, Chrome, and
probably Brave set `ALLOW_CAPTURE_BY_NONE`. No permission changes this. Only a
patched APK would, which is not a route to recommend.

## Windows — written, never compiled

**Start here.** No Windows machine, MSVC or Windows SDK existed on the Linux
host, so not one line has been through a compiler. Expect build errors.

```
cd windows
cmake -B build -A x64
cmake --build build --config Release
```

Two artefacts: `8DMusicAPO.dll` (the effect, loaded into `audiodg.exe`) and
`8DMusic.exe` (the control window).

Three things worth knowing before debugging:

* **`audiodg` runs at a different integrity level.** The shared block is created
  with an explicit low-integrity SDDL label. Without it the APO loads, runs, and
  silently never sees a setting — indistinguishable from "the GUI does nothing".
* **The APO has a heartbeat.** It bumps a counter every buffer; the window shows
  `PROCESSING` or `WAITING` from that rather than guessing.
* **Building is not installing.** Registering an APO against an endpoint needs
  its CLSID written into that device's `FxProperties` and audio restarted.
  Equalizer APO ships an entire installer utility for this step and the property
  keys vary by Windows version. Read theirs before writing ours.

**Alternative worth weighing:** a standalone exe using WASAPI loopback — user
sets a virtual cable as default output, we capture its loopback, process, render
to the real device. That is exactly the Linux architecture, roughly 300 lines,
no admin, no registry, no reboot, testable immediately. Cost: the user installs
a virtual cable.

## Immediate next steps

1. Get `windows/` to compile. Nothing else there can be trusted until it does.
2. Run the parity probe on Windows; compare against the desktop reference.
3. Decide APO versus loopback for the shipping path.
4. On Android: reword the Shizuku screen to say "once, ever".
5. Then the interface rebuild, from the design document rather than by patching.

## Things not to rediscover

* `AAudioStream_requestStart` returns an **error** on an already-started stream.
  Reading that as "not playing" broke the play button and the clock.
* Decoding a whole track before playing costs seconds; the Android build streams
  into the engine and starts after ~0.5 s.
* Muting every session also silences the launcher and system UI. There is a
  package blocklist for this.
* Capture must `excludeUid(Process.myUid())` or the app captures itself and
  feeds back.
