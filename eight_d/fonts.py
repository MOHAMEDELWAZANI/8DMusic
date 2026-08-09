"""The faces that ship with the app, and getting Tk to see them.

Tk can only draw with fonts fontconfig already knows about, and asking someone
to install two faces by hand before the interface reads properly is not a
reasonable thing to require.  So the bundled directory is pointed at through a
small generated fontconfig file that pulls in the system configuration first and
then adds our own directory, set on ``FONTCONFIG_FILE`` before Tk starts.

Nothing is written to the user's font directories and nothing is registered
system-wide; the file lives in the cache directory and only affects this
process.  If anything about that is not in place -- no system configuration to
build on, an unwritable cache, or a user who is already driving fontconfig
themselves -- the whole thing is skipped and the interface falls back to fonts
the system already has.
"""

from __future__ import annotations

import os
from pathlib import Path

DIRECTORY = Path(__file__).resolve().parent / "assets" / "fonts"
SYSTEM_CONFIG = Path("/etc/fonts/fonts.conf")

#: Arabic is drawn with the patched build produced by tools/make_arabic_font.py,
#: which is the same face with its joined letter forms made addressable.
ARABIC = "KO Methlama 8D"
LATIN = "OffBit"

_TEMPLATE = """<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig>
  <include ignore_missing="yes">{system}</include>
  <dir>{directory}</dir>
</fontconfig>
"""


def bootstrap() -> None:
    """Add the bundled directory to this process's font search path.

    Must run before the Tk interpreter is created, since fontconfig reads the
    variable once and caches the result for the life of the process.
    """
    if os.environ.get("FONTCONFIG_FILE"):
        return                                  # already configured; leave it
    if not DIRECTORY.is_dir() or not SYSTEM_CONFIG.exists():
        # Without the system config to include, pointing fontconfig at our
        # directory would hide every other font on the machine.
        return
    cache = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache")
    path = cache / "8dmusic" / "fonts.conf"
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(_TEMPLATE.format(system=SYSTEM_CONFIG, directory=DIRECTORY))
    except OSError:
        return                                  # cosmetic, never worth failing for
    os.environ["FONTCONFIG_FILE"] = str(path)
