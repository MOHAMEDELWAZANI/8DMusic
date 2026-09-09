// The channel between the control window and the DSP.
//
// The APO does not run in our process -- Windows loads it into audiodg.exe --
// so parameters cannot simply be a pointer.  This is a named shared block:
// the GUI writes settings, the APO writes back what the orbit is doing, and
// neither side ever blocks the other.
#pragma once
#include <windows.h>
#include <cstdint>
#include "../../../cpp/src/dsp/Params.h"

namespace eightd {

// Global\ so the GUI (user session) and audiodg (service session) see the same
// object.  Both names are versioned: a stale block from an older build must not
// be mistaken for this one.
inline constexpr wchar_t kSharedName[] = L"Global\\8DMusicState_v1";
inline constexpr uint32_t kMagic = 0x38444D31;   // "8DM1"

// Written by the GUI, read by the audio thread.  Torn reads are prevented with
// a seqlock rather than a mutex: the audio thread must never wait on a UI
// thread that might be descheduled mid-update.
struct SharedState {
    uint32_t magic;
    uint32_t version;

    volatile LONG seq;        // even = stable, odd = write in progress
    Params params;

    // Telemetry, the other way.  Relaxed by nature -- a torn meter is not
    // worth a lock.
    volatile LONG telemetrySeq;
    float angle;
    float distance;
    float peakL;
    float peakR;
    float motion;

    // So the GUI can say "the effect is actually running" rather than guess.
    volatile LONG heartbeat;
    uint32_t sampleRate;
    uint32_t channels;
};

/** Writer side: publish a settings change. */
inline void writeParams(SharedState* s, const Params& p) {
    InterlockedIncrement(&s->seq);          // -> odd
    MemoryBarrier();
    s->params = p;
    MemoryBarrier();
    InterlockedIncrement(&s->seq);          // -> even
}

/**
 * Reader side, called from the realtime callback.
 *
 * Bounded: if the GUI is mid-write we keep the previous snapshot rather than
 * spin.  A parameter change arriving one buffer late is inaudible; a late
 * buffer is not.
 */
inline bool readParams(const SharedState* s, Params& out) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const LONG before = s->seq;
        if (before & 1) continue;
        MemoryBarrier();
        Params copy = s->params;
        MemoryBarrier();
        if (s->seq == before) { out = copy; return true; }
    }
    return false;
}

inline void writeTelemetry(SharedState* s, float angle, float distance,
                           float peakL, float peakR, float motion) {
    InterlockedIncrement(&s->telemetrySeq);
    MemoryBarrier();
    s->angle = angle; s->distance = distance;
    s->peakL = peakL; s->peakR = peakR; s->motion = motion;
    MemoryBarrier();
    InterlockedIncrement(&s->telemetrySeq);
}

/**
 * Opens (or creates) the block.
 *
 * audiodg runs at a different integrity level to the desktop, so the mapping
 * needs a DACL that lets it in; without one the APO silently sees nothing and
 * the effect appears dead.
 */
SharedState* openSharedState(bool create);
void closeSharedState();

} // namespace eightd
