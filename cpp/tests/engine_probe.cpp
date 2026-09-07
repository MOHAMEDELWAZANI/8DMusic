// Headless engine check: starts the pipeline against a chosen sink, runs for a
// few seconds, reports what the audio thread saw.
#include "../src/audio/Engine.h"
#include <cstdio>
#include <thread>
#include <chrono>
using namespace eightd;

int main(int argc, char** argv) {
    pw_init(&argc, &argv);
    Engine e;
    std::string err;
    if (!e.init(err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }

    if (argc > 1 && std::string(argv[1]) == "--list") {
        for (const auto& s : e.graph().sinks())
            std::printf("%s\t%s\tserial=%u\n", s.name.c_str(), s.label().c_str(), s.serial);
        std::printf("default=%s\n", e.graph().defaultSinkName().c_str());
        e.shutdown(); return 0;
    }

    const std::string target = argc > 1 ? argv[1] : "";
    const int seconds = argc > 2 ? atoi(argv[2]) : 5;

    Params p;
    p.mode = Mode::Circular; p.speed = 0.5f; p.depth = 1.0f; p.smoothness = 0.05f;
    p.reverbMix = 0.f; p.delayMix = 0.f; p.outputGain = 1.0f;
    p.pauseWhenSilent = false;
    e.setParams(p);

    if (!e.start(target, 512, err, /*takeOverDefault=*/false)) {
        std::fprintf(stderr, "start: %s\n", err.c_str()); e.shutdown(); return 1;
    }
    std::printf("started -> %s\n", target.c_str());
    for (int i = 0; i < seconds; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto st = e.status();
        std::printf("t=%ds blocks=%llu underruns=%llu load=%.1f%% angle=%+.2f peakL=%.3f peakR=%.3f\n",
            i + 1, (unsigned long long)st.blocks, (unsigned long long)st.underruns,
            st.load * 100.0, e.processor().angle(),
            e.processor().peakL(), e.processor().peakR());
    }
    auto st = e.status();
    if (!st.error.empty()) std::printf("error: %s\n", st.error.c_str());
    e.stop();
    e.shutdown();
    std::printf("stopped cleanly\n");
    return 0;
}
