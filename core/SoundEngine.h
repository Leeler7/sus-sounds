// SoundEngine.h -- JUCE-free DSP for the agreed sound model (ARCHITECTURE §5.2).
//
// Each peg hit is a discrete fast-attack EVENT (an exciter grain), routed by peg type:
//   delay peg  -> dry + a tempo-synced feedback delay (rhythmic echoes)
//   reverb peg -> a Schroeder reverb (splash, long tail)
// pegToTap() is the §5 mapping (x->pan, y->scale-quantized pitch, energy->level+brightness).
//
// This is the standalone "exciter" path (plays with no input, like the Python spike). The
// input-audio path (delaying/reverbing incoming audio) is a follow-up within T7.
#pragma once
#include <vector>
#include "PhysicsCore.h"

struct EngineParams {
    double bpm = 120.0;
    float feedback   = 0.45f;  // delay feedback (echo decay)
    float delayMix   = 0.5f;   // wet delay level
    float reverbMix  = 0.4f;   // wet reverb level
    float reverbDecay= 0.82f;  // reverb comb feedback (tail length)
    float dryMix     = 0.9f;   // dry grain level
    float panWidth   = 1.0f;   // 1 = full width, 0 = mono-center
    int   rootMidi   = 57;     // A3
    int   pitchRangeOct = 3;   // y spans this many octaves of the scale
    float grainSeconds = 0.45f;// grain decay time (fast attack, this decay)
};

struct Hit {
    float pitchHz;
    float panL, panR;
    float level;
    float brightness;
    int   type;       // 0 = delay, 1 = reverb
};

struct ScheduledHit { int offset; Hit hit; };  // offset = sample within the block

// The §5 peg -> tap mapping.
Hit pegToTap(const Collision& c, const EngineParams& ep);

class SoundEngine {
public:
    void prepare(double sampleRate, const EngineParams& ep);
    // Render n samples. `hits` must be sorted by ascending offset.
    void process(float* outL, float* outR, int n, const ScheduledHit* hits, int nHits);

private:
    struct Voice {
        bool   active = false;
        double ph = 0.0, dph = 0.0;
        float  amp = 0.0f, bright = 0.0f, panL = 0.0f, panR = 0.0f;
        int    type = 0;
        float  env = 0.0f, envMul = 0.0f;
        int    atkN = 0, atkPos = 0;
    };
    static constexpr int NV = 64;

    void startVoice(const Hit& h);
    float reverbProcess(float in);

    double sr_ = 44100.0;
    EngineParams ep_{};
    Voice v_[NV];

    std::vector<float> dL_, dR_;  int dlen_ = 1, dpos_ = 0;          // stereo feedback delay
    std::vector<float> comb_[4];  int combLen_[4]{}, combPos_[4]{};  // reverb combs
    std::vector<float> ap_[2];    int apLen_[2]{},  apPos_[2]{};     // reverb allpasses
};
