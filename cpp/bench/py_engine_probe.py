#!/usr/bin/env python3
"""Headless Python engine, for benchmarking against the C++ engine_probe.

Routing side effects are neutralised so the machine's real audio is never
touched, but the *cost* of the graph housekeeping is kept: the stub still runs
the pw-dump the real router runs, because that polling is exactly what the two
versions do differently.
"""
import sys, time, os
sys.path.insert(0, "/home/mohamedwazane/Projects/8DMUSIC/python")
from eight_d.engine import AudioEngine
from eight_d.dsp import Params
from eight_d import pipewire as pw


class CostOnlyRouter:
    """Does the work the real router does, without changing any routing."""
    def engage(self): pw.default_sink_name()
    def release(self): pass
    def adopt_default(self, name): pass
    def capture_existing_streams(self):
        try:
            pw.dump()          # the subprocess the real router pays for
        except Exception:
            pass
        return 0


def main():
    target = sys.argv[1]
    seconds = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    latency = int(sys.argv[3]) if len(sys.argv) > 3 else 25

    eng = AudioEngine()
    eng._router = CostOnlyRouter()
    eng.set_params(Params(mode="circular", speed=0.5, depth=1.0, smoothness=0.05,
                          reverb_mix=0.0, delay_mix=0.0, output_gain=1.0,
                          pause_when_silent=False))
    eng.start(target, latency)
    print(f"started -> {target}", flush=True)
    for i in range(seconds):
        time.sleep(1.0)
        st = eng.status
        print(f"t={i+1}s blocks={st.blocks} underruns={st.underruns} "
              f"load={st.load*100:.1f}% angle={eng.angle:+.2f}", flush=True)
    eng.stop()
    print("stopped cleanly", flush=True)


if __name__ == "__main__":
    main()
