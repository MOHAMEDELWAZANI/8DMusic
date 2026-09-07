"""Settings persistence."""

from __future__ import annotations

import dataclasses
import json
import os
from pathlib import Path

from .dsp import CHARACTERS, MODES, Params


def config_path() -> Path:
    base = os.environ.get("XDG_CONFIG_HOME") or (Path.home() / ".config")
    return Path(base) / "8dmusic" / "settings.json"


_FIELDS = {f.name: f for f in dataclasses.fields(Params)}


def _coerce(name: str, value):
    field = _FIELDS[name]
    if field.type is bool or isinstance(getattr(Params(), name), bool):
        return bool(value)
    if name == "mode":
        return value if value in MODES else Params().mode
    if name == "character":
        return value if value in CHARACTERS else Params().character
    if name == "direction":
        return 1 if int(value) >= 0 else -1
    return float(value)


def load() -> tuple[Params, dict]:
    """Return the saved parameters plus the saved UI preferences."""
    path = config_path()
    if not path.exists():
        return Params(), {}
    try:
        data = json.loads(path.read_text())
    except (OSError, json.JSONDecodeError):
        return Params(), {}

    values = {}
    for name, value in (data.get("params") or {}).items():
        if name not in _FIELDS:
            continue
        try:
            values[name] = _coerce(name, value)
        except (TypeError, ValueError):
            continue
    prefs = data.get("prefs") or {}
    return Params(**values), prefs if isinstance(prefs, dict) else {}


def save(params: Params, prefs: dict | None = None) -> None:
    path = config_path()
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = {"params": dataclasses.asdict(params), "prefs": prefs or {}}
        tmp = path.with_suffix(".json.tmp")
        tmp.write_text(json.dumps(payload, indent=2))
        tmp.replace(path)
    except OSError:
        pass  # settings are a convenience, never a reason to fail
