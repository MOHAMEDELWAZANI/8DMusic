#!/usr/bin/env bash
# Launch 8D Music, setting up a private virtual environment on first run.
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VENV="$HERE/.venv"
STAMP="$VENV/.deps-ok"

cd "$HERE"

find_python() {
  for candidate in python3 python3.13 python3.12 python3.11 python; do
    if command -v "$candidate" >/dev/null 2>&1; then
      echo "$candidate"; return 0
    fi
  done
  return 1
}

if [ ! -x "$VENV/bin/python" ]; then
  PY="$(find_python)" || { echo "Python 3 is required but was not found." >&2; exit 1; }
  echo "Creating virtual environment in .venv ..."
  if ! "$PY" -m venv "$VENV" 2>/dev/null; then
    echo "Could not create a virtualenv. Install it with:" >&2
    echo "  sudo apt install python3-venv python3-tk" >&2
    exit 1
  fi
fi

if [ ! -f "$STAMP" ]; then
  echo "Installing dependencies (first run only) ..."
  "$VENV/bin/python" -m pip install --quiet --upgrade pip
  "$VENV/bin/python" -m pip install --quiet -r "$HERE/requirements.txt"
  touch "$STAMP"
fi

if ! "$VENV/bin/python" -c "import tkinter" 2>/dev/null; then
  echo "Tkinter is missing. Install it with:  sudo apt install python3-tk" >&2
  exit 1
fi

exec "$VENV/bin/python" -m eight_d "$@"
