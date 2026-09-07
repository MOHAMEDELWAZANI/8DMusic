#!/usr/bin/env bash
# Build if needed, then launch.
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"

if [ ! -x ./8dmusic ] || [ -n "$(find src -newer ./8dmusic -name '*.cpp' -o -newer ./8dmusic -name '*.h' 2>/dev/null)" ]; then
  echo "Building ..."
  if ! make; then
    echo
    echo "The build needs the development headers:" >&2
    echo "  sudo apt install build-essential pkg-config \\" >&2
    echo "                   libpipewire-0.3-dev libcairo2-dev libx11-dev" >&2
    exit 1
  fi
fi
exec ./8dmusic "$@"
