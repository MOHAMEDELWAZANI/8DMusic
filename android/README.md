# 8D Music — Android

The mobile build. **Stage 1**: the effect runs on Android against one bundled
track, so the DSP can be heard and measured on a device. It does not yet
process other apps — see [Stage 2](#stage-2-system-wide) below.

## The effect is not copied here

`app/src/main/cpp/CMakeLists.txt` compiles `../../../../../cpp/src/dsp`
directly. There is exactly one copy of the effect in this repository, shared
with the desktop C++ build, and CMake fails loudly if it cannot find it. A
change to the orbit, the reverb or the head model lands on all builds at once.

The compiler flags mirror `cpp/Makefile` — `-std=c++20 -O3 -ffast-math
-fno-math-errno` — so Android is not quietly running a differently-rounded
version of the same code.

## Audio path

```
  assets/boheme.mp3
      │  MediaCodec, once, at load
      ▼
  PCM ──▶ resample to the device rate ──▶ float, stereo, in native memory
                                              │
                                              │  AAudio callback
                                              ▼
                                    Processor::process ──▶ speakers
```

No ring buffer, no producer thread, no `AudioTrack` write loop. The whole file
is decoded up front, so the audio callback only ever does a `memcpy` and then
calls the DSP — the same shape as the desktop build, where the DSP runs inside
PipeWire's realtime callback.

AAudio, not Oboe: Oboe's value is device workarounds and API-26 fallback, and
`minSdk` here is 29. The NDK's own API costs one less dependency.

| | Desktop (`cpp/`) | Android |
| --- | --- | --- |
| Output | two `pw_stream`s | one `AAudioStream` |
| Callback | PipeWire realtime | AAudio realtime |
| DSP | `eightd::Processor` | the same file |
| Parameters | snapshot by value | snapshot by value, published under `try_lock` |

## Build

```bash
cd android && ./gradlew :app:assembleDebug
```

Or open `android/` in Android Studio. Needs NDK 29, CMake 3.22.1 and
platform 35; `local.properties` points at the SDK and is not committed.

The bundled track is **not** in the repository — it is a real song. Drop any
`.mp3` at `app/src/main/assets/boheme.mp3` before building.

## Stage 2, system-wide

Capturing other apps needs `AudioPlaybackCapture` (hence `minSdk 29`), plus a
maximum-priority muting `AudioEffect` on each captured session so the original
stream is silenced rather than heard alongside the processed one. Apps that set
`ALLOW_CAPTURE_BY_NONE` — Spotify, Chrome, SoundCloud — cannot be processed at
all, so the per-app screen must show them as unavailable rather than fail
quietly. None of that exists yet.
