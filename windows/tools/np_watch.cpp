// Prints what the Windows media session actually reports, once a second.
//
// The now-playing bar is drawn from three numbers -- Position, PlaybackStatus
// and LastUpdatedTime -- and when it sits still the question is which of them is
// lying. This answers that without having to reason about it: run it while
// something plays and watch the columns.
//
//   np_watch.exe [seconds]
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace winrt::Windows::Media::Control;

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? std::atoi(argv[1]) : 15;
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    GlobalSystemMediaTransportControlsSessionManager manager{ nullptr };
    try {
        manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    } catch (...) {
        std::printf("no media platform\n");
        return 2;
    }

    std::printf("%-6s %-9s %-10s %-10s %-10s %s\n",
                "t", "status", "position", "end", "updated", "title");
    for (int i = 0; i < seconds; ++i) {
        try {
            auto session = manager.GetCurrentSession();
            if (!session) {
                std::printf("%-6d (no session)\n", i);
            } else {
                const auto info = session.GetPlaybackInfo();
                const bool playing = info.PlaybackStatus() ==
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                const auto tl = session.GetTimelineProperties();
                const double pos = std::chrono::duration<double>(tl.Position()).count();
                const double end = std::chrono::duration<double>(tl.EndTime()).count();
                // How long ago the player last told Windows where it was. If the
                // position is stale, this is how stale.
                const double age = std::chrono::duration<double>(
                    winrt::clock::now() - tl.LastUpdatedTime()).count();
                const auto props = session.TryGetMediaPropertiesAsync().get();
                std::printf("%-6d %-9s %-10.2f %-10.2f %-10.2f %ls\n",
                            i, playing ? "playing" : "paused", pos, end, age,
                            props.Title().c_str());
            }
        } catch (...) {
            std::printf("%-6d (threw)\n", i);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    return 0;
}
