"""Working out what the user is actually listening to.

Two independent sources, best first:

    MPRIS      players publish title, artist, length and position on the session
               bus.  Exact, local, no network -- and it hands us working
               transport controls for free.
    PipeWire   the playback node's own props.  Coarser, and most apps leave the
               interesting fields empty, but it covers players that never
               registered on the bus at all.

Both shell out, and a 50 ms subprocess in the middle of a 33 ms UI frame is a
visible stutter, so all of it happens on a background thread and the interface
only ever reads the last snapshot.

MPRIS is reached through ``busctl``, whose ``--json`` output saves us parsing
GVariant text by hand.  Note that a player confined by a snap or flatpak will
refuse callers whose AppArmor label it does not recognise -- launching this app
from an IDE's embedded terminal is enough to trigger that -- so a denial is
treated as "no metadata", never as an error.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import threading
import time
from dataclasses import dataclass, replace
from pathlib import Path

from . import pipewire as pw

BUS_PREFIX = "org.mpris.MediaPlayer2."
OBJECT_PATH = "/org/mpris/MediaPlayer2"
ROOT_IFACE = "org.mpris.MediaPlayer2"
PLAYER_IFACE = "org.mpris.MediaPlayer2.Player"
PROPS_IFACE = "org.freedesktop.DBus.Properties"

#: How often the bus is asked what is playing, and how often the far slower
#: `pw-dump` is run to see whose audio we are actually carrying.
POLL_INTERVAL = 1.0
GRAPH_INTERVAL = 3.0

# -- title tidying ---------------------------------------------------------
# Uploaders put a lot into a title that is not the name of the song.  These
# only ever remove or move text, so a title we fail to recognise is left alone.

_CHANNEL_SUFFIX = re.compile(r"\s*-\s*Topic\s*$|\s*VEVO\s*$", re.I)
_NOISE = re.compile(
    r"""\s*[\(\[]\s*
        (?:official\s+)?(?:music\s+)?
        (?:video|audio|lyrics?(?:\s+video)?|visuali[sz]er|hd|hq|4k|
           remastered(?:\s+\d{4})?|explicit|clean|full\s+song)
        \s*[\)\]]""",
    re.I | re.X,
)
_FEATURED = re.compile(
    r"""\s*[\(\[]?\s*\b(?:feat|ft|featuring)\b\.?\s+([^)\]]+?)\s*[\)\]]?\s*$""",
    re.I | re.X,
)
#: Deliberately conservative -- "and" and "x" appear inside band names far too
#: often to be treated as separators.
_SPLIT_ARTISTS = re.compile(r"\s*(?:,|&|;)\s*")


def _first_letter(text: str) -> str:
    return next((ch for ch in text if ch.isalnum()), "")


@dataclass(frozen=True)
class Track:
    """One snapshot of what is playing."""

    title: str = ""
    artists: tuple[str, ...] = ()
    album: str = ""
    player: str = ""            # human name of the app -- "Spotify", "Brave"
    playing: bool = False
    length: float = 0.0         # seconds; 0 when the player does not say
    position: float = 0.0       # seconds, as read at `stamp`
    stamp: float = 0.0          # time.monotonic() when `position` was read
    can_prev: bool = False
    can_next: bool = False
    can_seek: bool = False
    captured: bool = False      # its audio is flowing through our virtual sink
    bus: str = ""               # empty for the PipeWire fallback
    track_id: str = ""
    pid: int = -1
    source: str = "mpris"

    @property
    def artist_line(self) -> str:
        return " · ".join(self.artists)

    @property
    def initials(self) -> str:
        """The cover mark: one capital per artist, or the first two letters.

        Two names give their initials -- Eminem and Rihanna become ``Er``; a
        single name gives its opening pair, so Eminem alone becomes ``Em``.
        """
        names = [a for a in self.artists if _first_letter(a)]
        if not names and _first_letter(self.title):
            names = [self.title]
        if not names:
            return "—"
        if len(names) >= 2:
            return _first_letter(names[0]).upper() + _first_letter(names[1]).lower()
        letters = [ch for ch in names[0] if ch.isalnum()][:2]
        return "".join(letters).capitalize()

    def at(self, now: float) -> float:
        """Position carried forward to `now`, so the bar moves between polls."""
        elapsed = (now - self.stamp) if self.playing else 0.0
        pos = self.position + max(elapsed, 0.0)
        return min(pos, self.length) if self.length > 0 else pos

    @property
    def known(self) -> bool:
        """Whether we have a song, as opposed to just knowing an app is busy."""
        return bool(self.title or self.artists)


# --------------------------------------------------------------------------
# cleaning up what players report
# --------------------------------------------------------------------------


def _clean(title: str, artists: list[str]) -> tuple[str, tuple[str, ...]]:
    """Turn a player's raw fields into a title and a list of performers.

    Streaming sites hand us the uploader as the artist and stuff everything
    else into the title, so ``Eminem - Love The Way You Lie ft. Rihanna`` with
    an artist of ``EminemVEVO`` has to come apart into the song and the two
    people on it.
    """
    names = [_CHANNEL_SUFFIX.sub("", a).strip() for a in artists]
    names = [a for a in names if a]

    title = title.strip()
    # An uploader name is not a performer: if it was the only "artist" and the
    # title still carries the usual "artist - song", trust the title.
    if " - " in title and (not names or names[0].lower() in title.lower()[:len(names[0]) + 2]):
        head, _, tail = title.partition(" - ")
        if head.strip() and tail.strip():
            names = _SPLIT_ARTISTS.split(head.strip()) if not names else names
            title = tail.strip()

    title = _NOISE.sub("", title).strip()

    featured = _FEATURED.search(title)
    if featured:
        title = title[:featured.start()].strip()
        names += [n.strip() for n in _SPLIT_ARTISTS.split(featured.group(1))]

    seen, ordered = set(), []
    for name in names:
        name = _NOISE.sub("", name).strip(" -–—")
        key = name.lower()
        if name and key not in seen:
            seen.add(key)
            ordered.append(name)
    return title, tuple(ordered)


# --------------------------------------------------------------------------
# the session bus
# --------------------------------------------------------------------------


def _unwrap(value):
    """busctl wraps every value as {"type": ..., "data": ...}."""
    if isinstance(value, dict) and "data" in value and "type" in value:
        return _unwrap(value["data"])
    if isinstance(value, dict):
        return {k: _unwrap(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_unwrap(v) for v in value]
    return value


def _busctl(args: list[str], timeout: float = 3.0):
    try:
        res = subprocess.run(["busctl", "--user", "--json=short", *args],
                             capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.SubprocessError):
        return None
    if res.returncode != 0 or not res.stdout.strip():
        return None       # no such player, or the peer refused us
    try:
        return json.loads(res.stdout)
    except json.JSONDecodeError:
        return None


def _ancestry(pid: int, limit: int = 8) -> set[int]:
    """`pid` and its parents, so a browser's audio child can be traced home."""
    chain: set[int] = set()
    for _ in range(limit):
        if pid <= 1 or pid in chain:
            break
        chain.add(pid)
        try:
            stat = Path(f"/proc/{pid}/stat").read_text()
        except OSError:
            break
        # The command name sits in parentheses and may itself contain spaces,
        # so the fields only line up after the final ')'.
        tail = stat[stat.rfind(")") + 1:].split()
        if len(tail) < 2:
            break
        try:
            pid = int(tail[1])
        except ValueError:
            break
    return chain


class Watcher:
    """Polls for the current track in the background.

    Read :attr:`track` from anywhere; it is either ``None`` or the last
    complete snapshot.  The transport methods act on whichever player that
    snapshot came from.
    """

    def __init__(self, interval: float = POLL_INTERVAL,
                 graph_interval: float = GRAPH_INTERVAL):
        self.interval = interval
        self.graph_interval = graph_interval
        self.sink_id: int | None = None      # our virtual sink, when running

        self._track: Track | None = None
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._wake = threading.Event()
        self._thread: threading.Thread | None = None
        self._identities: dict[str, str] = {}
        self._graph: tuple[list[pw.Stream], set[int]] = ([], set())
        self._graph_at = 0.0
        self._have_busctl = shutil.which("busctl") is not None

    # -- lifecycle ---------------------------------------------------------

    def start(self) -> None:
        if self._thread is not None:
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._loop, name="8d-nowplaying",
                                        daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()
        self._wake.set()
        thread, self._thread = self._thread, None
        if thread is not None:
            thread.join(timeout=2.0)

    @property
    def track(self) -> Track | None:
        with self._lock:
            return self._track

    def _loop(self) -> None:
        while not self._stop.is_set():
            try:
                self._refresh()
            except Exception:       # noqa: BLE001 - a readout is never fatal
                pass
            # A transport press sets the event so the change shows immediately
            # instead of a second later.
            self._wake.wait(self.interval)
            self._wake.clear()

    # -- the poll ----------------------------------------------------------

    def _refresh(self) -> None:
        candidates = self._mpris_tracks()

        # `pw-dump` is by far the most expensive thing here, so only pay for it
        # when it can actually tell us something: either we are running and can
        # say whose audio we carry, or the bus came back empty-handed.
        if self.sink_id is not None or not candidates:
            self._refresh_graph()
            streams, captured_ids = self._graph
        else:
            streams, captured_ids = [], set()

        candidates = [replace(t, captured=self._is_captured(t, streams, captured_ids))
                      for t in candidates]
        if not candidates:
            candidates = self._pipewire_tracks(streams, captured_ids)

        with self._lock:
            self._track = self._pick(candidates)

    @staticmethod
    def _pick(candidates: list[Track]) -> Track | None:
        """Playing beats paused, ours beats somebody else's, detail beats none."""
        if not candidates:
            return None
        return max(candidates, key=lambda t: (t.playing, t.captured, t.known))

    def _refresh_graph(self) -> None:
        now = time.monotonic()
        if now - self._graph_at < self.graph_interval and self._graph_at:
            return
        self._graph_at = now
        try:
            objects = pw.dump()
        except pw.PipeWireError:
            return
        streams = pw.list_output_streams(objects)
        captured = (pw.streams_into(self.sink_id, objects)
                    if self.sink_id is not None else set())
        self._graph = (streams, captured)

    def _is_captured(self, track: Track, streams: list[pw.Stream],
                     captured_ids: set[int]) -> bool:
        """Is this player's audio the audio we are processing?

        Matching on the app name alone is not enough for a browser, whose bus
        connection belongs to the main process while the sound comes out of a
        child, so fall back to walking the stream's parents.
        """
        if not captured_ids:
            return False
        name = track.player.lower()
        family: set[int] | None = None
        for stream in streams:
            if stream.id not in captured_ids:
                continue
            if name and name == stream.app.lower():
                return True
            if track.pid > 0 and stream.pid > 0:
                if family is None:
                    family = set()
                if stream.pid not in family:
                    family |= _ancestry(stream.pid)
                if track.pid in family:
                    return True
        return False

    # -- MPRIS -------------------------------------------------------------

    def _player_buses(self) -> list[tuple[str, int]]:
        if not self._have_busctl:
            return []
        rows = _busctl(["list"])
        if not isinstance(rows, list):
            return []
        buses = []
        for row in rows:
            name = row.get("name") or ""
            if name.startswith(BUS_PREFIX):
                try:
                    buses.append((name, int(row.get("pid") or -1)))
                except (TypeError, ValueError):
                    buses.append((name, -1))
        self._identities = {b: n for b, n in self._identities.items()
                            if b in {bus for bus, _ in buses}}
        return buses

    def _identity(self, bus: str) -> str:
        """The player's own name for itself, asked once per connection."""
        if bus in self._identities:
            return self._identities[bus]
        reply = _busctl(["get-property", bus, OBJECT_PATH, ROOT_IFACE, "Identity"])
        name = _unwrap(reply) if reply else None
        if not isinstance(name, str) or not name:
            # org.mpris.MediaPlayer2.spotify.instance7 -> Spotify
            name = bus[len(BUS_PREFIX):].split(".")[0].replace("_", " ").title()
        self._identities[bus] = name
        return name

    def _mpris_tracks(self) -> list[Track]:
        tracks = []
        for bus, pid in self._player_buses():
            reply = _busctl(["call", bus, OBJECT_PATH, PROPS_IFACE, "GetAll",
                             "s", PLAYER_IFACE])
            props = _unwrap(reply) if reply else None
            if not isinstance(props, list) or not props:
                continue        # gone, or refusing us
            props = props[0]
            if not isinstance(props, dict):
                continue
            status = props.get("PlaybackStatus")
            if status == "Stopped":
                continue
            meta = props.get("Metadata") or {}
            raw_artists = meta.get("xesam:artist") or []
            if isinstance(raw_artists, str):
                raw_artists = [raw_artists]
            title, artists = _clean(str(meta.get("xesam:title") or ""),
                                    [str(a) for a in raw_artists])
            if not title and not artists:
                continue
            tracks.append(Track(
                title=title,
                artists=artists,
                album=str(meta.get("xesam:album") or ""),
                player=self._identity(bus),
                playing=status == "Playing",
                length=float(meta.get("mpris:length") or 0) / 1e6,
                position=float(props.get("Position") or 0) / 1e6,
                stamp=time.monotonic(),
                can_prev=bool(props.get("CanGoPrevious")),
                can_next=bool(props.get("CanGoNext")),
                can_seek=bool(props.get("CanSeek")),
                bus=bus,
                track_id=str(meta.get("mpris:trackid") or ""),
                pid=pid,
            ))
        return tracks

    # -- PipeWire fallback -------------------------------------------------

    def _pipewire_tracks(self, streams: list[pw.Stream],
                         captured_ids: set[int]) -> list[Track]:
        """What the graph alone can tell us about a player that stays silent."""
        tracks = []
        for stream in streams:
            captured = stream.id in captured_ids
            if not captured and not stream.title:
                continue
            title, artists = _clean(stream.title, [stream.artist] if stream.artist else [])
            tracks.append(Track(
                title=title,
                artists=artists,
                player=stream.app,
                playing=True,
                stamp=time.monotonic(),
                captured=captured,
                source="pipewire",
                pid=stream.pid,
            ))
        return tracks

    # -- transport ---------------------------------------------------------

    def previous(self) -> None:
        self._command("Previous")

    def play_pause(self) -> None:
        self._command("PlayPause")

    def next(self) -> None:
        self._command("Next")

    def seek(self, fraction: float) -> None:
        track = self.track
        if track is None or not (track.can_seek and track.track_id and track.length):
            return
        micros = int(min(max(fraction, 0.0), 1.0) * track.length * 1e6)
        self._command("SetPosition", "ox", track.track_id, str(micros))

    def _command(self, member: str, *args: str) -> None:
        """Fire and forget, off the caller's thread -- busctl takes ~30 ms."""
        track = self.track
        if track is None or not track.bus:
            return
        bus = track.bus

        def run():
            _busctl(["call", bus, OBJECT_PATH, PLAYER_IFACE, member, *args])
            time.sleep(0.15)        # let the player settle before we re-read it
            self._wake.set()

        threading.Thread(target=run, name="8d-transport", daemon=True).start()
