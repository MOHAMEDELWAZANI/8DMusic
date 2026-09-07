// Working out what the user is actually listening to.
//
// Players publish title, artist, length and position on the session bus over
// MPRIS, and hand us working transport controls with them.  The Python build
// reached the bus by running `busctl` once a second; here we speak D-Bus
// directly through sd-bus, so nothing is forked.
//
// All of it runs on its own thread -- a bus round trip in the middle of a 33 ms
// frame would be a visible stutter -- and the interface only ever reads the
// last snapshot.
#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>

struct sd_bus;

namespace eightd {

struct Track {
    std::string title;
    std::vector<std::string> artists;
    std::string album;
    std::string player;          // the app's own name for itself
    bool playing = false;
    double length = 0;           // seconds; 0 when the player does not say
    double position = 0;         // seconds, as read at `stamp`
    double stamp = 0;            // monotonic seconds when `position` was read
    bool canPrev = false, canNext = false, canSeek = false;
    bool captured = false;       // its audio is flowing through our sink
    std::string bus;
    int pid = -1;

    bool known() const { return !title.empty() || !artists.empty(); }
    std::string artistLine() const;
    // The cover mark: two names give their initials, one name its opening pair.
    std::string initials() const;
    // Position carried forward, so the bar moves between polls.
    double at(double now) const;
};

class NowPlaying {
public:
    ~NowPlaying();
    void start();
    void stop();

    bool has() const;
    Track track() const;

    // Queued for the bus thread: sd_bus is not safe to touch from here.
    void previous();
    void playPause();
    void next();

    // Names of applications whose audio we are currently carrying, refreshed by
    // the app from the PipeWire graph.
    void setCapturedApps(std::vector<std::string> apps);

private:
    void loop();
    void poll(sd_bus* bus);
    void drainCommands(sd_bus* bus);

    std::thread thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex mutex_;
    Track track_;
    bool has_ = false;
    std::vector<std::string> capturedApps_;

    std::mutex cmdMutex_;
    std::deque<std::pair<std::string, std::string>> commands_;  // bus, member
};

// Exposed for testing: turns a player's raw fields into a title and performers.
void cleanTrack(std::string& title, std::vector<std::string>& artists);

} // namespace eightd
