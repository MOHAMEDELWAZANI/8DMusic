"""Entry point: python -m eight_d"""

from __future__ import annotations

import argparse
import sys

from . import __version__


def main() -> int:
    parser = argparse.ArgumentParser(
        prog="8dmusic",
        description="Apply a real-time 8D spatial effect to all system audio.",
    )
    parser.add_argument("--version", action="version", version=f"8D Music {__version__}")
    parser.add_argument("--list-devices", action="store_true",
                        help="print available audio outputs and exit")
    parser.add_argument("--check", action="store_true",
                        help="verify the system can run the tool and exit")
    args = parser.parse_args()

    from . import pipewire as pw

    if args.check or args.list_devices:
        missing = pw.missing_tools()
        if missing:
            print("Missing required tools:", ", ".join(missing), file=sys.stderr)
            print("Install them with: sudo apt install pipewire-bin", file=sys.stderr)
            return 1
        try:
            sinks = pw.list_sinks()
        except pw.PipeWireError as exc:
            print(f"PipeWire is not reachable: {exc}", file=sys.stderr)
            return 1
        print("PipeWire tools: OK")
        print(f"Audio outputs ({len(sinks)}):")
        for sink in sinks:
            print(f"  - {sink}   [{sink.name}]")
        default = pw.default_sink_name()
        if default:
            print(f"Current default: {default}")
        return 0 if sinks else 1

    from .ui import main as run_ui
    return run_ui()


if __name__ == "__main__":
    raise SystemExit(main())
