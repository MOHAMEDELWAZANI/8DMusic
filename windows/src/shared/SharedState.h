// The channel between the control window and the DSP.
//
// The APO does not run in our process -- Windows loads it into audiodg.exe --
// so parameters cannot simply be a pointer.
//
// WHY THIS IS A FILE AND NOT A `Global\` NAMED MAPPING
//
// Measured on the development machine, not assumed:
//
//   * audiodg.exe runs in session 0.  The control window runs in the user's
//     interactive session (session 8 on that box).  A `Local\` name is
//     per-session, so it can never reach audiodg -- the name has to be global.
//   * Creating anything in the `Global\` namespace requires
//     SeCreateGlobalPrivilege.  `whoami /priv` for a normal interactive user
//     does not list it.  A non-elevated 8DMusic.exe therefore *cannot* create
//     a `Global\` mapping at all; CreateFileMapping fails with
//     ERROR_ACCESS_DENIED.
//
// So the block is backed by a real file instead.  Two processes mapping the
// same file share the same pages, with no kernel namespace involved and no
// privilege required.  The installer creates the file once, with a DACL that
// lets audiodg's restricted token in.
//
// It also buys something the named mapping never had: settings survive a
// reboot, and survive audiodg being restarted, because they live on disk.
#pragma once
#include <windows.h>
#include <cstdint>
#include "../../../cpp/src/dsp/Params.h"

namespace eightd {

// Versioned: a block left by an older build must not be read as this one.
inline constexpr uint32_t kMagic   = 0x38444D32;   // "8DM2"
inline constexpr uint32_t kVersion = 2;

// %ProgramData%\8DMusic\state.bin -- resolved at runtime, never hardcoded to C:.
const wchar_t* sharedStatePath();

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

    // Set by the APO when it had to refuse the endpoint's format, so the GUI
    // can say *why* nothing is happening instead of showing a bare WAITING.
    volatile LONG formatRejected;   // 0 = fine, 1 = refused
    uint32_t rejectedChannels;
    uint32_t rejectedBits;
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
 * Opens the block, creating the backing file if `create` is set.
 *
 * Reference counted: several APO instances live in one audiodg (one per
 * endpoint), and the first one to be destroyed must not unmap the view the
 * others are still reading.
 *
 * The view is locked into memory by the caller in the APO, so the realtime
 * thread can never take a page fault on it.
 */
SharedState* openSharedState(bool create);
void closeSharedState();

} // namespace eightd
