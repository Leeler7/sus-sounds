// render_demo -- physics + sound engine -> WAV.
//   render_demo <out.wav>            exciter mode (standalone tones, like the spike)
//   render_demo <out.wav> <in.wav>   effect mode  (ball echoes/reverbs your input audio)
#include "PhysicsCore.h"
#include "SoundEngine.h"
#include "Wav.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>

int main(int argc, char** argv) {
    const std::string out = (argc > 1) ? argv[1] : "engine_demo.wav";
    const std::string inPath = (argc > 2) ? argv[2] : "";
    const bool useInput = !inPath.empty();

    double sr = 44100.0;
    std::vector<float> inMono;
    if (useInput) {
        inMono = readWavMono(inPath, sr);
        if (inMono.empty()) { std::printf("could not read input WAV: %s\n", inPath.c_str()); return 1; }
    }
    const int total = useInput ? (int)inMono.size() : (int)(12.0 * sr);
    const int block = 512;

    BoardParams p;
    makeStaggeredBoard(p);
    for (int i = 0; i < p.pegCount; ++i) {
        if (i % 3 == 1) p.pegType[i] = 1;    // ~1/3 reverb pegs, spread across the board
        if (i % 9 == 0) p.pegRest[i] = 1.4f;  // bumpers
    }

    PhysicsWorld w;
    w.init(12345, p);
    EngineParams ep;
    SoundEngine eng;
    eng.prepare(sr, ep);
    if (useInput) eng.setInput(inMono.data(), (int)inMono.size());

    std::vector<float> L(total, 0.0f), R(total, 0.0f);
    std::vector<Collision> ev;
    std::vector<ScheduledHit> hits;
    size_t consumed = 0;
    int delayHits = 0, revHits = 0;
    const float dryPass = 0.7f;

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
            sh.hit.inputStart = s + off;   // grain echoes the input at the hit moment
            hits.push_back(sh);
            if (ev[k].type == 0) ++delayHits; else ++revHits;
        }
        consumed = ev.size();
        eng.process(&L[s], &R[s], n, hits.data(), (int)hits.size());

        if (useInput) {                    // continuous dry passthrough of the source
            for (int i = 0; i < n; ++i) {
                float x = inMono[s + i] * dryPass;
                L[s + i] += x;
                R[s + i] += x;
            }
        }
    }

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
    std::printf("rendered %s  (%.1fs @ %.0fHz, %s, %zu hits: %d delay + %d reverb, loops=%d)\n",
                out.c_str(), total / sr, sr, useInput ? "EFFECT/input" : "exciter",
                ev.size(), delayHits, revHits, w.loopIndex());
    return 0;
}
