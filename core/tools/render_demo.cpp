// render_demo -- wires PhysicsWorld + SoundEngine and renders a WAV you can listen to.
// Proves the real C++ engine makes sound from the deterministic physics (the milestone
// the Python spike opened, now in the actual engine). Usage: render_demo [out.wav]
#include "PhysicsCore.h"
#include "SoundEngine.h"
#include "Wav.h"
#include <vector>
#include <cstdio>
#include <cmath>
#include <string>

int main(int argc, char** argv) {
    const std::string out = (argc > 1) ? argv[1] : "engine_demo.wav";
    const double sr = 44100.0;
    const int total = (int)(12.0 * sr);   // 12 seconds
    const int block = 512;

    // Board: lower half = reverb pegs (splash), upper half = delay pegs (rhythm),
    // every 9th peg a bumper (accent + keeps the ball lively).
    BoardParams p;
    makeStaggeredBoard(p);
    for (int i = 0; i < p.pegCount; ++i) {
        if (i % 3 == 1) p.pegType[i] = 1;   // ~1/3 reverb pegs, spread so the ball hits a mix
        if (i % 9 == 0) p.pegRest[i] = 1.4f; // bumpers
    }

    PhysicsWorld w;
    w.init(12345, p);
    EngineParams ep;
    SoundEngine eng;
    eng.prepare(sr, ep);

    std::vector<float> L(total, 0.0f), R(total, 0.0f);
    std::vector<Collision> ev;
    std::vector<ScheduledHit> hits;
    size_t consumed = 0;
    int delayHits = 0, revHits = 0;

    for (int s = 0; s < total; s += block) {
        int n = (block < total - s) ? block : (total - s);
        double blockStartT = (double)s / sr;
        w.advance((double)n / sr, ev);

        hits.clear();
        for (size_t k = consumed; k < ev.size(); ++k) {
            int off = (int)((ev[k].t - blockStartT) * sr + 0.5);
            if (off < 0) off = 0;
            if (off >= n) off = n - 1;
            ScheduledHit sh;
            sh.offset = off;
            sh.hit = pegToTap(ev[k], ep);
            hits.push_back(sh);
            if (ev[k].type == 0) ++delayHits; else ++revHits;
        }
        consumed = ev.size();
        eng.process(&L[s], &R[s], n, hits.data(), (int)hits.size());
    }

    // normalize
    float peak = 0.0f;
    for (int i = 0; i < total; ++i) {
        peak = std::fmax(peak, std::fabs(L[i]));
        peak = std::fmax(peak, std::fabs(R[i]));
    }
    if (peak > 0.0f) {
        float g = 0.89f / peak;
        for (int i = 0; i < total; ++i) { L[i] *= g; R[i] *= g; }
    }

    writeWavStereo(out, L, R, sr);
    std::printf("rendered %s  (%.1fs, %zu hits: %d delay + %d reverb, loops=%d, peak=%.3f)\n",
                out.c_str(), total / sr, ev.size(), delayHits, revHits, w.loopIndex(), peak);
    return 0;
}
