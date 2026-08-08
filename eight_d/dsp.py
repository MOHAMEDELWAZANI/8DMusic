"""Real-time 8D spatial audio DSP.

The signal is treated as a virtual sound source orbiting the listener's head.
Position is turned into audible cues the same way a real source would be:

    ITD   interaural time difference  -- the far ear hears it a fraction later
    ILD   interaural level difference -- constant-power panning
    head shadow                       -- the far ear loses high frequencies
    front/back cue                    -- rear positions get a gentle HF dip
    distance                          -- gain, air absorption, reverb send

Everything is block-processed with numpy; the only per-sample recursions
(one-pole filters) go through scipy's lfilter, and every delay line is longer
than one block so its feedback can be evaluated a whole block at a time.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, replace

import numpy as np
from scipy.signal import lfilter

TWO_PI = 2.0 * math.pi

#: Maximum interaural time difference in seconds (~ head radius / speed of sound).
ITD_MAX_S = 0.00070

#: Silence gate.  Two thresholds so a signal hovering at the boundary cannot
#: chatter, and a hold long enough to ride out the gaps between tracks.
GATE_OPEN = 3.0e-4      # about -70 dBFS peak
GATE_CLOSE = 1.0e-4     # about -80 dBFS peak
GATE_HOLD_S = 0.7       # long enough to ride out a gap between tracks
#: Movement picks up almost at once but coasts to a stop, so the orbit is
#: already travelling on the first beat and does not jerk to a halt at the end.
MOTION_ATTACK = 0.08
MOTION_RELEASE = 0.25

MODES = (
    "circular",
    "pingpong",
    "pendulum",
    "linear",
    "figure8",
    "spiral",
    "random",
    "static",
)

#: Tone characters applied to the source before it is placed in the orbit.
CHARACTERS = ("clean", "slowed", "radio")

CHARACTER_LABELS = {
    "clean": "Clean",
    "slowed": "Slowed & sad",
    "radio": "Old radio",
}

MODE_LABELS = {
    "circular": "Circular orbit",
    "pingpong": "Ping-pong",
    "pendulum": "Pendulum",
    "linear": "Linear sweep",
    "figure8": "Figure eight",
    "spiral": "Spiral",
    "random": "Random drift",
    "static": "Static position",
}


@dataclass(frozen=True)
class Params:
    """A complete snapshot of the effect settings.

    Instances are frozen so the audio thread can grab one by reference and read
    a consistent set of values without locking.
    """

    enabled: bool = True
    mode: str = "circular"
    speed: float = 0.12        # orbits per second
    radius: float = 1.0        # virtual distance to the listener, in metres
    depth: float = 0.85        # how far through the stereo field it travels, 0..1
    smoothness: float = 0.35   # 0 = snappy, 1 = very gradual
    width: float = 1.0         # stereo width of the source material, 0..2
    direction: int = 1         # +1 clockwise, -1 counter-clockwise
    manual_angle: float = 0.0  # used by the "static" mode, radians
    pause_when_silent: bool = True   # park the orbit while nothing is playing

    character: str = "clean"        # tone applied before the spatialiser
    character_amount: float = 1.0   # how strongly, 0..1

    delay_mix: float = 0.0
    delay_time: float = 0.28   # seconds
    delay_feedback: float = 0.35

    reverb_mix: float = 0.18
    reverb_size: float = 0.6
    reverb_damp: float = 0.45

    output_gain: float = 0.9

    def replace(self, **kw) -> "Params":
        return replace(self, **kw)


PRESETS: dict[str, dict] = {
    "Classic 8D": dict(
        mode="circular", speed=0.12, radius=1.0, depth=0.9, smoothness=0.35,
        width=1.15, delay_mix=0.0, reverb_mix=0.18, reverb_size=0.6,
    ),
    "Slow Orbit": dict(
        mode="circular", speed=0.05, radius=1.4, depth=0.8, smoothness=0.6,
        width=1.1, delay_mix=0.0, reverb_mix=0.28, reverb_size=0.72,
    ),
    "Ping-Pong": dict(
        mode="pingpong", speed=0.35, radius=0.8, depth=1.0, smoothness=0.25,
        width=1.0, delay_mix=0.22, delay_time=0.22, delay_feedback=0.4,
        reverb_mix=0.12,
    ),
    "Wide Cinema": dict(
        mode="pendulum", speed=0.07, radius=1.8, depth=0.65, smoothness=0.75,
        width=1.5, delay_mix=0.12, delay_time=0.4, delay_feedback=0.3,
        reverb_mix=0.4, reverb_size=0.82, reverb_damp=0.3,
    ),
    "Subtle Motion": dict(
        mode="pendulum", speed=0.06, radius=1.1, depth=0.35, smoothness=0.8,
        width=1.05, delay_mix=0.0, reverb_mix=0.08,
    ),
    "Extreme Spin": dict(
        mode="circular", speed=0.65, radius=0.5, depth=1.0, smoothness=0.1,
        width=1.3, delay_mix=0.1, delay_time=0.15, delay_feedback=0.45,
        reverb_mix=0.2,
    ),
    "Deep Space": dict(
        mode="spiral", speed=0.09, radius=2.4, depth=0.9, smoothness=0.65,
        width=1.4, delay_mix=0.3, delay_time=0.5, delay_feedback=0.5,
        reverb_mix=0.55, reverb_size=0.88, reverb_damp=0.25,
    ),
    "Figure Eight": dict(
        mode="figure8", speed=0.15, radius=1.2, depth=0.95, smoothness=0.4,
        width=1.2, delay_mix=0.08, reverb_mix=0.2,
    ),
    "Slowed & Sad": dict(
        mode="circular", speed=0.045, radius=1.7, depth=0.85, smoothness=0.82,
        width=1.3, delay_mix=0.2, delay_time=0.6, delay_feedback=0.44,
        reverb_mix=0.52, reverb_size=0.86, reverb_damp=0.35,
        character="slowed", character_amount=0.85,
    ),
    "Old Radio": dict(
        mode="pendulum", speed=0.05, radius=1.15, depth=0.4, smoothness=0.7,
        width=0.4, delay_mix=0.0, reverb_mix=0.24, reverb_size=0.5,
        reverb_damp=0.62, character="radio", character_amount=1.0,
    ),
}


# --------------------------------------------------------------------------
# building blocks
# --------------------------------------------------------------------------


class OnePole:
    """One-pole low-pass with persistent state, evaluated a block at a time."""

    def __init__(self, channels: int = 2, cutoff: float = 5000.0, rate: int = 48000):
        self.channels = channels
        self.rate = rate
        self._zi = np.zeros((1, channels), dtype=np.float64)
        self._b = np.array([1.0], dtype=np.float64)
        self._a = np.array([1.0, 0.0], dtype=np.float64)
        self.set_cutoff(cutoff)

    def set_cutoff(self, fc: float) -> None:
        fc = float(np.clip(fc, 20.0, self.rate * 0.45))
        pole = math.exp(-TWO_PI * fc / self.rate)
        self._b = np.array([1.0 - pole], dtype=np.float64)
        self._a = np.array([1.0, -pole], dtype=np.float64)

    def process(self, x: np.ndarray) -> np.ndarray:
        y, self._zi = lfilter(self._b, self._a, x, axis=0, zi=self._zi)
        return y

    def reset(self) -> None:
        self._zi[:] = 0.0


def _onepole_mono(x: np.ndarray, pole: float, state: float) -> tuple[np.ndarray, float]:
    """y[n] = (1-pole)*x[n] + pole*y[n-1], carrying `state` across blocks."""
    y, zf = lfilter([1.0 - pole], [1.0, -pole], x, zi=[pole * state])
    return y, float(zf[0] / pole) if pole else 0.0


class DelayLine:
    """Ring buffer with per-sample fractional read positions.

    ``write`` advances an absolute sample counter; ``read`` interprets the
    requested delays relative to the block that is about to be (or has just
    been) written.  Reading before writing is safe as long as every delay is at
    least one block long -- which is how the echo and reverb stages use it.
    """

    def __init__(self, max_delay: int, channels: int = 2):
        size = 1
        while size < max_delay + 8:
            size <<= 1
        self.size = size
        self.mask = size - 1
        self.buf = np.zeros((size, channels), dtype=np.float32)
        self.channels = channels
        self.t = 0

    def reset(self) -> None:
        self.buf[:] = 0.0

    def write(self, x: np.ndarray) -> None:
        n = x.shape[0]
        idx = (self.t + np.arange(n)) & self.mask
        self.buf[idx] = x
        self.t += n

    def read(self, delays: np.ndarray, channel: int, base: int | None = None) -> np.ndarray:
        """Read `len(delays)` samples, sample n coming from `delays[n]` ago."""
        n = delays.shape[0]
        t0 = self.t if base is None else base
        pos = (t0 + np.arange(n)) - np.maximum(delays, 1.0)
        i0 = np.floor(pos)
        frac = (pos - i0).astype(np.float32)
        i0 = i0.astype(np.int64)
        col = self.buf[:, channel]
        a = col[i0 & self.mask]
        b = col[(i0 + 1) & self.mask]
        return a + (b - a) * frac


class Comb:
    """Freeverb comb filter: a delay whose feedback path is low-passed."""

    def __init__(self, size: int):
        self.buf = np.zeros(size, dtype=np.float32)
        self.size = size
        self.i = 0
        self.store = 0.0

    def reset(self) -> None:
        self.buf[:] = 0.0
        self.store = 0.0

    def process(self, x: np.ndarray, feedback: float, damp: float) -> np.ndarray:
        n = x.shape[0]
        i, size = self.i, self.size
        end = i + n
        if end <= size:
            out = self.buf[i:end].copy()
        else:
            out = np.concatenate((self.buf[i:], self.buf[: end - size]))

        filt, self.store = _onepole_mono(out, damp, self.store)
        fresh = (x + filt * feedback).astype(np.float32)

        if end <= size:
            self.buf[i:end] = fresh
        else:
            k = size - i
            self.buf[i:] = fresh[:k]
            self.buf[: end - size] = fresh[k:]
        self.i = end % size
        return out


class AllPass:
    """Freeverb all-pass diffuser (fixed 0.5 feedback)."""

    def __init__(self, size: int, feedback: float = 0.5):
        self.buf = np.zeros(size, dtype=np.float32)
        self.size = size
        self.i = 0
        self.feedback = feedback

    def reset(self) -> None:
        self.buf[:] = 0.0

    def process(self, x: np.ndarray) -> np.ndarray:
        n = x.shape[0]
        i, size = self.i, self.size
        end = i + n
        if end <= size:
            buffered = self.buf[i:end].copy()
        else:
            buffered = np.concatenate((self.buf[i:], self.buf[: end - size]))

        out = buffered - x
        fresh = (x + buffered * self.feedback).astype(np.float32)

        if end <= size:
            self.buf[i:end] = fresh
        else:
            k = size - i
            self.buf[i:] = fresh[:k]
            self.buf[: end - size] = fresh[k:]
        self.i = end % size
        return out


# Freeverb's tuning, given at 44.1 kHz and rescaled to the running sample rate.
_COMB_TUNING = (1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617)
_ALLPASS_TUNING = (556, 441, 341, 225)
_STEREO_SPREAD = 23


class Reverb:
    """Stereo Freeverb.

    Runs in sub-blocks no longer than its shortest delay so every feedback path
    can still be evaluated with whole-array arithmetic.
    """

    def __init__(self, rate: int = 48000):
        scale = rate / 44100.0
        self.combs = [
            [Comb(max(int(t * scale) + off, 32)) for t in _COMB_TUNING]
            for off in (0, _STEREO_SPREAD)
        ]
        self.allpasses = [
            [AllPass(max(int(t * scale) + off, 16)) for t in _ALLPASS_TUNING]
            for off in (0, _STEREO_SPREAD)
        ]
        shortest = min(ap.size for side in self.allpasses for ap in side)
        self.sub = max(32, shortest - 1)

    def reset(self) -> None:
        for side in self.combs:
            for c in side:
                c.reset()
        for side in self.allpasses:
            for a in side:
                a.reset()

    def process(self, x: np.ndarray, size: float, damp: float) -> np.ndarray:
        feedback = 0.7 + 0.28 * float(np.clip(size, 0.0, 1.0))
        damping = float(np.clip(damp, 0.0, 0.95)) * 0.4
        out = np.empty_like(x)
        n = x.shape[0]
        for start in range(0, n, self.sub):
            stop = min(start + self.sub, n)
            chunk = x[start:stop]
            mono = ((chunk[:, 0] + chunk[:, 1]) * 0.015).astype(np.float32)
            for ch in (0, 1):
                acc = np.zeros(stop - start, dtype=np.float32)
                for comb in self.combs[ch]:
                    acc += comb.process(mono, feedback, damping)
                for ap in self.allpasses[ch]:
                    acc = ap.process(acc)
                out[start:stop, ch] = acc
        return out


class PitchDown:
    """Varispeed pitch shifter: two delay taps drifting apart, crossfaded.

    Writing at one rate and reading slower makes the delay grow, which drops
    the pitch.  A single tap would run off the end of the buffer, so two run
    half a window apart under Hann envelopes -- those sum to exactly one at 50%
    overlap, so the seam where a tap wraps back is silent.

    This lowers pitch, which is the audible signature of "slowed" audio.  It
    cannot slow the tempo: the stream arrives in real time, so there is no way
    to stretch it without falling permanently behind.
    """

    def __init__(self, rate: int = 48000, max_block: int = 2048,
                 window_ms: float = 62.0):
        self.window = max(int(rate * window_ms / 1000.0), 256)
        self.min_delay = 2.0
        self.line = DelayLine(max_block + self.window + 64, channels=2)
        self.phase = 0.0

    def reset(self) -> None:
        self.line.reset()
        self.phase = 0.0

    def process(self, x: np.ndarray, ratio: float) -> np.ndarray:
        n = x.shape[0]
        step = (1.0 - float(np.clip(ratio, 0.5, 1.0))) / self.window
        ph = self.phase + step * np.arange(n)
        self.phase = float((self.phase + step * n) % 1.0)

        u1 = ph % 1.0
        u2 = (ph + 0.5) % 1.0
        d1 = self.min_delay + u1 * self.window
        d2 = self.min_delay + u2 * self.window
        w1 = (0.5 - 0.5 * np.cos(TWO_PI * u1)).astype(np.float32)
        w2 = (0.5 - 0.5 * np.cos(TWO_PI * u2)).astype(np.float32)

        self.line.write(x)
        base = self.line.t - n
        out = np.empty_like(x)
        for ch in (0, 1):
            out[:, ch] = (self.line.read(d1, ch, base) * w1
                          + self.line.read(d2, ch, base) * w2)
        return out.astype(np.float32, copy=False)


class RadioTone:
    """An old take heard through an AM set.

    Mono, band-limited to roughly 400 Hz - 3.2 kHz, softly saturated, with the
    slow pitch drift of a worn tape and a bed of hiss that follows the signal
    so silence stays silent.
    """

    LOW_CUT, HIGH_CUT = 450.0, 3000.0
    POLES = 3                       # 18 dB per octave on each skirt
    MAKEUP = 1.15                   # midrange near unity, plus a little back
                                    # for the bass the band-pass removed
    WOW_BASE_MS, WOW_DEPTH_MS = 12.0, 1.3

    def __init__(self, rate: int = 48000, max_block: int = 2048):
        self.rate = rate
        self.hp = [OnePole(2, self.LOW_CUT, rate) for _ in range(self.POLES)]
        self.lp = [OnePole(2, self.HIGH_CUT, rate) for _ in range(self.POLES)]
        self.hiss_hp = OnePole(2, 1100.0, rate)
        self.wow = DelayLine(max_block + int(0.05 * rate) + 64, channels=2)
        self.phase = 0.0
        self.envelope = 0.0
        self._rng = np.random.default_rng()

    def reset(self) -> None:
        for stage in (*self.hp, *self.lp, self.hiss_hp):
            stage.reset()
        self.wow.reset()
        self.phase = 0.0
        self.envelope = 0.0

    def process(self, x: np.ndarray, amount: float) -> np.ndarray:
        n = x.shape[0]
        amount = float(np.clip(amount, 0.0, 1.0))

        # a broadcast is mono
        mono = ((x[:, 0] + x[:, 1]) * 0.5).astype(np.float32)
        wet = np.repeat(mono[:, None], 2, axis=1)

        # wow and flutter: two slow, mutually prime LFOs so it never loops
        t = (self.phase + np.arange(n) / self.rate)
        self.phase = float(t[-1] + 1.0 / self.rate) if n else self.phase
        drift = (np.sin(TWO_PI * 0.6 * t) * 0.7
                 + np.sin(TWO_PI * 0.17 * t) * 0.3)
        base = self.WOW_BASE_MS * self.rate / 1000.0
        depth = self.WOW_DEPTH_MS * self.rate / 1000.0
        delays = base + drift * depth
        self.wow.write(wet)
        origin = self.wow.t - n
        for ch in (0, 1):
            wet[:, ch] = self.wow.read(delays, ch, origin)

        # The valve stage comes first: its harmonics have to be band-limited
        # too, or the set would somehow reproduce what it cannot pass.
        drive = 2.4
        wet = np.tanh(wet * drive) / math.tanh(drive)

        for stage in self.hp:
            wet = (wet - stage.process(wet)).astype(np.float32)

        # hiss, gated so silence stays silent, shaped by the same speaker
        level = float(np.abs(mono).mean()) if n else 0.0
        self.envelope += (level - self.envelope) * 0.15
        hiss = self._rng.standard_normal((n, 2)).astype(np.float32) * 0.05
        hiss = (hiss - self.hiss_hp.process(hiss)).astype(np.float32)
        wet = wet + hiss * float(np.clip(self.envelope * 12.0, 0.0, 1.0))

        for stage in self.lp:
            wet = stage.process(wet).astype(np.float32)

        wet = (wet * self.MAKEUP).astype(np.float32)   # make up the band loss
        return (x * (1.0 - amount) + wet * amount).astype(np.float32)


class Orbit:
    """Turns a movement mode into a continuous (angle, radius) trajectory.

    The angle is kept unwrapped so it can be low-pass smoothed without the
    discontinuity a wrapped angle would introduce at +/-pi.
    """

    def __init__(self, rate: int = 48000):
        self.rate = rate
        self.phase = 0.0          # 0..1 through the current cycle
        self.theta = 0.0          # unwrapped target angle
        self.theta_smooth = 0.0   # what the renderer actually follows
        self.radius = 1.0
        self.radius_smooth = 1.0
        self._omega = 0.0         # random-mode angular velocity
        self._rng = np.random.default_rng()

    def reset(self) -> None:
        self.phase = 0.0
        self.theta = self.theta_smooth = 0.0
        self.radius = self.radius_smooth = 1.0
        self._omega = 0.0

    def step(self, frames: int, p: Params,
             motion: float = 1.0) -> tuple[float, float, float, float]:
        """Advance by `frames` samples, returning (theta0, theta1, r0, r1).

        `motion` scales how much of that time the trajectory actually travels,
        so the caller can park the source while nothing is playing.  Smoothing
        still runs on real time, so a parked orbit settles onto its target
        instead of freezing part-way through an interpolation.
        """
        dt = frames / self.rate
        move = dt * min(max(motion, 0.0), 1.0)
        theta_prev = self.theta_smooth
        radius_prev = self.radius_smooth

        direction = 1.0 if p.direction >= 0 else -1.0
        self.phase = (self.phase + p.speed * move) % 1.0
        ph = self.phase * TWO_PI
        radius = p.radius

        if p.mode == "circular":
            self.theta += direction * TWO_PI * p.speed * move
        elif p.mode == "pingpong":
            # Triangle wave: constant speed across the field, hard turnarounds.
            tri = 4.0 * abs(self.phase - 0.5) - 1.0
            self.theta = direction * (math.pi / 2.0) * tri
        elif p.mode == "pendulum":
            self.theta = direction * (math.pi / 2.0) * math.sin(ph)
        elif p.mode == "linear":
            # Sweeps left to right then restarts; smoothing rounds the jump.
            self.theta = direction * math.pi * (self.phase - 0.5)
        elif p.mode == "figure8":
            self.theta = direction * (math.pi / 2.0) * math.sin(ph)
            radius = p.radius * (0.45 + 0.55 * abs(math.cos(ph)))
        elif p.mode == "spiral":
            self.theta += direction * TWO_PI * p.speed * move
            radius = p.radius * (0.4 + 0.6 * (0.5 + 0.5 * math.sin(ph / 3.0)))
        elif p.mode == "random":
            # Ornstein-Uhlenbeck angular velocity: wanders without ever jumping.
            target = self._rng.normal(0.0, TWO_PI * p.speed)
            k = 1.0 - math.exp(-move / 0.9)
            self._omega += (target - self._omega) * k
            self.theta += direction * self._omega * move
        else:  # static
            self.theta = p.manual_angle

        self.radius = radius

        # Smoothness sets the time constant the renderer uses to chase the target.
        tau = 0.004 + (p.smoothness ** 2) * 0.9
        alpha = 1.0 - math.exp(-dt / tau)
        self.theta_smooth += (self.theta - self.theta_smooth) * alpha
        self.radius_smooth += (self.radius - self.radius_smooth) * min(alpha * 2.0, 1.0)

        return theta_prev, self.theta_smooth, radius_prev, self.radius_smooth


# --------------------------------------------------------------------------
# the processor
# --------------------------------------------------------------------------


class EightDProcessor:
    """Applies the full 8D chain to interleaved stereo float32 blocks."""

    def __init__(self, rate: int = 48000, max_block: int = 2048):
        self.rate = rate
        self.orbit = Orbit(rate)

        # The ITD taps are read relative to the start of the block that was just
        # written, so the ring has to outlive a whole block plus the longest tap.
        self.max_block = int(max_block)
        self.itd_line = DelayLine(self.max_block + int(ITD_MAX_S * rate) + 64, channels=2)
        self.echo_line = DelayLine(int(1.5 * rate), channels=2)
        self.reverb = Reverb(rate)
        self.pitch = PitchDown(rate, self.max_block)
        self.radio = RadioTone(rate, self.max_block)

        self.shadow_lp = OnePole(2, 2200.0, rate)   # far-ear head shadow
        self.rear_lp = OnePole(2, 5200.0, rate)     # front/back pinna cue
        self.air_lp = OnePole(2, 18000.0, rate)     # distance / air absorption

        self._echo_time = 0.28
        self._limiter_gain = 1.0
        self._last_air_cut = -1.0
        self._quiet_for = GATE_HOLD_S
        self.playing = False        # is anything coming in?
        self.motion = 0.0           # 0 parked .. 1 travelling

        # Published for the UI's orbit visualiser.
        self.angle = 0.0
        self.distance = 1.0
        self.peak_l = 0.0
        self.peak_r = 0.0

    def reset(self) -> None:
        self.orbit.reset()
        self.itd_line.reset()
        self.echo_line.reset()
        self.reverb.reset()
        self.pitch.reset()
        self.radio.reset()
        self._quiet_for = GATE_HOLD_S
        self.playing = False
        self.motion = 0.0
        self.shadow_lp.reset()
        self.rear_lp.reset()
        self.air_lp.reset()
        self._limiter_gain = 1.0

    # -- helpers ----------------------------------------------------------

    @staticmethod
    def _ramp(a: float, b: float, n: int) -> np.ndarray:
        return np.linspace(a, b, n, endpoint=False, dtype=np.float32)

    def _gate(self, x: np.ndarray, p: Params) -> float:
        """How much the orbit should travel this block, 0..1.

        Nothing playing means nothing to place, so the source parks where it is
        rather than circling an empty room.  The two thresholds stop a signal
        sitting on the boundary from chattering, and the hold rides out the gap
        between tracks; the result is eased so the orbit coasts to a stop.
        """
        n = x.shape[0]
        dt = n / self.rate
        level = float(np.abs(x).max()) if n else 0.0

        if level >= GATE_OPEN:
            self._quiet_for = 0.0
            self.playing = True
        elif level < GATE_CLOSE:
            self._quiet_for += dt
            if self._quiet_for >= GATE_HOLD_S:
                self.playing = False
        # between the two thresholds the previous verdict stands

        target = 1.0 if (self.playing or not p.pause_when_silent) else 0.0
        tau = MOTION_ATTACK if target > self.motion else MOTION_RELEASE
        self.motion += (target - self.motion) * (1.0 - math.exp(-dt / tau))
        if self.motion < 1e-3:
            self.motion = 0.0       # otherwise it creeps forever
        return self.motion

    # -- main -------------------------------------------------------------

    def process(self, x: np.ndarray, p: Params) -> np.ndarray:
        """`x` is (frames, 2) float32; returns a new (frames, 2) float32 block."""
        n = x.shape[0]
        if n > self.max_block:
            # A host handing us bigger blocks than advertised would otherwise
            # have the ITD ring overwrite itself mid-write.
            self.max_block = n
            self.itd_line = DelayLine(n + int(ITD_MAX_S * self.rate) + 64, channels=2)
            self.pitch = PitchDown(self.rate, n)
            self.radio = RadioTone(self.rate, n)

        theta0, theta1, r0, r1 = self.orbit.step(n, p, self._gate(x, p))
        self.angle = theta1
        self.distance = r1

        if not p.enabled:
            out = np.clip(x * p.output_gain, -1.0, 1.0).astype(np.float32)
            self.peak_l = float(np.abs(out[:, 0]).max()) if n else 0.0
            self.peak_r = float(np.abs(out[:, 1]).max()) if n else 0.0
            return out

        # --- stereo width on the source material -------------------------
        mid = (x[:, 0] + x[:, 1]) * 0.5
        side = (x[:, 0] - x[:, 1]) * 0.5 * float(p.width)
        wet = np.empty_like(x)
        wet[:, 0] = mid + side
        wet[:, 1] = mid - side

        # --- tone character, applied to the source ---------------------------
        amount = float(np.clip(p.character_amount, 0.0, 1.0))
        if amount > 0.002:
            if p.character == "slowed":
                wet = self.pitch.process(wet, 1.0 - 0.18 * amount)
            elif p.character == "radio":
                wet = self.radio.process(wet, amount)

        # --- position -----------------------------------------------------
        theta = self._ramp(theta0, theta1, n)
        radius = np.clip(self._ramp(r0, r1, n), 0.25, 4.0)
        depth = float(np.clip(p.depth, 0.0, 1.0))

        lateral = np.sin(theta) * depth          # -1 fully left .. +1 fully right
        frontness = np.cos(theta)                # +1 in front, -1 behind

        # Close sources push the ears further apart perceptually.
        near = np.clip(1.0 + 0.35 / radius, 1.0, 1.6)

        # --- interaural time difference -----------------------------------
        itd = ITD_MAX_S * self.rate * 0.5 * near
        delay_l = 1.0 + itd * (1.0 + lateral)
        delay_r = 1.0 + itd * (1.0 - lateral)

        self.itd_line.write(wet)
        base = self.itd_line.t - n
        out = np.empty_like(wet)
        out[:, 0] = self.itd_line.read(delay_l, 0, base)
        out[:, 1] = self.itd_line.read(delay_r, 1, base)

        # --- interaural level difference (constant power) -------------------
        ild = np.clip(1.15 / radius, 0.35, 1.8)
        pan = np.clip(lateral * ild, -1.0, 1.0)
        phi = (math.pi / 4.0) * (1.0 + pan)
        gain_l = np.cos(phi) * math.sqrt(2.0)
        gain_r = np.sin(phi) * math.sqrt(2.0)

        # --- distance attenuation ------------------------------------------
        dist_gain = np.power(1.0 / radius, 0.6)
        out[:, 0] *= gain_l * dist_gain
        out[:, 1] *= gain_r * dist_gain

        # --- head shadow: the far ear loses highs ---------------------------
        shadow = self.shadow_lp.process(out)
        k_l = (np.clip(lateral, 0.0, 1.0) * 0.8).astype(np.float32)   # source right
        k_r = (np.clip(-lateral, 0.0, 1.0) * 0.8).astype(np.float32)  # source left
        out[:, 0] += (shadow[:, 0] - out[:, 0]) * k_l
        out[:, 1] += (shadow[:, 1] - out[:, 1]) * k_r

        # --- front/back cue --------------------------------------------------
        rear = self.rear_lp.process(out)
        k_rear = (np.clip(-frontness, 0.0, 1.0) * 0.35 * depth).astype(np.float32)
        out[:, 0] += (rear[:, 0] - out[:, 0]) * k_rear
        out[:, 1] += (rear[:, 1] - out[:, 1]) * k_rear

        # --- air absorption grows with distance ------------------------------
        mean_r = float(radius.mean())
        cut = float(np.clip(19000.0 * math.exp(-0.30 * (mean_r - 1.0)), 2500.0, 20000.0))
        if abs(cut - self._last_air_cut) > 50.0:
            self.air_lp.set_cutoff(cut)
            self._last_air_cut = cut
        if mean_r > 1.05:
            out = self.air_lp.process(out).astype(np.float32)
        else:
            self.air_lp.process(out)  # keep filter state warm

        out = out.astype(np.float32, copy=False)

        # --- ping-pong echo ---------------------------------------------------
        if p.delay_mix > 0.001:
            out = self._echo(out, p)

        # --- reverb ------------------------------------------------------------
        # Distance is part of the illusion: further away means more room in the mix.
        wet_amount = float(np.clip(p.reverb_mix, 0.0, 1.0))
        wet_amount *= float(np.clip(0.55 + 0.45 * (mean_r - 0.25) / 2.75, 0.4, 1.3))
        if wet_amount > 0.002:
            tail = self.reverb.process(out, p.reverb_size, p.reverb_damp)
            out = out * (1.0 - 0.5 * wet_amount) + tail * (wet_amount * 3.0)
            out = out.astype(np.float32, copy=False)

        # --- output stage --------------------------------------------------------
        out *= float(p.output_gain)
        out = self._limit(out)

        self.peak_l = float(np.abs(out[:, 0]).max()) if n else 0.0
        self.peak_r = float(np.abs(out[:, 1]).max()) if n else 0.0
        return out

    # -- stages ---------------------------------------------------------------

    def _echo(self, x: np.ndarray, p: Params) -> np.ndarray:
        n = x.shape[0]
        target = float(np.clip(p.delay_time, 0.04, 1.4))
        # Glide the delay time so changes bend in tape-style instead of clicking.
        prev, self._echo_time = self._echo_time, self._echo_time + (target - self._echo_time) * 0.25
        d0 = max(prev * self.rate, n + 2.0)
        d1 = max(self._echo_time * self.rate, n + 2.0)
        delays = self._ramp(d0, d1, n).astype(np.float64)

        base = self.echo_line.t
        tap_l = self.echo_line.read(delays, 0, base)
        tap_r = self.echo_line.read(delays, 1, base)

        fb = float(np.clip(p.delay_feedback, 0.0, 0.85))
        feed = np.empty_like(x)
        feed[:, 0] = x[:, 0] + tap_r * fb   # cross-fed: taps bounce ear to ear
        feed[:, 1] = x[:, 1] + tap_l * fb
        self.echo_line.write(feed)

        mix = float(np.clip(p.delay_mix, 0.0, 1.0))
        out = np.empty_like(x)
        out[:, 0] = x[:, 0] + tap_l * mix
        out[:, 1] = x[:, 1] + tap_r * mix
        return out

    def _limit(self, x: np.ndarray) -> np.ndarray:
        ceiling = 0.98
        peak = float(np.abs(x).max()) if x.size else 0.0
        target = ceiling / peak if peak > ceiling else 1.0
        start = self._limiter_gain
        if target < start:
            self._limiter_gain = target                     # fast attack
        else:
            self._limiter_gain += (target - start) * 0.05   # slow release
        ramp = self._ramp(start, self._limiter_gain, x.shape[0])[:, None]
        out = x * ramp
        np.clip(out, -1.0, 1.0, out=out)
        return out
