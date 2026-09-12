// Runs the chain over a deterministic signal and dumps raw f32.
//
// Every platform renders this same probe; compare_f32.py compares two sets of
// dumps.  It is how "the builds produce identical audio" stays a measurement
// rather than a claim.
#include "../src/dsp/Processor.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
using namespace eightd;

int main(int argc, char** argv) {
    const char* modeName = argc > 1 ? argv[1] : "circular";
    const char* outPath  = argc > 2 ? argv[2] : "/tmp/cpp_out.f32";
    const uint32_t RATE = 48000, BLOCK = 512, SECONDS = 5;
    const uint32_t N = RATE * SECONDS;

    Mode mode = Mode::Circular;
    std::string m(modeName);
    if (m=="pingpong") mode=Mode::PingPong; else if (m=="pendulum") mode=Mode::Pendulum;
    else if (m=="linear") mode=Mode::Linear; else if (m=="figure8") mode=Mode::Figure8;
    else if (m=="spiral") mode=Mode::Spiral; else if (m=="static") mode=Mode::Static;
    else if (m=="radio") mode=Mode::Circular; else if (m=="slowed") mode=Mode::Circular;

    Params p;
    p.mode = mode; p.speed = 0.25f; p.radius = 1.2f; p.depth = 0.85f;
    p.smoothness = 0.35f; p.width = 1.0f;
    p.delayMix = 0.25f; p.delayTime = 0.28f; p.delayFeedback = 0.35f;
    p.reverbMix = 0.30f; p.reverbSize = 0.6f; p.reverbDamp = 0.45f;
    p.outputGain = 0.9f; p.pauseWhenSilent = false;   // deterministic: never park
    if (m=="radio")  { p.character = Character::Radio;  p.characterAmount = 1.f; }
    if (m=="slowed") { p.character = Character::Slowed; p.characterAmount = 0.85f; }

    // deterministic input: 220 Hz tone, slightly different per channel
    std::vector<float> in(N * 2), out(N * 2);
    for (uint32_t i = 0; i < N; ++i) {
        const double t = double(i) / RATE;
        const float s = float(0.25 * std::sin(2 * M_PI * 220 * t));
        in[2*i] = s; in[2*i + 1] = s * 0.9f;
    }

    Processor proc;
    proc.init(float(RATE), BLOCK);

    auto t0 = std::chrono::steady_clock::now();
    double worst = 0;
    uint32_t blocks = 0;
    for (uint32_t i = 0; i + BLOCK <= N; i += BLOCK) {
        auto b0 = std::chrono::steady_clock::now();
        proc.process(in.data() + i * 2, out.data() + i * 2, BLOCK, p);
        double ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - b0).count();
        if (ms > worst) worst = ms;
        ++blocks;
    }
    double wall = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - t0).count();
    double audio = double(blocks) * BLOCK / RATE;

    FILE* f = fopen(outPath, "wb");
    fwrite(out.data(), sizeof(float), size_t(blocks) * BLOCK * 2, f);
    fclose(f);

    fprintf(stderr, "%-9s rt=%5.2f%%  worstblk=%.3f ms (budget %.1f)  blocks=%u\n",
            modeName, wall / audio * 100.0, worst, BLOCK * 1000.0 / RATE, blocks);
    return 0;
}
