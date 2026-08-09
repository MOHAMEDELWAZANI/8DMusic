"""PipeWire plumbing.

To process everything the machine plays we insert ourselves into the graph:

    apps  ->  [ 8D Music virtual sink ]  ->  monitor  ->  us  ->  real speakers

Making our virtual sink the default routes new streams into it automatically;
already-running streams are moved across explicitly and put back on exit.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import time
from dataclasses import dataclass

NODE_PREFIX = "eight_d_music"
SINK_NODE_NAME = f"{NODE_PREFIX}_sink"
SINK_DESCRIPTION = "8D Music"
PLAYBACK_NODE_NAME = f"{NODE_PREFIX}_output"
CAPTURE_NODE_NAME = f"{NODE_PREFIX}_input"

REQUIRED_TOOLS = ("pw-cli", "pw-dump", "pw-metadata", "pw-record", "pw-play")


class PipeWireError(RuntimeError):
    pass


@dataclass
class Sink:
    id: int
    serial: int
    name: str
    description: str

    def __str__(self) -> str:  # what the device dropdown shows
        return self.description or self.name


@dataclass
class Stream:
    id: int
    serial: int
    name: str
    app: str
    pid: int = -1
    title: str = ""      # only a handful of players fill these in
    artist: str = ""


def missing_tools() -> list[str]:
    return [t for t in REQUIRED_TOOLS if shutil.which(t) is None]


def _run(args: list[str], timeout: float = 5.0) -> str:
    try:
        res = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise PipeWireError(f"{args[0]} failed: {exc}") from exc
    return res.stdout


def dump() -> list[dict]:
    raw = _run(["pw-dump"], timeout=8.0)
    try:
        return json.loads(raw)
    except json.JSONDecodeError as exc:
        raise PipeWireError(f"could not parse pw-dump output: {exc}") from exc


def _props(obj: dict) -> dict:
    return (obj.get("info") or {}).get("props") or {}


def list_sinks(objects: list[dict] | None = None, include_virtual: bool = False) -> list[Sink]:
    """Real output devices we can send the processed audio to."""
    sinks: list[Sink] = []
    for obj in objects if objects is not None else dump():
        p = _props(obj)
        if p.get("media.class") != "Audio/Sink":
            continue
        name = p.get("node.name", "")
        if not include_virtual and name == SINK_NODE_NAME:
            continue
        sinks.append(
            Sink(
                id=int(obj.get("id", -1)),
                serial=int(p.get("object.serial", -1)),
                name=name,
                description=p.get("node.description") or p.get("node.nick") or name,
            )
        )
    return sinks


def list_output_streams(objects: list[dict] | None = None) -> list[Stream]:
    """Application playback streams, minus our own."""
    streams: list[Stream] = []
    for obj in objects if objects is not None else dump():
        p = _props(obj)
        if p.get("media.class") != "Stream/Output/Audio":
            continue
        name = p.get("node.name", "")
        # Never touch our own playback stream -- routing it into the virtual
        # sink would feed our output straight back into our input.
        if name.startswith(NODE_PREFIX):
            continue
        try:
            pid = int(p.get("application.process.id", -1))
        except (TypeError, ValueError):
            pid = -1
        streams.append(
            Stream(
                id=int(obj.get("id", -1)),
                serial=int(p.get("object.serial", -1)),
                name=name,
                app=p.get("application.name") or p.get("media.name") or name,
                pid=pid,
                title=p.get("media.title") or "",
                artist=p.get("media.artist") or "",
            )
        )
    return streams


def streams_into(sink_id: int | None, objects: list[dict] | None = None) -> set[int]:
    """Ids of the stream nodes currently linked into `sink_id`.

    A stream's `target.object` says where it was *asked* to go; the links say
    where its audio is actually arriving, which is what "captured" has to mean.
    """
    if sink_id is None:
        return set()
    linked: set[int] = set()
    for obj in objects if objects is not None else dump():
        if not str(obj.get("type", "")).endswith("Link"):
            continue
        p = _props(obj)
        if p.get("link.input.node") != sink_id:
            continue
        try:
            linked.add(int(p["link.output.node"]))
        except (KeyError, TypeError, ValueError):
            continue
    return linked


def default_sink_name() -> str | None:
    out = _run(["pw-metadata", "-n", "default", "0", "default.audio.sink"])
    for line in out.splitlines():
        if "default.audio.sink" not in line:
            continue
        start = line.find("value:")
        if start < 0:
            continue
        value = line[start + len("value:"):].strip()
        if value.startswith("'") and "'" in value[1:]:
            value = value[1:1 + value[1:].find("'")]
        try:
            return json.loads(value).get("name")
        except json.JSONDecodeError:
            return None
    return None


def set_default_sink(name: str) -> None:
    payload = json.dumps({"name": name})
    for key in ("default.configured.audio.sink", "default.audio.sink"):
        _run(["pw-metadata", "-n", "default", "0", key, payload, "Spa:String:JSON"])


def move_stream(stream_id: int, sink_serial: int) -> None:
    _run(["pw-metadata", "-n", "default", str(stream_id), "target.object", str(sink_serial)])


def clear_stream_target(stream_id: int) -> None:
    _run(["pw-metadata", "-n", "default", "-d", str(stream_id), "target.object"])


class VirtualSink:
    """A null sink owned by this process.

    It is created through an interactive ``pw-cli`` session that we keep alive:
    when that process goes away -- cleanly or not -- PipeWire drops the node, so
    a crash can never leave a dead sink behind as the system default.
    """

    def __init__(self, description: str = SINK_DESCRIPTION):
        self.description = description
        self.proc: subprocess.Popen | None = None
        self.id: int | None = None
        self.serial: int | None = None

    def create(self, timeout: float = 6.0) -> None:
        if self.proc is not None:
            return
        if any(s.name == SINK_NODE_NAME for s in list_sinks(include_virtual=True)):
            raise PipeWireError(
                "An '8D Music' sink already exists — another copy of the app is "
                "probably running. Close it and try again."
            )
        self.proc = subprocess.Popen(
            ["pw-cli"],
            stdin=subprocess.PIPE,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1,
        )
        command = (
            "create-node adapter { "
            "factory.name=support.null-audio-sink "
            f"node.name={SINK_NODE_NAME} "
            f'node.description="{self.description}" '
            "media.class=Audio/Sink "
            "audio.position=[FL,FR] "
            "monitor.channel-volumes=true "
            "object.linger=false "
            "}\n"
        )
        assert self.proc.stdin is not None
        self.proc.stdin.write(command)
        self.proc.stdin.flush()

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for sink in list_sinks(include_virtual=True):
                if sink.name == SINK_NODE_NAME:
                    self.id, self.serial = sink.id, sink.serial
                    return
            time.sleep(0.15)

        self.destroy()
        raise PipeWireError(
            "PipeWire did not create the virtual sink in time. "
            "Try restarting the audio service with: systemctl --user restart pipewire"
        )

    def destroy(self) -> None:
        if self.proc is None:
            return
        proc, self.proc = self.proc, None
        self.id = self.serial = None
        try:
            if proc.stdin is not None:
                proc.stdin.close()
            proc.terminate()
            proc.wait(timeout=3.0)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()


class Router:
    """Moves application audio into the virtual sink and puts it back after."""

    def __init__(self, sink: VirtualSink):
        self.sink = sink
        self._previous_default: str | None = None
        self._moved: set[int] = set()

    def engage(self) -> None:
        if self.sink.serial is None:
            raise PipeWireError("virtual sink is not running")
        self._previous_default = default_sink_name()
        if self._previous_default != SINK_NODE_NAME:
            set_default_sink(SINK_NODE_NAME)
        self.capture_existing_streams()

    def adopt_default(self, name: str) -> None:
        """Remember a default the user chose while we were holding the graph.

        Without this, stopping would restore whatever was default at start --
        putting the user back on a device they have since moved away from.
        """
        if name and name != SINK_NODE_NAME:
            self._previous_default = name

    def capture_existing_streams(self) -> int:
        """Pull any stray playback streams into the virtual sink."""
        if self.sink.serial is None:
            return 0
        moved = 0
        for stream in list_output_streams():
            if stream.id in self._moved:
                continue
            try:
                move_stream(stream.id, self.sink.serial)
            except PipeWireError:
                continue
            self._moved.add(stream.id)
            moved += 1
        return moved

    def release(self) -> None:
        # Restore the default first: clearing a stream's target makes it follow
        # whatever the default happens to be at that moment.
        if self._previous_default and self._previous_default != SINK_NODE_NAME:
            try:
                set_default_sink(self._previous_default)
            except PipeWireError:
                pass
        self._previous_default = None
        for stream_id in self._moved:
            try:
                clear_stream_target(stream_id)
            except PipeWireError:
                pass
        self._moved.clear()
