# Python vs C++ — measured

Both builds do the same job with the same effect, so the only interesting
question is what each one costs. Everything below was measured on this machine,
with the harness in `bench/`; nothing is estimated.

## How it was measured

Runs are **interleaved** — within each repetition both versions are exercised
back to back, and the order alternates between repetitions — so a machine that
warms up, throttles or gets busy affects both columns equally. Figures are
**medians** with the observed spread, because a single timing on a laptop means
very little.

The machine was not a quiet benchmark rig, and that is worth stating plainly:

| | |
| --- | --- |
| CPU | Intel i7-8650U, 4 cores / 8 threads |
| Governor | `powersave`, running at ~67 % of max clock |
| Load average | ~1.2 during the runs |
| Other load | a RustDesk remote session was active throughout |

The spreads show the noise this introduced. It is small relative to every gap
reported here, and interleaving cancels drift, but absolute numbers on an idle
desktop would be somewhat better for both sides.

Reproduce with:

```bash
cd bench
REPS=5 python3 dsp_bench.py        # the effect itself
REPS=3 python3 audio_bench.py      # the whole audio path, engines headless
REPS=5 python3 latency_bench.py    # delay added by the effect
REPS=3 python3 gui_bench.py        # the interface
```

## 1. The effect itself

Five seconds of identical audio through each engine, block by block, measured as
percentage of real time — lower is better, and anything at or above 100 % cannot
keep up.

| Movement mode | Python | C++ | Speed-up |
| --- | ---: | ---: | ---: |
| Circular orbit | 12.84 % | 0.440 % | 29.2× |
| Ping-pong | 12.84 % | 0.430 % | 29.9× |
| Pendulum | 12.83 % | 0.440 % | 29.2× |
| Linear sweep | 12.96 % | 0.450 % | 28.8× |
| Figure eight | 12.97 % | 0.430 % | 30.2× |
| Spiral | 12.99 % | 0.430 % | 30.2× |
| Static position | 13.00 % | 0.450 % | 28.9× |
| Slowed & sad | 14.36 % | 0.520 % | 27.6× |
| Old radio | 15.73 % | 0.670 % | 23.5× |

**Median 29.2×** (range 23.5×–30.2×). Spreads across five repetitions were tight
— circular, for instance, ran 12.63–13.26 % in Python and 0.430–0.450 % in C++.

The two builds produce numerically identical audio (correlation 1.0000, largest
sample difference 0.0006), so this is a like-for-like comparison, not a
comparison of two different effects.

## 2. The whole audio path

Each engine run headless against a scratch sink with a tone flowing through it.
CPU is counted over the **entire process tree**, which matters: the Python build
carries its audio through `pw-record` and `pw-play` subprocesses, and charging
it only for the parent would flatter it.

| | Python | C++ | |
| --- | ---: | ---: | ---: |
| CPU (process tree) | 23.6 % | 2.6 % | **9.0× less** |
| Memory (process tree) | 134.5 MB | 11.8 MB | **11.4× less** |
| Processes | 4 | 1 | |
| Underruns in 8 s | 0 | 0 | both clean |

Routing side effects were neutralised so the machine's real audio was never
touched — but Python's graph housekeeping still ran its `pw-dump` subprocess, so
that polling cost is included rather than quietly excused.

## 3. Latency added by the effect

Measured by capturing **both sides at once** — the virtual sink's monitor (what
goes in) and the scratch output (what comes out) — and taking the gap between
the signal reaching one and reaching the other. Capturing both sides is what
makes this trustworthy: the source's own buffering appears in both streams and
cancels.

| | Python | C++ |
| --- | ---: | ---: |
| Added latency (median) | 21.7 ms | 3.5 ms |
| Spread | 17.8–27.3 ms | 1.8–7.1 ms |
| Valid samples | 5 of 5 | 4 of 5 |

**The C++ path adds about 18 ms less.** Two caveats, both real: the C++ figure
sits close to the method's own resolution (~1.3 ms chunks plus capture jitter),
and one C++ repetition produced a slightly negative result, which was discarded
— that is itself a sign the delay is small enough to be hard to measure this
way. The 18 ms *difference* is the robust number; treat the 3.5 ms as "a few
milliseconds", not a precise value.

Each version ran at its own default latency setting, which is what a user
actually gets: Python asks `pw-record`/`pw-play` for 25 ms each and adds two
pipe hops; C++ runs a 512-frame quantum in process with no pipes.

## 4. The interface

Window open, engine stopped, pointer parked away — how an app spends most of its
life.

| | Python | C++ | |
| --- | ---: | ---: | ---: |
| Time to window | 1601 ms | 91 ms | **17.6× faster** |
| CPU sitting idle | 20.5 % | 0.5 % | **41× less** |
| Memory | 121.2 MB | 37.0 MB | **3.3× less** |

The idle figure is the one worth dwelling on. It is not measurement noise: the
Tk build calls `stage.refresh(...)` unconditionally every 33 ms whether or not
anything changed, so it repaints thirty times a second forever. The C++ build
draws **zero frames** when nothing has changed, and repaints only the orbit
strip when the source is moving.

## 5. Footprint

| | Python | C++ |
| --- | ---: | ---: |
| What you install | 271 MB `.venv` + 7.7 MB source | **443 KB binary** |
| Runtime dependencies | NumPy 2.5.1, SciPy 1.18.0, Tk | 45 shared libraries, all already on the system |
| Source | 4 348 lines | 3 969 lines |
| Build | — | 17.5 s from clean |

## What actually explains the gap

Little of this is "C++ is faster than Python". Most of it is architecture, and
the language mainly made the architecture affordable:

- **No helper processes.** Python spawns `pw-record` and `pw-play` and pushes
  every sample through kernel pipes. C++ runs two native `pw_stream`s in one
  process, so audio never leaves the address space.
- **No polling.** Python shells out to `pw-dump` and parses ~1 MB of JSON every
  couple of seconds to answer "what devices exist". C++ reads the PipeWire
  registry, which pushes changes to it.
- **No unconditional repaint.** The interface draws when something changed.
- The DSP itself is the one place where the language dominates: the same
  algorithm, allocation-free and in the realtime callback, is ~29× faster.

## Where Python still wins

Worth saying, since a benchmark that only flatters one side is not much use:

- It runs anywhere Python and PipeWire's command-line tools exist — no compiler,
  no development headers.
- 4 348 lines of Python were quicker to write and are quicker to change than the
  equivalent C++.
- The effect is *identical*. If 20 % of one core is not a problem on the machine
  in question, the Python build is not doing anything wrong.
