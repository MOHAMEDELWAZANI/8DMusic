#!/usr/bin/env python3
"""DSP throughput: the same audio through both engines.

Runs are interleaved C++/Python within each repetition, so a machine that
speeds up or slows down over the run affects both sides equally.  The reported
figure is the median across repetitions, with the spread alongside it, because
a single timing on a loaded laptop means very little.
"""
import subprocess, sys, time, statistics, json, os

PY_ROOT = "/home/mohamedwazane/Projects/8DMUSIC/python"
CPP_PROBE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "dsp_probe")
sys.path.insert(0, PY_ROOT)

import numpy as np
from eight_d.dsp import EightDProcessor, Params

RATE, BLOCK, SECONDS = 48000, 512, 5
MODES = ["circular", "pingpong", "pendulum", "linear", "figure8", "spiral", "static"]
CHARACTERS = ["slowed", "radio"]
REPS = int(os.environ.get("REPS", "5"))

def signal():
    n = RATE * SECONDS
    t = np.arange(n) / RATE
    s = (0.25 * np.sin(2 * np.pi * 220 * t)).astype(np.float32)
    return np.stack([s, s * 0.9], 1).astype(np.float32)

X = signal()

def run_cpp(mode):
    r = subprocess.run([CPP_PROBE, mode, "/tmp/bench_cpp.f32"],
                       capture_output=True, text=True)
    # "circular  rt= 0.60%  worstblk=..."
    return float(r.stderr.split("rt=")[1].split("%")[0])

def run_python(mode):
    kw = dict(mode=mode if mode in MODES else "circular", speed=0.25, radius=1.2,
              depth=0.85, smoothness=0.35, width=1.0, delay_mix=0.25,
              delay_time=0.28, delay_feedback=0.35, reverb_mix=0.30,
              reverb_size=0.6, reverb_damp=0.45, output_gain=0.9,
              pause_when_silent=False)
    if mode == "slowed":
        kw.update(character="slowed", character_amount=0.85)
    elif mode == "radio":
        kw.update(character="radio", character_amount=1.0)
    p = EightDProcessor(RATE, BLOCK)
    params = Params(**kw)
    n = X.shape[0]
    t0 = time.perf_counter()
    blocks = 0
    for i in range(0, n - BLOCK + 1, BLOCK):
        p.process(X[i:i + BLOCK], params)
        blocks += 1
    wall = time.perf_counter() - t0
    return wall / (blocks * BLOCK / RATE) * 100.0

def main():
    cases = MODES + CHARACTERS
    results = {c: {"cpp": [], "py": []} for c in cases}
    for rep in range(REPS):
        for case in cases:
            # interleaved, C++ first on even reps and Python first on odd ones
            if rep % 2 == 0:
                results[case]["cpp"].append(run_cpp(case))
                results[case]["py"].append(run_python(case))
            else:
                results[case]["py"].append(run_python(case))
                results[case]["cpp"].append(run_cpp(case))
        print(f"  rep {rep + 1}/{REPS} done", file=sys.stderr)

    print(f"{'case':10s} {'python % rt':>18s} {'c++ % rt':>18s} {'speed-up':>9s}")
    print("-" * 60)
    speedups = []
    out = {}
    for case in cases:
        py = results[case]["py"]; cpp = results[case]["cpp"]
        pym, cppm = statistics.median(py), statistics.median(cpp)
        su = pym / cppm
        speedups.append(su)
        out[case] = {"python_pct": pym, "cpp_pct": cppm, "speedup": su,
                     "python_all": py, "cpp_all": cpp}
        print(f"{case:10s} {pym:8.2f} [{min(py):5.2f}-{max(py):5.2f}] "
              f"{cppm:8.3f} [{min(cpp):5.3f}-{max(cpp):5.3f}] {su:8.1f}x")
    print("-" * 60)
    print(f"median speed-up across cases: {statistics.median(speedups):.1f}x "
          f"(range {min(speedups):.1f}x - {max(speedups):.1f}x)")
    with open("/tmp/bench_dsp.json", "w") as f:
        json.dump(out, f, indent=2)

if __name__ == "__main__":
    main()
