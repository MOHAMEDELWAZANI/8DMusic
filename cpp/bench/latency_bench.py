#!/usr/bin/env python3
"""Latency added by the effect itself.

The signal is captured on both sides of the engine at once -- the virtual
sink's monitor (what goes in) and the scratch output (what comes out) -- and
the delay is the gap between the noise reaching one and reaching the other.
Capturing both sides is what makes this trustworthy: the source's own buffering
lands in both streams equally and cancels, which a single-ended "when did I
write it" measurement cannot do.
"""
import subprocess, sys, time, os, threading, atexit, statistics, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import numpy as np
import scratchsink

HERE = os.path.dirname(os.path.abspath(__file__))
CPP = "/home/mohamedwazane/Projects/8DMUSIC/cpp"
VENV = "/home/mohamedwazane/Projects/8DMUSIC/python/.venv/bin/python"
DEST = "eight_d_lat_dest"
SINK = "eight_d_music_sink"
RATE = 48000
REPS = int(os.environ.get("REPS", "5"))
THRESH = 0.02

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
    scratchsink._reap_pw_cli()

def capture(target):
    """pw-record on a sink's monitor, with a wall time per chunk."""
    p = spawn(["pw-record", "--target", target, "-P", "{ stream.capture.sink=true }",
               "--rate", str(RATE), "--channels", "2", "--format", "f32",
               "--latency", "10ms", "--raw", "-"],
              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    state = {"stamps": [], "chunks": []}
    def drain():
        frames = 0
        while True:
            b = p.stdout.read(512)            # 64 frames -> ~1.3 ms resolution
            if not b: break
            state["stamps"].append((time.perf_counter(), frames))
            state["chunks"].append(b)
            frames += len(b) // 8
    threading.Thread(target=drain, daemon=True).start()
    return p, state

def onset_time(state):
    """Wall time at which the signal first appears in this capture."""
    data = b"".join(state["chunks"])
    a = np.frombuffer(data[:len(data) // 8 * 8], dtype=np.float32).reshape(-1, 2)
    if a.size == 0: return None, 0.0
    amp = np.abs(a).max(axis=1)
    hits = np.nonzero(amp > THRESH)[0]
    if hits.size == 0: return None, float(amp.max())
    hit = int(hits[0])
    t = None
    for ts, before in state["stamps"]:
        if before <= hit: t = ts
        else: break
    if t is None: return None, float(amp.max())
    # refine inside the chunk the onset landed in
    for ts, before in state["stamps"]:
        if before <= hit: t, base = ts, before
        else: break
    return t + (hit - base) / RATE, float(amp.max())

def one_shot():
    inp, in_state = capture(SINK)
    out, out_state = capture(DEST)
    time.sleep(1.2)                            # both captures settled on silence

    src = spawn(["pw-play", "--target", SINK, "--rate", str(RATE), "--channels", "2",
                 "--format", "f32", "--latency", "10ms", "--raw", "-"],
                stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
    rng = np.random.default_rng(7)
    n = int(RATE * 0.020)
    noise = (rng.standard_normal((n, 2)) * 0.35).astype(np.float32).ravel().tobytes()
    stop = threading.Event()
    def pump():
        try:
            while not stop.is_set():
                src.stdin.write(noise); src.stdin.flush()
        except Exception:
            pass
    threading.Thread(target=pump, daemon=True).start()

    time.sleep(2.0)
    stop.set(); time.sleep(0.15)
    for p in (src, inp, out):
        try: p.kill()
        except Exception: pass
    time.sleep(0.2)

    t_in, peak_in = onset_time(in_state)
    t_out, peak_out = onset_time(out_state)
    if t_in is None or t_out is None:
        print(f"      [diag] in peak={peak_in:.4f} out peak={peak_out:.4f}",
              file=sys.stderr)
        return None
    return (t_out - t_in) * 1000.0

def run(which):
    # A fresh destination each time: a lingering null sink that has already
    # carried one run stops passing audio on the next, and a silent capture is
    # indistinguishable from a broken measurement.
    scratchsink.destroy(DEST)
    scratchsink.create(DEST)
    time.sleep(0.8)
    if which == "cpp":
        app = spawn([f"{CPP}/tests/engine_probe_bin", DEST, "12"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    else:
        app = spawn([VENV, f"{HERE}/py_engine_probe.py", DEST, "12", "25"],
                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(3.5)
    d = one_shot()
    try: app.terminate(); app.wait(timeout=6)
    except Exception: app.kill()
    time.sleep(2.0)
    return d

def main():
    res = {"cpp": [], "py": []}
    for rep in range(REPS):
        for w in (["cpp", "py"] if rep % 2 == 0 else ["py", "cpp"]):
            d = run(w)
            if d is not None and 0 <= d < 500:
                res[w].append(d)
            print(f"  rep {rep+1} {w:3s}: " +
                  (f"{d:6.1f} ms" if d is not None else "no signal"), file=sys.stderr)
    scratchsink.destroy(DEST)

    pv, cv = res["py"], res["cpp"]
    print(f"\n{'':22s} {'python':>18s} {'c++':>18s}")
    print("-" * 62)
    if len(pv) >= 2 and len(cv) >= 2:
        pm, cm = statistics.median(pv), statistics.median(cv)
        print(f"{'added latency':22s} {pm:13.1f} ms {cm:13.1f} ms")
        print(f"{'spread':22s} {min(pv):6.1f}-{max(pv):<6.1f} ms {min(cv):6.1f}-{max(cv):<6.1f} ms")
        print(f"{'samples':22s} {len(pv):16d} {len(cv):16d}")
        print("-" * 62)
        print(f"the C++ path adds {pm - cm:.1f} ms less")
        json.dump({"python_ms": pv, "cpp_ms": cv}, open("/tmp/bench_lat.json", "w"), indent=2)
    else:
        print(f"not enough samples: python={pv} cpp={cv}")

if __name__ == "__main__":
    main()
