#!/usr/bin/env python3
"""The interface: how long it takes to appear, and what it costs sitting idle.

Both apps are measured with the engine stopped and the pointer parked away from
the window, which is how an app spends most of its life.
"""
import subprocess, sys, time, os, atexit, statistics, json
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from proctree import tree_ticks, tree_rss_kb, tree_size, HZ
from Xlib import display as xdisplay, X

CPP = "/home/mohamedwazane/Projects/8DMUSIC/cpp/8dmusic"
PYDIR = "/home/mohamedwazane/Projects/8DMUSIC/python"
VENV = f"{PYDIR}/.venv/bin/python"
REPS = int(os.environ.get("REPS", "3"))
IDLE = float(os.environ.get("IDLE", "6"))

procs = []
def spawn(*a, **k):
    p = subprocess.Popen(*a, **k); procs.append(p); return p

@atexit.register
def cleanup():
    for p in procs:
        try: p.terminate(); p.wait(timeout=3)
        except Exception:
            try: p.kill()
            except Exception: pass

def window_up(d):
    root = d.screen().root
    def walk(w):
        try:
            n = w.get_wm_name()
            if n and "8D Music" in n:
                a = w.get_attributes(); g = w.get_geometry()
                if a.map_state == X.IsViewable and g.width > 200:
                    return True
            for c in w.query_tree().children:
                if walk(c): return True
        except Exception:
            pass
        return False
    return walk(root)

def measure(which):
    env = dict(os.environ, DISPLAY=":0")
    cmd = [CPP] if which == "cpp" else [VENV, "-m", "eight_d"]
    cwd = None if which == "cpp" else PYDIR
    d = xdisplay.Display(":0")
    t0 = time.perf_counter()
    app = spawn(cmd, env=env, cwd=cwd, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
    startup = None
    deadline = t0 + 30
    while time.perf_counter() < deadline:
        if window_up(d):
            startup = time.perf_counter() - t0
            break
        time.sleep(0.02)
    if startup is None:
        try: app.kill()
        except Exception: pass
        return None
    time.sleep(3.0)                      # let first-frame work settle
    c0 = tree_ticks(app.pid); s0 = time.time()
    time.sleep(IDLE)
    c1 = tree_ticks(app.pid); s1 = time.time()
    res = {"startup": startup,
           "idle_cpu": (c1 - c0) / HZ / (s1 - s0) * 100.0,
           "rss": tree_rss_kb(app.pid) / 1024.0,
           "procs": tree_size(app.pid)}
    try: app.terminate(); app.wait(timeout=6)
    except Exception: app.kill()
    d.close()
    time.sleep(2.0)
    return res

def main():
    runs = {"cpp": [], "py": []}
    for rep in range(REPS):
        for w in (["cpp", "py"] if rep % 2 == 0 else ["py", "cpp"]):
            r = measure(w)
            if r:
                runs[w].append(r)
                print(f"  rep {rep+1} {w:3s}: start={r['startup']*1000:6.0f} ms  "
                      f"idle={r['idle_cpu']:5.2f}%  rss={r['rss']:6.1f} MB  "
                      f"procs={r['procs']}", file=sys.stderr)
            else:
                print(f"  rep {rep+1} {w:3s}: window never appeared", file=sys.stderr)

    print(f"\n{'metric':28s} {'python':>14s} {'c++':>14s}")
    print("-" * 60)
    out = {}
    for key, name, unit, scale in (("startup", "time to window", "ms", 1000.0),
                                   ("idle_cpu", "idle CPU", "%", 1.0),
                                   ("rss", "memory", "MB", 1.0),
                                   ("procs", "processes", "", 1.0)):
        pv = [r[key] * scale for r in runs["py"]]
        cv = [r[key] * scale for r in runs["cpp"]]
        if not pv or not cv: continue
        pm, cm = statistics.median(pv), statistics.median(cv)
        out[key] = {"python": pm, "cpp": cm, "python_all": pv, "cpp_all": cv}
        print(f"{name:28s} {pm:10.1f} {unit:<3s} {cm:10.1f} {unit:<3s}")
    print("-" * 60)
    json.dump(out, open("/tmp/bench_gui.json", "w"), indent=2)

if __name__ == "__main__":
    main()
