"""The real-time audio engine.

A worker thread pulls raw stereo float32 from the virtual sink's monitor
(``pw-record``), runs it through :class:`EightDProcessor`, and pushes it to the
chosen output device (``pw-play``).  Both are plain pipes, so no PortAudio or
other native binding is needed -- just the PipeWire tools already on the system.
"""

from __future__ import annotations

import subprocess
import threading
import time
from dataclasses import dataclass

import numpy as np

from . import pipewire as pw
from .dsp import EightDProcessor, Params

RATE = 48000
CHANNELS = 2
BLOCK = 512               # 10.7 ms per processing block
BYTES_PER_FRAME = CHANNELS * 4

LATENCY_PROFILES = {
    "Low (snappiest)": 12,
    "Balanced": 25,
    "Safe (most stable)": 50,
}
DEFAULT_LATENCY = "Balanced"


@dataclass
class EngineStatus:
    running: bool = False
    message: str = "Stopped"
    error: str | None = None
    underruns: int = 0
    load: float = 0.0          # fraction of the block period spent processing
    blocks: int = 0
    output: str | None = None  # node we are currently playing to
    output_moves: int = 0      # bumped on every retarget, so the UI can follow


class AudioEngine:
    def __init__(self) -> None:
        self.processor = EightDProcessor(RATE)
        self.status = EngineStatus()

        # Swapped wholesale by the UI thread; the worker reads it once per block.
        self._params = Params()

        self._sink = pw.VirtualSink()
        self._router = pw.Router(self._sink)
        self._rec: subprocess.Popen | None = None
        self._play: subprocess.Popen | None = None
        self._thread: threading.Thread | None = None
        self._watcher: threading.Thread | None = None
        self._stop = threading.Event()
        self._lock = threading.Lock()
        # Guards the playback process alone, so it can be swapped underneath
        # the audio thread without disturbing capture.
        self._play_lock = threading.Lock()
        self._play_gen = 0
        self._output_sink: str | None = None
        self._default_held = False
        self._latency_ms = LATENCY_PROFILES[DEFAULT_LATENCY]

    # -- parameters -------------------------------------------------------

    @property
    def params(self) -> Params:
        return self._params

    def set_params(self, params: Params) -> None:
        self._params = params

    def update(self, **kw) -> Params:
        self._params = self._params.replace(**kw)
        return self._params

    # -- telemetry --------------------------------------------------------

    @property
    def angle(self) -> float:
        return self.processor.angle

    @property
    def distance(self) -> float:
        return self.processor.distance

    def levels(self) -> tuple[float, float]:
        return self.processor.peak_l, self.processor.peak_r

    @property
    def moving(self) -> bool:
        """False while the orbit is parked because nothing is playing."""
        return self.processor.motion > 0.05

    # -- lifecycle --------------------------------------------------------

    def start(self, output_sink: str, latency_ms: int = 25) -> None:
        with self._lock:
            if self.status.running:
                return
            self._output_sink = output_sink
            self._default_held = False
            self._latency_ms = int(latency_ms)
            self.status = EngineStatus(running=True, message="Starting...",
                                       output=output_sink)
            self._stop.clear()
            try:
                self._sink.create()
                self._spawn_record()
                self._spawn_play()
                self._router.engage()
            except Exception as exc:  # noqa: BLE001 - surfaced in the UI
                self._teardown()
                self.status = EngineStatus(running=False, message="Stopped", error=str(exc))
                raise

            self.processor.reset()
            self._thread = threading.Thread(target=self._run, name="8d-audio", daemon=True)
            self._thread.start()
            # Graph housekeeping shells out to pw-dump, which is far too slow to
            # do between audio blocks, so it lives on its own thread.
            self._watcher = threading.Thread(target=self._watch, name="8d-graph",
                                             daemon=True)
            self._watcher.start()
            self.status.message = "Running"

    def stop(self) -> None:
        with self._lock:
            if not self.status.running and self._thread is None:
                return
            self._stop.set()
            thread, self._thread = self._thread, None
            watcher, self._watcher = self._watcher, None
        for worker in (thread, watcher):
            if worker is not None:
                worker.join(timeout=3.0)
        with self._lock:
            self._teardown()
            self.status = EngineStatus(running=False, message="Stopped")

    def retarget(self, output_sink: str) -> bool:
        """Move playback to another device, leaving capture untouched.

        ``pw-play`` takes its target once, at spawn, so following the desktop
        to a new output means replacing that process.  The audio thread reads
        the handle under ``_play_lock`` together with a generation counter, so
        a write that fails because we swapped the pipe is told apart from one
        that fails because the pipeline actually died.
        """
        with self._play_lock:
            if not self.status.running or not output_sink:
                return False
            if output_sink == self._output_sink:
                return False
            previous, previous_name = self._play, self._output_sink
            self._output_sink = output_sink
            try:
                self._spawn_play()
            except OSError as exc:
                self._play, self._output_sink = previous, previous_name
                self.status.error = f"Could not switch output: {exc}"
                return False
            self._play_gen += 1
            self.status.output = output_sink
            self.status.output_moves += 1
        self._close(previous)
        return True

    @staticmethod
    def _close(proc: subprocess.Popen | None) -> None:
        if proc is None:
            return
        try:
            if proc.stdin is not None and not proc.stdin.closed:
                proc.stdin.close()
        except OSError:
            pass
        try:
            proc.terminate()
            proc.wait(timeout=2.0)
        except (OSError, subprocess.TimeoutExpired):
            proc.kill()

    def _spawn_record(self) -> None:
        rec_props = (
            "{ stream.capture.sink=true "
            f"node.name={pw.CAPTURE_NODE_NAME} "
            'node.description="8D Music capture" '
            "node.passive=false }"
        )
        self._rec = subprocess.Popen(
            [
                "pw-record",
                "--target", pw.SINK_NODE_NAME,
                "-P", rec_props,
                "--rate", str(RATE),
                "--channels", str(CHANNELS),
                "--format", "f32",
                "--latency", f"{self._latency_ms}ms",
                "--raw", "-",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            bufsize=0,
        )

    def _spawn_play(self) -> None:
        play_props = (
            "{ "
            f"node.name={pw.PLAYBACK_NODE_NAME} "
            'node.description="8D Music output" '
            "media.role=Music }"
        )
        args = ["pw-play"]
        if self._output_sink:
            args += ["--target", self._output_sink]
        args += [
            "-P", play_props,
            "--rate", str(RATE),
            "--channels", str(CHANNELS),
            "--format", "f32",
            "--latency", f"{self._latency_ms}ms",
            "--raw", "-",
        ]
        self._play = subprocess.Popen(
            args, stdin=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0
        )

    def _teardown(self) -> None:
        try:
            self._router.release()
        except Exception:  # noqa: BLE001
            pass
        for proc in (self._rec, self._play):
            self._close(proc)
        self._rec = self._play = None
        self._sink.destroy()

    # -- worker -----------------------------------------------------------

    def _read_exact(self, view: memoryview) -> bool:
        assert self._rec is not None and self._rec.stdout is not None
        stream = self._rec.stdout
        filled = 0
        total = len(view)
        while filled < total:
            if self._stop.is_set():
                return False
            got = stream.readinto(view[filled:])
            if not got:
                return False
            filled += got
        return True

    def _run(self) -> None:
        raw = bytearray(BLOCK * BYTES_PER_FRAME)
        view = memoryview(raw)
        block_period = BLOCK / RATE
        load_avg = 0.0
        blocks = 0
        underruns = 0

        try:
            while not self._stop.is_set():
                if not self._read_exact(view):
                    break

                started = time.perf_counter()
                frames = np.frombuffer(raw, dtype=np.float32).reshape(-1, CHANNELS)
                out = self.processor.process(frames, self._params)
                elapsed = time.perf_counter() - started

                with self._play_lock:
                    play, generation = self._play, self._play_gen
                if play is None or play.stdin is None:
                    break
                try:
                    play.stdin.write(out.tobytes())
                except (BrokenPipeError, OSError):
                    with self._play_lock:
                        swapped = generation != self._play_gen
                    if not swapped:
                        break
                    # Playback moved to another device; this block is lost.

                blocks += 1
                load_avg += (elapsed / block_period - load_avg) * 0.05
                if elapsed > block_period:
                    underruns += 1
                if blocks % 8 == 0:
                    self.status.load = load_avg
                    self.status.blocks = blocks
                    self.status.underruns = underruns
        except Exception as exc:  # noqa: BLE001
            self.status.error = str(exc)
        finally:
            if not self._stop.is_set():
                # The pipeline died on its own -- let the UI notice and clean up.
                self.status.running = False
                self.status.message = "Audio stream ended"


    def _watch(self) -> None:
        """Keep the graph consistent while the desktop moves underneath us."""
        while not self._stop.wait(1.5):
            for step in (self._router.capture_existing_streams,
                         self._follow_default_sink,
                         self._replace_lost_output):
                try:
                    step()
                except Exception:  # noqa: BLE001 - housekeeping is best effort
                    pass

    def _follow_default_sink(self) -> None:
        """Read "the user picked another output" out of the default sink.

        Our virtual sink *is* the system default while we run, so the desktop's
        output switcher necessarily points the default somewhere else.  Treat
        that as the choice of where 8D should play, then take the default back
        so applications keep feeding us.
        """
        default = pw.default_sink_name()
        if not default:
            return
        if default == pw.SINK_NODE_NAME:
            self._default_held = True
            return
        if not self._default_held:
            # Our own claim on the default has not landed yet; a stale reading
            # here would drag playback off whichever device the user picked.
            return
        self._router.adopt_default(default)
        self.retarget(default)
        pw.set_default_sink(pw.SINK_NODE_NAME)

    def _replace_lost_output(self) -> None:
        """Fall back to another device if the one we play to is unplugged."""
        target = self._output_sink
        if not target:
            return
        available = [s.name for s in pw.list_sinks()]
        if target in available or not available:
            return
        self.retarget(available[0])
        self.status.error = None


def describe_error(engine: AudioEngine) -> str | None:
    """Best-effort explanation when the pipeline dies unexpectedly."""
    for proc, label in ((engine._rec, "pw-record"), (engine._play, "pw-play")):
        if proc is None or proc.poll() is None or proc.stderr is None:
            continue
        try:
            err = proc.stderr.read().decode(errors="replace").strip()
        except (OSError, ValueError):
            continue
        if err:
            return f"{label}: {err.splitlines()[-1]}"
    return None
