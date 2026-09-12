// Working out what the user is actually listening to.
//
// The Linux build reads MPRIS over D-Bus and Android reads MediaController;
// Windows has the same idea in GlobalSystemMediaTransportControlsSessionManager
// -- the thing behind the flyout the volume keys pop up. Same fields, same
// snapshot-under-a-lock shape, so cpp/src/audio/NowPlaying.h's Track survives
// almost unchanged.
//
// It polls on its own thread. A WinRT call in the middle of a 33 ms frame would
// be a visible stutter in the orbit, so the interface only ever reads the last
// snapshot.
//
// WHAT THIS CANNOT SEE
//
// Only apps that publish a media session appear. Spotify does; Chromium
// browsers do, so Brave and Chrome report whatever the page's Media Session API
// declares (YouTube gives title and channel). Discord does not. Games do not.
// Anything that simply opens WASAPI and pushes PCM is invisible here, and will
// read as "Nothing playing" while it is plainly audible. That is a limit of the
// API, not a bug to fix.
//
// It also reports what the *system* is playing, which is not necessarily what
// is going through our APO -- a player on another endpoint still shows up.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

namespace eightd {

struct Track {
    std::wstring title;
    std::vector<std::wstring> artists;
    std::wstring album;
    std::wstring player;         // the app, from SourceAppUserModelId
    bool   playing = false;
    double length  = 0;          // seconds; 0 when the player does not say
    double position = 0;         // seconds, as read at `stamp`
    double stamp   = 0;          // monotonic seconds when `position` was read

    bool known() const { return !title.empty() || !artists.empty(); }

    std::wstring artistLine() const;
    // The cover mark: two names give their initials, one name its opening pair.
    std::wstring initials() const;
    // Position advanced to `now`, so the bar moves smoothly between polls
    // instead of stepping twice a second.
    double at(double now) const;
};

class NowPlaying {
public:
    ~NowPlaying();
    void start();
    void stop();

    bool  has() const { return has_.load(std::memory_order_relaxed); }
    Track track() const;

private:
    void run();

    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> has_{false};
    mutable std::mutex mutex_;
    Track track_;
};

} // namespace eightd
