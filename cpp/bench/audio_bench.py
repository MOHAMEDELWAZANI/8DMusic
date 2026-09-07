#!/usr/bin/env python3
"""The full audio path: virtual sink -> DSP -> output device.

Both engines run headless against a scratch sink, with a tone pushed into their
virtual sink, so the machine's real audio is untouched.  CPU is counted over the
whole process tree, which matters: the Python build carries its audio through
pw-record and pw-play subprocesses.
"""
import subprocess, sys, time, os, threading, atexit, statistics, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, "/home/mohamedwazane/Projects/8DMUSIC/python")
import numpy as np
import scratchsink
from proctree import tree_ticks, tree_rss_kb, tree_size, HZ

HERE = os.path.dirname(os.path.abspath(__file__))
CPP = "/home/mohamedwazane/Projects/8DMUSIC/cpp"
PY = "/home/mohamedwazane/Projects/8DMUSIC/python"
VENV = f"{PY}/.venv/bin/python"
DEST = "eight_d_bench_dest"
RATE = 48000
SECONDS = int(os.environ.get("SECONDS", "8"))
REPS = int(os.environ.get("REPS", "3"))

procs = []
def spawn(*a, **k):
    p = subprocess.Popen(*a, **k); procs.append(p); return p

@atexit.register
def cleanup():
    for p in procs:
        try: p.terminate(); p.wait(timeout=2)
        except Exception:
            try: p.kill()
            except Exception: pass


def feed_tone(stop):
    """Push a steady tone into whichever virtual sink is currently up."""
    src = spawn(["pw-play", "--target", "eight_d_music_sink", "--rate", str(RATE),
                 "--channels", "2", "--format", "f32", "--latency", "20ms",
                 "--raw", "-"], stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
    t = np.arange(RATE * 4) / RATE
    tone = (0.3 * np.sin(2 * np.pi * 330 * t)).astype(np.float32)
    block = np.stack([tone, tone], 1).ravel().tobytes()
    def pump():
        try:
            while not stop.is_set():
                src.stdin.write(block); src.stdin.flush()
        except Exception:
            pass
    threading.Thread(target=pump, daemon=True).start()
    return src

def measure(cmd, label):
    app = spawn(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    time.sleep(2.5)                       # let the sink appear and settle
    stop = threading.Event()
    src = feed_tone(stop)
    time.sleep(2.0)                       # let audio actually flow

    c0 = tree_ticks(app.pid); t0 = time.time()
    time.sleep(SECONDS)
    c1 = tree_ticks(app.pid); t1 = time.time()
    cpu = (c1 - c0) / HZ / (t1 - t0) * 100.0
    rss = tree_rss_kb(app.pid) / 1024.0
    procn = tree_size(app.pid)

    stop.set()
    try: src.kill()
    except Exception: pass
    out = ""
    try:
        app.wait(timeout=SECONDS + 12); out = app.stdout.read()
    except Exception:
        app.kill()
        try: out = app.stdout.read()
        except Exception: pass
    under = 0
    for line in (out or "").splitlines():
        if "underruns=" in line:
            try: under = max(under, int(line.split("underruns=")[1].split()[0]))
            except Exception: pass
    time.sleep(1.5)
    return {"cpu": cpu, "rss": rss, "procs": procn, "underruns": under}

def main():
    scratchsink.create(DEST)
    runs = {"cpp": [], "py": []}
    total = SECONDS + 8
    for rep in range(REPS):
        order = ["cpp", "py"] if rep % 2 == 0 else ["py", "cpp"]
        for which in order:
            if which == "cpp":
                cmd = [f"{CPP}/tests/engine_probe_bin", DEST, str(total)]
            else:
                cmd = [VENV, f"{HERE}/py_engine_probe.py", DEST, str(total), "25"]
            r = measure(cmd, which)
            runs[which].append(r)
            print(f"  rep {rep+1} {which:3s}: cpu={r['cpu']:5.1f}%  rss={r['rss']:5.1f}MB  "
                  f"procs={r['procs']}  underruns={r['underruns']}", file=sys.stderr)
    scratchsink.destroy(DEST)

    print(f"\n{'metric':26s} {'python':>16s} {'c++':>16s}")
    print("-" * 62)
    out = {}
    for key, name, unit in (("cpu", "CPU, whole tree", "%"),
                            ("rss", "memory, whole tree", "MB"),
                            ("procs", "processes", ""),
                            ("underruns", "underruns", "")):
        pv = [r[key] for r in runs["py"]]; cv = [r[key] for r in runs["cpp"]]
        pm, cm = statistics.median(pv), statistics.median(cv)
        out[key] = {"python": pm, "cpp": cm, "python_all": pv, "cpp_all": cv}
        print(f"{name:26s} {pm:10.1f} {unit:<4s} {cm:10.1f} {unit:<4s}")
    print("-" * 62)
    print(f"CPU ratio: {out['cpu']['python']/max(out['cpu']['cpp'],1e-9):.1f}x")
    with open("/tmp/bench_audio.json", "w") as f:
        json.dump(out, f, indent=2)

if __name__ == "__main__":
    main()
