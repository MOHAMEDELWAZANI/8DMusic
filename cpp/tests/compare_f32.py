#!/usr/bin/env python3
"""Compare two raw f32 dumps from dsp_probe.

Both the Python and the Android builds are held to the same bar as the C++
one: the same probe, the same signal, then correlation and worst-case sample
difference against the reference.

    python3 cpp/tests/compare_f32.py REFERENCE_DIR CANDIDATE_DIR
    python3 cpp/tests/compare_f32.py ref/circular.f32 and/circular.f32
"""
import sys
import pathlib
import numpy as np


def compare(ref_path, cand_path):
    a = np.fromfile(ref_path, dtype=np.float32)
    b = np.fromfile(cand_path, dtype=np.float32)
    n = min(a.size, b.size)
    if n == 0:
        return None
    a, b = a[:n], b[:n]

    denom = np.linalg.norm(a) * np.linalg.norm(b)
    corr = float(np.dot(a, b) / denom) if denom else float("nan")
    diff = np.abs(a - b)
    return corr, float(diff.max()), float(np.sqrt(np.mean(diff**2))), n, a.size != b.size


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
