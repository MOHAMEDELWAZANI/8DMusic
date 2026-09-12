# Measured

Nothing here is estimated. Where a figure could not be reproduced on the
hardware to hand, it says so rather than being quietly dropped.

Two different questions are measured, and they matter in different ways:

1. **Do the builds produce the same audio?** This is the project's central
   claim, it is checked by rendering the same probe on each platform, and it is
   reproducible today.
2. **What does the effect cost?** Measured on the Linux build, on a machine that
   was explicitly not a quiet benchmark rig.

---

## 1. Parity — the builds produce the same audio

The effect exists once, in `cpp/src/dsp/`. Every platform compiles those files
in place, so "identical everywhere" ought to be true by construction. It is
checked anyway, because a claim nobody tests is a claim that quietly stops being
true.

`cpp/tests/dsp_probe.cpp` renders a deterministic signal — a 220 Hz tone, five
seconds, 512-frame blocks, every movement mode — straight to raw `f32`.
`cpp/tests/compare_f32.py` compares two sets of those dumps.

| Candidate | Compiler | Arch | Correlation | Largest sample difference |
| --- | --- | --- | ---: | ---: |
| Linux | GCC 14 | x86-64 | reference | — |
| Windows | MSVC 2022 | x86-64 | **1.000000** | **0.000006** |
| Android | Clang (NDK 29) | x86-64 | **1.000000** | **0.000006** |

Compiler flags are matched deliberately — `-O3 -ffast-math -fno-math-errno` on
GCC and Clang, `/O2 /fp:fast` on MSVC — because parity to six decimal places
only means anything if the arithmetic is allowed to be the same.

**Caveat worth stating:** the Android figure was measured on the x86-64
emulator, not on an ARM device. The `arm64-v8a` library is built from the same
source with the same flags and has been heard working on hardware, but the probe
has not been run there. Until it is, ARM parity is expected rather than
measured.

Reproduce:

```bash
g++ -std=c++20 -O3 -ffast-math -fno-math-errno \
    -o dsp_probe cpp/tests/dsp_probe.cpp cpp/src/dsp/Processor.cpp -Icpp/src/dsp

mkdir -p ref && for m in circular pingpong pendulum figure8 spiral static radio slowed; do
    ./dsp_probe $m ref/$m.f32
done

python3 cpp/tests/compare_f32.py ref CANDIDATE_DIR
```

Windows has `windows/parity.ps1`, which builds a GCC reference and compares it
against the MSVC one. Android renders the same probe from inside the app and the
dumps are pulled with `adb`.

---

## 2. What the effect costs — Linux

### The machine

Not a quiet benchmark rig, and that is worth stating plainly:

| | |
| --- | --- |
| CPU | Intel i7-8650U, 4 cores / 8 threads |
| Governor | `powersave`, running at ~67 % of max clock |
| Load average | ~1.2 during the runs |
| Other load | a RustDesk remote session was active throughout |

Figures are medians with the observed spread. Absolute numbers on an idle
desktop would be somewhat better.

### The DSP itself

Five seconds of audio through the engine, block by block, as a percentage of
real time. Lower is better; at or above 100 % it cannot keep up.

| Movement mode | Share of real time |
| --- | ---: |
| Circular orbit | 0.44 % |
| Ping-pong | 0.43 % |
| Pendulum | 0.44 % |
| Linear sweep | 0.45 % |
| Figure eight | 0.43 % |
| Spiral | 0.43 % |
| Static position | 0.45 % |
| Slowed & sad | 0.52 % |
| Old radio | 0.67 % |

Spreads across five repetitions were tight — circular ran 0.430–0.450 %.

Old radio is the most expensive mode by a clear margin, which is unsurprising:
it is the only one that adds band-limiting, saturation, tape wow and a gated
hiss on top of the orbit.

### The whole audio path

Run headless against a scratch sink with a tone flowing through it. CPU is
counted over the entire process tree.

| | |
| --- | ---: |
| CPU (process tree) | 2.6 % |
| Memory (process tree) | 11.8 MB |
| Processes | 1 |
| Underruns in 8 s | 0 |

One process is the point. Audio never leaves the address space: two native
`pw_stream`s with the DSP inside PipeWire's realtime callback, no helper
processes and no kernel pipes.

### Latency added by the effect

Measured by capturing **both sides at once** — the virtual sink's monitor and
the scratch output — and taking the gap between the signal reaching one and the
other. Capturing both sides is what makes it trustworthy: the source's own
buffering appears in both streams and cancels.

| | |
| --- | ---: |
| Added latency (median) | ~3.5 ms |
| Spread | 1.8–7.1 ms |

Treat this as "a few milliseconds", not a precise value. It sits close to the
method's own resolution (~1.3 ms chunks plus capture jitter), and one repetition
produced a slightly negative result — itself a sign the delay is small enough to
be hard to measure this way.

The engine runs a 512-frame quantum in process, which is what a user actually
gets.

### The interface

Window open, engine stopped, pointer parked away — how an app spends most of its
life.

| | |
| --- | ---: |
| Time to window | 91 ms |
| CPU sitting idle | 0.5 % |
| Memory | 37.0 MB |

The idle figure is deliberate, not luck: the interface draws **zero frames**
when nothing has changed, and repaints only the orbit strip while the source is
moving.

### Footprint

| | |
| --- | ---: |
| Binary | 448 KB |
| Runtime dependencies | 45 shared libraries, all already on the system |
| Source | 3 969 lines |
| Build from clean | 17.5 s |

---

## 3. Android, on device

Measured with the parity probe running inside the app, which reports its own
timing as it renders. Redmi Note 12 (Android 14) for the audio path; the
emulator for the probe figures below.

| | |
| --- | ---: |
| DSP, share of real time | 0.57–1.07 % |
| Worst single block | 0.114–0.208 ms (budget 10.7 ms) |
| Output stream | 48 kHz / 256-frame burst, or 44.1 kHz / 882 depending on route |
| Underruns during playback | 0 |

The device negotiates its own rate — 44.1 kHz on one route, 48 kHz on another —
and the engine takes whatever it is given, because `Processor::init` is handed
the real rate rather than assuming one.

---

## A note on this document's history

Earlier versions of this document compared two implementations of the project: a
reference build and the native rewrite. Only the native one remains, and the
comparison harness went with the other — every script in it drove both sides.

The figures above are that build's own measurements from those runs. They stand
as measurements, but they are not reproducible from this tree as it is; the
harness is in the git history if it is ever wanted back.

The parity check in section 1 is unaffected and runs today.
