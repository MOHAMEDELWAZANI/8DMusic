"""CPU accounting over a whole process tree.

The Python build runs its audio through pw-record and pw-play subprocesses, so
counting only the parent would credit it for work it really does.
"""
import os

HZ = os.sysconf("SC_CLK_TCK")

def children(pid, seen=None):
    if seen is None:
        seen = set()
    seen.add(pid)
    try:
        kids = open(f"/proc/{pid}/task/{pid}/children").read().split()
    except OSError:
        return seen
    for k in kids:
        k = int(k)
        if k not in seen:
            children(k, seen)
    return seen

def tree_ticks(pid):
    total = 0
    for p in children(pid):
        try:
            f = open(f"/proc/{p}/stat").read().split()
            total += int(f[13]) + int(f[14])
        except (OSError, IndexError):
            pass
    return total

def tree_rss_kb(pid):
    total = 0
    for p in children(pid):
        try:
            for line in open(f"/proc/{p}/status"):
                if line.startswith("VmRSS"):
                    total += int(line.split()[1]); break
        except OSError:
            pass
    return total

def tree_size(pid):
    return len(children(pid))
