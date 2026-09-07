"""A throwaway null sink for benchmarks.

Created with object.linger so no process has to be held open for it: keeping a
pw-cli alive on a pipe has twice now turned into a process spinning on a core,
which quietly poisons every measurement taken while it runs.
"""
import subprocess, json, time, os, signal

def _reap_pw_cli():
    """pw-cli sometimes fails to exit and spins on a core, which silently
    poisons any measurement running at the time.  Never leave one behind."""
    try:
        out = subprocess.run(["pgrep", "-x", "pw-cli"], capture_output=True,
                             text=True, timeout=5).stdout.split()
    except Exception:
        return
    for pid in out:
        try:
            os.kill(int(pid), signal.SIGTERM)
        except Exception:
            pass


def _node_id(name):
    try:
        d = json.loads(subprocess.run(["pw-dump"], capture_output=True,
                                      text=True, timeout=10).stdout)
    except Exception:
        return None
    for o in d:
        p = (o.get("info") or {}).get("props") or {}
        if p.get("node.name") == name:
            return o.get("id")
    return None

def create(name, timeout=8.0):
    if _node_id(name) is not None:
        destroy(name)
    cmd = (f'create-node adapter {{ factory.name=support.null-audio-sink '
           f'node.name={name} node.description="{name}" media.class=Audio/Sink '
           f'audio.position=[FL,FR] object.linger=true }}')
    subprocess.run(["pw-cli", *cmd.split(" ", 1)[:1], *[cmd.split(" ", 1)[1]]],
                   capture_output=True, text=True, timeout=timeout)
    _reap_pw_cli()
    deadline = time.time() + timeout
    while time.time() < deadline:
        nid = _node_id(name)
        if nid is not None:
            return nid
        time.sleep(0.2)
    raise RuntimeError(f"scratch sink {name} never appeared")

def destroy(name):
    nid = _node_id(name)
    if nid is None:
        return False
    subprocess.run(["pw-cli", "destroy", str(nid)], capture_output=True,
                   text=True, timeout=8)
    _reap_pw_cli()
    time.sleep(0.5)
    return _node_id(name) is None
