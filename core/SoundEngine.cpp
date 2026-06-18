#include "SoundEngine.h"
#include <cmath>

static const int   SCALE[] = { 0, 3, 5, 7, 10 };  // minor pentatonic
static const int   SCALE_N = 5;
static const float TWO_PI  = 6.28318530718f;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float midiToHz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

Hit pegToTap(const Collision& c, const EngineParams& ep) {
    Hit h;
    // pitch: higher peg (smaller y) -> higher note, quantized to the scale
    int ndeg = ep.pitchRangeOct * SCALE_N;
    int idx = (int)((1.0f - c.ny) * (ndeg - 1) + 0.5f);
    idx = idx < 0 ? 0 : (idx > ndeg - 1 ? ndeg - 1 : idx);
    int oct = idx / SCALE_N, deg = idx % SCALE_N;
    int semis = oct * 12 + SCALE[deg];
    h.pitchHz = midiToHz((float)ep.rootMidi + (float)semis);

    // pan from x, scaled by width around center
    float pan = clampf(c.nx, 0.0f, 1.0f);
    pan = 0.5f + (pan - 0.5f) * ep.panWidth;
    float theta = pan * 1.57079633f;
    h.panL = std::cos(theta);
    h.panR = std::sin(theta);

    // energy -> level + brightness (harder hit = louder + brighter; bumpers accent naturally)
    float e = clampf(c.energy / 5.0f, 0.0f, 1.0f);
    h.level = 0.18f + 0.60f * e;
    float baseBright = 0.20f + 0.80f * e;
    h.brightness = clampf(baseBright * (0.4f + 1.2f * ep.tone), 0.03f, 1.0f);  // global tone bias
    h.type = c.type;
    return h;
}

void SoundEngine::prepare(double sampleRate, const EngineParams& ep) {
    sr_ = sampleRate;
    ep_ = ep;
    for (auto& v : v_) v.active = false;

    double beat = 60.0 / ep.bpm;
    double delaySec = beat * 0.5;            // eighth-note echoes
    dlen_ = (int)(delaySec * sr_) + 1;
    dL_.assign(dlen_, 0.0f);
    dR_.assign(dlen_, 0.0f);
    dpos_ = 0;

    const int cr[4] = { 1557, 1617, 1491, 1422 };  // classic Schroeder comb lengths @44.1k
    const int ar[2] = { 225, 556 };
    for (int i = 0; i < 4; ++i) {
        combLen_[i] = (int)(cr[i] * sr_ / 44100.0);
        comb_[i].assign(combLen_[i], 0.0f);
        combPos_[i] = 0;
    }
    for (int i = 0; i < 2; ++i) {
        apLen_[i] = (int)(ar[i] * sr_ / 44100.0);
        ap_[i].assign(apLen_[i], 0.0f);
        apPos_[i] = 0;
    }
}

void SoundEngine::setInput(const float* mono, int len) {
    input_ = mono;
    inLen_ = len;
    useInput_ = (mono != nullptr && len > 0);
}

void SoundEngine::startVoice(const Hit& h) {
    int idx = -1;
    float minEnv = 1e9f;
    for (int i = 0; i < NV; ++i) {
        if (!v_[i].active) { idx = i; break; }
        if (v_[i].env < minEnv) { minEnv = v_[i].env; idx = i; }  // steal quietest
    }
    Voice& v = v_[idx];
    v.active = true;
    v.ph = 0.0;
    v.dph = TWO_PI * h.pitchHz / sr_;
    v.amp = h.level;
    v.bright = h.brightness;
    v.panL = h.panL;
    v.panR = h.panR;
    v.type = h.type;
    v.env = 1.0f;
    // decay to ~0.1% by grainSeconds, so grainSeconds is the real grain LENGTH
    v.envMul = std::exp(-6.9f / (ep_.grainSeconds * (float)sr_));
    v.atkN = (int)(0.002 * sr_);   // 2 ms attack (fast, but click-free)
    v.atkPos = 0;
    v.fromInput = useInput_;        // effect path: play a grain of the input
    v.inPos = h.inputStart;
    v.lp = 0.0f;
    v.lpCoef = 0.06f + 0.94f * h.brightness;  // brighter hit = less lowpass (darkness control)
}

float SoundEngine::reverbProcess(float in) {
    float out = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float y = comb_[i][combPos_[i]];
        out += y;
        comb_[i][combPos_[i]] = in + y * ep_.reverbDecay;
        if (++combPos_[i] >= combLen_[i]) combPos_[i] = 0;
    }
    out *= 0.25f;
    for (int i = 0; i < 2; ++i) {
        float bufout = ap_[i][apPos_[i]];
        float y = -in * 0.0f + (bufout - out);     // simple allpass-ish diffusion
        ap_[i][apPos_[i]] = out + bufout * 0.5f;
        out = y;
        if (++apPos_[i] >= apLen_[i]) apPos_[i] = 0;
    }
    return out;
}

void SoundEngine::process(float* outL, float* outR, int n, const ScheduledHit* hits, int nHits) {
    int h = 0;
    for (int i = 0; i < n; ++i) {
        while (h < nHits && hits[h].offset <= i) { startVoice(hits[h].hit); ++h; }

        float dryL = 0.0f, dryR = 0.0f, delInL = 0.0f, delInR = 0.0f, revIn = 0.0f;
        for (int k = 0; k < NV; ++k) {
            Voice& v = v_[k];
            if (!v.active) continue;

            float s;
            if (v.fromInput) {                 // grain of the input audio
                float x = (v.inPos >= 0 && v.inPos < inLen_) ? input_[v.inPos] : 0.0f;
                ++v.inPos;
                v.lp += v.lpCoef * (x - v.lp); // one-pole lowpass (brightness)
                s = v.lp;
            } else {                           // synthesized exciter tone
                float p = (float)v.ph;
                s = std::sin(p)
                  + 0.50f * v.bright * std::sin(2.0f * p)
                  + 0.28f * v.bright * std::sin(3.0f * p)
                  + 0.14f * v.bright * std::sin(4.0f * p);
                v.ph += v.dph;
                if (v.ph > TWO_PI) v.ph -= TWO_PI;
            }

            float a = v.env;
            if (v.atkPos < v.atkN) { a *= (float)v.atkPos / (float)v.atkN; ++v.atkPos; }
            v.env *= v.envMul;
            if (v.env < 1e-4f) v.active = false;

            float val = s * a * v.amp;
            float l = val * v.panL, r = val * v.panR;
            // input grains route ONLY to the wet busses (the continuous dry input is mixed
            // by the host/harness); exciter tones include their own dry.
            bool addDry = !v.fromInput;
            if (v.type == 0) {                 // delay peg: (dry) + into the delay line
                if (addDry) { dryL += l; dryR += r; }
                delInL += l; delInR += r;
            } else {                           // reverb peg: into the reverb (splash)
                if (addDry) { dryL += l * 0.3f; dryR += r * 0.3f; }
                revIn += (l + r) * 0.5f;
            }
        }

        float dlo = dL_[dpos_], dro = dR_[dpos_];
        dL_[dpos_] = delInL + dlo * ep_.feedback;
        dR_[dpos_] = delInR + dro * ep_.feedback;
        if (++dpos_ >= dlen_) dpos_ = 0;

        float rv = reverbProcess(revIn);

        float wetL = dlo * ep_.delayMix + rv * ep_.reverbMix;
        float wetR = dro * ep_.delayMix + rv * ep_.reverbMix;
        float dry = 1.0f - ep_.dryWet;
        outL[i] = dryL * dry + wetL * ep_.dryWet;
        outR[i] = dryR * dry + wetR * ep_.dryWet;
    }
}
