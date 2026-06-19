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

inline constexpr int kNumBuses = 4;   // effect buses: each peg routes to one; each has its own character

struct EngineParams {
    double bpm = 120.0;
    float feedback   [kNumBuses] = { 0.62f, 0.62f, 0.62f, 0.62f };  // per-bus delay feedback (echo decay)
    float delayMix   [kNumBuses] = { 0.5f,  0.5f,  0.5f,  0.5f  };  // per-bus wet delay level
    float reverbMix  [kNumBuses] = { 0.5f,  0.5f,  0.5f,  0.5f  };  // per-bus wet reverb level
    float reverbDecay[kNumBuses] = { 0.85f, 0.85f, 0.85f, 0.85f };  // per-bus reverb size (comb feedback)
    int   delayType  [kNumBuses] = { 0, 0, 0, 0 };  // 0 Digital, 1 Analogue, 2 Tape, 3 Ping-pong
    int   reverbType [kNumBuses] = { 1, 1, 1, 1 };  // 0 Room, 1 Hall, 2 Cathedral, 3 Plate (Hall = default)
    float dryWet     = 0.5f;   // 0 = dry (tones/input), 1 = fully wet (delay+reverb)
    float panWidth   = 1.0f;   // 1 = full width, 0 = mono-center
    int   rootMidi   = 57;     // A3
    int   pitchRangeOct = 3;   // y spans this many octaves of the scale
    float tone = 0.6f;         // global brightness bias (0 = dark, 1 = bright)
    float grainSeconds = 0.18f;// grain LENGTH (fast attack, decays to ~silence by here).
                               // Short = distinct percussive echoes; long = a continuous smear.
    float holdSeconds = 0.3f;  // Live input mode: how long a peg hit keeps feeding the live signal
                               // into its bus's delay/reverb (gate release time). More = more sound.
    float impact = 0.6f;       // input transient designer: 0 = smooth swell, 1 = pure percussive stab.
                               // Manufactures an attack accent from the input so hits feel struck.
};

struct Hit {
    float pitchHz;
    float panL, panR;
    float level;
    float brightness;
    int   type;        // 0 = delay, 1 = reverb
    int   inputStart;  // input mode: absolute input sample to start this grain from
    int   bus = 0;     // which effect bus this peg routes to (0..kNumBuses-1)
    float send = 1.0f; // per-peg wet send to the bus (0 = dry tap only, 1 = full)
};

struct ScheduledHit { int offset; Hit hit; };  // offset = sample within the block

// The §5 peg -> tap mapping.
Hit pegToTap(const Collision& c, const EngineParams& ep);

class SoundEngine {
public:
    void prepare(double sampleRate, const EngineParams& ep);
    // Update live params (does NOT resize buffers). Call per block from the host.
    void setParams(const EngineParams& ep) { ep_ = ep; }
    // Effect path: set the input audio (mono). Hits then play grains OF this input
    // instead of synthesized exciter tones. Call after prepare().
    void setInput(const float* mono, int len);
    // Live input: each peg throw reads the input FORWARD from the strike, length = Hold (vs granular's
    // short backward snapshot). Both are percussive voices; this only changes read direction + length.
    void setLiveMode(bool b) { liveMode_ = b; }
    // Input source: input voices are heard DIRECTLY (the percussive throw) as well as feeding the
    // effects, so the strikes are prominent (not just faint echoes). Off for the synth source.
    void setInputMix(bool b) { inputMix_ = b; }
    // Render n samples. `hits` must be sorted by ascending offset.
    // auxL/auxR (optional, size kNumBuses, entries may be null): per-bus DRY peg-throw outputs
    // (pre built-in effect) for external routing -- each bus's raw triggered audio.
    void process(float* outL, float* outR, int n, const ScheduledHit* hits, int nHits,
                 float* const* auxL = nullptr, float* const* auxR = nullptr);

private:
    struct Voice {
        bool   active = false;
        double ph = 0.0, dph = 0.0;
        float  amp = 0.0f, bright = 0.0f, panL = 0.0f, panR = 0.0f;
        int    type = 0;
        float  env = 0.0f, envMul = 0.0f;
        int    atkN = 0, atkPos = 0;
        bool   fromInput = false;   // read input audio instead of synthesizing
        int    inPos = 0;           // absolute read index into input
        float  lp = 0.0f, lpCoef = 1.0f;  // one-pole lowpass (brightness)
        int    bus = 0;             // effect bus this voice feeds
        float  send = 1.0f;         // wet send amount to the bus
        float  tEnv = 0.0f, tMul = 0.0f;  // onset transient envelope (input transient designer)
    };
    static constexpr int NV = 64;

    void startVoice(const Hit& h);
    float reverbProcess(int bus, float in);

    double sr_ = 44100.0;
    EngineParams ep_{};
    Voice v_[NV];

    const float* input_ = nullptr;  // effect-path source (mono)
    int   inLen_ = 0;
    bool  useInput_ = false;

    std::vector<float> dL_[kNumBuses], dR_[kNumBuses];  int dlen_ = 1, dpos_[kNumBuses]{};   // per-bus stereo delay
    float dFbLpL_[kNumBuses]{}, dFbLpR_[kNumBuses]{};   // delay feedback lowpass state (analogue/tape darken)
    std::vector<float> comb_[kNumBuses][4];  int combLen_[4]{}, combMaxLen_[4]{}, combPos_[kNumBuses][4]{};  // combs (allocated at max size)
    std::vector<float> ap_[kNumBuses][2];    int apLen_[2]{},  apPos_[kNumBuses][2]{};       // per-bus allpasses
    float combLp_[kNumBuses][4]{};            // reverb comb HF-damping state (room/hall darkening)

    bool  liveMode_ = false;   // Live = throw reads forward (Hold-length); granular = backward snapshot (grainSeconds)
    bool  inputMix_ = false;   // input source: input voices heard directly + feed effects (prominent throws)
};
