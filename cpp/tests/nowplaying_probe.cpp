// Reports what the session bus says is playing, once a second.
#include "../src/audio/NowPlaying.h"
#include <cstdio>
#include <thread>
#include <chrono>
using namespace eightd;

static void checkClean(const char* title, std::vector<std::string> artists) {
    std::string t = title;
    cleanTrack(t, artists);
    std::printf("  \"%s\" + [", title);
    std::printf("] -> title=\"%s\" artists=[", t.c_str());
    for (size_t i = 0; i < artists.size(); ++i)
        std::printf("%s\"%s\"", i ? ", " : "", artists[i].c_str());
    std::printf("]\n");
}

int main(int argc, char** argv) {
    std::printf("-- title cleaning --\n");
    checkClean("Eminem - Love The Way You Lie ft. Rihanna (Official Video)", {"EminemVEVO"});
    checkClean("Bohemian Rhapsody (Remastered 2011)", {"Queen"});
    checkClean("Echams Ettalaa", {"Nass El Ghiwane - Topic"});
    checkClean("Song [Official Music Video]", {"A & B"});

    NowPlaying np;
    np.start();
    const int seconds = argc > 1 ? atoi(argv[1]) : 6;
    std::printf("-- bus --\n");
    for (int i = 0; i < seconds; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!np.has()) { std::printf("  t=%d: nothing playing\n", i + 1); continue; }
        const Track t = np.track();
        std::printf("  t=%d: [%s] \"%s\" by %s | %s | %.0f/%.0f s | prev=%d next=%d | mark=%s\n",
                    i + 1, t.player.c_str(), t.title.c_str(), t.artistLine().c_str(),
                    t.playing ? "playing" : "paused",
                    t.at(std::chrono::duration<double>(
                        std::chrono::steady_clock::now().time_since_epoch()).count()),
                    t.length, t.canPrev, t.canNext, t.initials().c_str());
    }
    np.stop();
    std::printf("stopped cleanly\n");
    return 0;
}
