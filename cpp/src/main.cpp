#include "ui/App.h"
#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char** argv) {
    pw_init(&argc, &argv);

    const bool check = argc > 1 && (!std::strcmp(argv[1], "--check") ||
                                    !std::strcmp(argv[1], "--list-devices"));
    if (check) {
        eightd::Engine engine;
        std::string err;
        if (!engine.init(err)) {
            std::fprintf(stderr, "PipeWire is not reachable: %s\n", err.c_str());
            return 1;
        }
        const auto sinks = engine.graph().sinks();
        std::printf("PipeWire: OK\nAudio outputs (%zu):\n", sinks.size());
        for (const auto& s : sinks)
            std::printf("  - %s   [%s]\n", s.label().c_str(), s.name.c_str());
        const auto def = engine.graph().defaultSinkName();
        if (!def.empty()) std::printf("Current default: %s\n", def.c_str());
        engine.shutdown();
        return sinks.empty() ? 1 : 0;
    }
    // Renders every page and both presentations to PNGs, so the build can be
    // put side by side with the design file.
    if (argc > 2 && !std::strcmp(argv[1], "--shot")) {
        eightd::App app;
        std::string error;
        if (!app.shoot(argv[2], error)) {
            std::fprintf(stderr, "Could not render: %s\n", error.c_str());
            return 1;
        }
        std::printf("Wrote the design pages to %s\n", argv[2]);
        return 0;
    }
    if (argc > 1 && !std::strcmp(argv[1], "--version")) {
        std::printf("8D Music (C++) 1.0.0\n");
        return 0;
    }

    eightd::App app;
    std::string error;
    if (!app.run(error)) {
        std::fprintf(stderr, "8D Music could not start: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
