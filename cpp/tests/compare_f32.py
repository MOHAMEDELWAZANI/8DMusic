#!/usr/bin/env python3
"""Compare two raw f32 dumps from dsp_probe.

Every platform is held to the same bar: the same probe, the same signal, then
correlation and worst-case sample difference against the reference build.

    python3 cpp/tests/compare_f32.py REFERENCE_DIR CANDIDATE_DIR
    python3 cpp/tests/compare_f32.py ref/circular.f32 win/circular.f32

Deliberately has no dependencies. This is the check that keeps "one effect,
three platforms" honest, so it has to run anywhere a probe can -- including a
fresh Windows box with nothing installed but Python itself.
"""
import array
import math
import pathlib
import sys


def read_f32(path):
    a = array.array("f")
    size = path.stat().st_size
    with path.open("rb") as f:
        a.fromfile(f, size // 4)
    if sys.byteorder == "big":          # the probe always writes little-endian
        a.byteswap()
    return a


def compare(ref_path, cand_path):
    a = read_f32(ref_path)
    b = read_f32(cand_path)
    n = min(len(a), len(b))
    if n == 0:
        return None

    dot = na = nb = 0.0
    worst = 0.0
    sq = 0.0
    for i in range(n):
        x = a[i]
        y = b[i]
        dot += x * y
        na += x * x
        nb += y * y
        d = abs(x - y)
        if d > worst:
            worst = d
        sq += d * d

    denom = math.sqrt(na) * math.sqrt(nb)
    corr = dot / denom if denom else float("nan")
    return corr, worst, math.sqrt(sq / n), n, len(a) != len(b)


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2

    ref, cand = pathlib.Path(argv[1]), pathlib.Path(argv[2])
    pairs = []
    if ref.is_dir():
        for f in sorted(ref.glob("*.f32")):
            other = cand / f.name
            if other.exists():
                pairs.append((f, other))
            else:
                print(f"{f.stem:<10} MISSING on the candidate side")
    else:
        pairs.append((ref, cand))

    if not pairs:
        print("nothing to compare")
        return 1

    print(f"{'mode':<10} {'correlation':>12} {'max diff':>11} {'rms diff':>11}  {'samples':>9}")
    worst = 0.0
    for r, c in pairs:
        result = compare(r, c)
        if result is None:
            print(f"{r.stem:<10} empty file")
            continue
        corr, mx, rms, n, truncated = result
        worst = max(worst, mx)
        note = "  (length differs, compared the overlap)" if truncated else ""
        print(f"{r.stem:<10} {corr:>12.6f} {mx:>11.6f} {rms:>11.6f}  {n:>9}{note}")

    print(f"\nlargest sample difference across all modes: {worst:.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
