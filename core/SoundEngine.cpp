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
    h.level = 0.06f + 0.85f * e;   // wide dynamic range so a decaying clatter fades toward silence
                                   // (the "quarter settling on a table" decrescendo)
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
    double delaySec = beat * 0.5;            // eighth-note echoes (same time for all buses for now)
    dlen_ = (int)(delaySec * sr_) + 1;
    for (int b = 0; b < kNumBuses; ++b) { dL_[b].assign(dlen_, 0.0f); dR_[b].assign(dlen_, 0.0f); dpos_[b] = 0; }

    const int cr[4] = { 1557, 1617, 1491, 1422 };  // classic Schroeder comb lengths @44.1k (Hall = base)
    const int ar[2] = { 225, 556 };
    for (int i = 0; i < 4; ++i) {
        combLen_[i]    = (int)(cr[i] * sr_ / 44100.0);
        combMaxLen_[i] = (int)(cr[i] * 1.7 * sr_ / 44100.0) + 1;   // Cathedral is the largest size
    }
    for (int i = 0; i < 2; ++i) apLen_[i] = (int)(ar[i] * sr_ / 44100.0);
    for (int b = 0; b < kNumBuses; ++b) {
        for (int i = 0; i < 4; ++i) { comb_[b][i].assign(combMaxLen_[i], 0.0f); combPos_[b][i] = 0; combLp_[b][i] = 0.0f; }
        for (int i = 0; i < 2; ++i) { ap_[b][i].assign(apLen_[i], 0.0f);  apPos_[b][i] = 0; }
        dFbLpL_[b] = dFbLpR_[b] = 0.0f;
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
    v.bus  = (h.bus < 0 || h.bus >= kNumBuses) ? 0 : h.bus;
    v.send = h.send;
    v.env = 1.0f;
    // decay to ~0.1% by the throw length: Hold in live mode (long throw), grainSeconds otherwise
    float len = liveMode_ ? ep_.holdSeconds : ep_.grainSeconds;
    if (len < 0.01f) len = 0.01f;
    v.envMul = std::exp(-6.9f / (len * (float)sr_));
    v.atkN = (int)(0.002 * sr_);   // 2 ms attack (fast, but click-free)
    v.atkPos = 0;
    v.fromInput = useInput_;        // effect path: play a grain of the input
    if (v.fromInput) v.amp *= 6.0f; // makeup: WAV grains are far below the synth's full-scale level
    v.inPos = h.inputStart;
    v.tEnv = 1.0f;                   // onset transient (input transient designer): decay over ~45 ms
    v.tMul = std::exp(-6.9f / (0.045f * (float)sr_));
    v.lp = 0.0f;
    v.lpCoef = 0.06f + 0.94f * h.brightness;  // brighter hit = less lowpass (darkness control)
}

float SoundEngine::reverbProcess(int bus, float in) {
    // Reverb type = a (size, decay, HF-damping) family on the Schroeder bank.
    float sizeScale, decayMul, damp;
    switch (ep_.reverbType[bus]) {
        case 0: sizeScale = 0.6f; decayMul = 0.85f; damp = 0.45f; break;  // Room: small, dark
        default:
        case 1: sizeScale = 1.0f; decayMul = 1.0f;  damp = 0.20f; break;  // Hall
        case 2: sizeScale = 1.7f; decayMul = 1.08f; damp = 0.15f; break;  // Cathedral: large, long
        case 3: sizeScale = 0.5f; decayMul = 1.0f;  damp = 0.0f;  break;  // Plate: small, dense, bright
    }
    float decay = ep_.reverbDecay[bus] * decayMul;
    if (decay > 0.97f) decay = 0.97f;

    float out = 0.0f;
    for (int i = 0; i < 4; ++i) {
        int len = (int)(combLen_[i] * sizeScale);
        if (len < 1) len = 1; else if (len > combMaxLen_[i]) len = combMaxLen_[i];
        int pos = combPos_[bus][i];
        if (pos >= len) pos = 0;                   // size shrank: keep the index in range
        float y = comb_[bus][i][pos];
        out += y;
        float fb = in + y * decay;
        if (damp > 0.0f) { combLp_[bus][i] += damp * (fb - combLp_[bus][i]); fb = combLp_[bus][i]; }  // HF damping
        comb_[bus][i][pos] = fb;
        if (++pos >= len) pos = 0;
        combPos_[bus][i] = pos;
    }
    out *= 0.25f;
    for (int i = 0; i < 2; ++i) {
        float bufout = ap_[bus][i][apPos_[bus][i]];
        float y = (bufout - out);                  // simple allpass-ish diffusion
        ap_[bus][i][apPos_[bus][i]] = out + bufout * 0.5f;
        out = y;
        if (++apPos_[bus][i] >= apLen_[i]) apPos_[bus][i] = 0;
    }
    return out;
}

void SoundEngine::process(float* outL, float* outR, int n, const ScheduledHit* hits, int nHits,
                          float* const* auxL, float* const* auxR) {
    int h = 0;
    for (int i = 0; i < n; ++i) {
        while (h < nHits && hits[h].offset <= i) { startVoice(hits[h].hit); ++h; }

        float dryL = 0.0f, dryR = 0.0f;
        float delInL[kNumBuses] = { 0 }, delInR[kNumBuses] = { 0 }, revIn[kNumBuses] = { 0 };
        float vbusL[kNumBuses] = { 0 }, vbusR[kNumBuses] = { 0 };   // per-bus dry throws (aux outs)
        for (int k = 0; k < NV; ++k) {
            Voice& v = v_[k];
            if (!v.active) continue;

            float s;
            if (v.fromInput) {                 // grain of the input audio (ring buffer of recent input)
                float x = 0.0f;
                if (input_ != nullptr && inLen_ > 0) x = input_[v.inPos % inLen_];   // wrap
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
            if (v.fromInput) {                       // transient designer: punch in, then settle to sustain
                float sustain = 1.0f - ep_.impact;   // impact=1 -> pure stab; 0 -> smooth swell
                a *= sustain + (1.0f - sustain) * v.tEnv;
                v.tEnv *= v.tMul;
            }
            v.env *= v.envMul;
            if (v.env < 1e-4f) v.active = false;

            float val = s * a * v.amp;
            float l = val * v.panL, r = val * v.panR;
            // input grains route ONLY to the wet busses (the continuous dry input is mixed
            // by the host/harness); exciter tones include their own dry.
            bool addDry = (!v.fromInput) || inputMix_;   // input throws are heard directly too (prominent)
            const int b = v.bus;
            vbusL[b] += l; vbusR[b] += r;                // per-bus dry throw (for the aux outputs)
            if (v.type == 0) {                 // delay peg: (dry) + wet send into its bus's delay line
                if (addDry) { dryL += l; dryR += r; }
                delInL[b] += l * v.send; delInR[b] += r * v.send;
            } else {                           // reverb peg: wet send into its bus's reverb (splash)
                if (addDry) { dryL += l * 0.3f; dryR += r * 0.3f; }
                revIn[b] += (l + r) * 0.5f * v.send;
            }
        }

        float wetL = 0.0f, wetR = 0.0f;
        for (int b = 0; b < kNumBuses; ++b) {
            float dlo = dL_[b][dpos_[b]], dro = dR_[b][dpos_[b]];
            const float fb = ep_.feedback[b];
            const int   dt = ep_.delayType[b];
            float fbL, fbR;
            if (dt == 3) {                          // Ping-pong: feedback crosses L<->R
                fbL = dro * fb; fbR = dlo * fb;
            } else {
                fbL = dlo * fb; fbR = dro * fb;
                if (dt == 1 || dt == 2) {           // Analogue / Tape: lowpass the feedback (repeats darken)
                    const float c = 0.35f;
                    dFbLpL_[b] += c * (fbL - dFbLpL_[b]); fbL = dFbLpL_[b];
                    dFbLpR_[b] += c * (fbR - dFbLpR_[b]); fbR = dFbLpR_[b];
                }
                if (dt == 2) {                      // Tape: soft saturation in the feedback (grit/compression)
                    fbL = std::tanh(fbL * 1.5f) * 0.667f;
                    fbR = std::tanh(fbR * 1.5f) * 0.667f;
                }
            }
            dL_[b][dpos_[b]] = delInL[b] + fbL;
            dR_[b][dpos_[b]] = delInR[b] + fbR;
            if (++dpos_[b] >= dlen_) dpos_[b] = 0;
            float rv = reverbProcess(b, revIn[b]);
            wetL += dlo * ep_.delayMix[b] + rv * ep_.reverbMix[b];
            wetR += dro * ep_.delayMix[b] + rv * ep_.reverbMix[b];
        }
        if (auxL != nullptr) {           // per-bus dry throws to the aux outputs (external routing)
            for (int b = 0; b < kNumBuses; ++b) {
                if (auxL[b]) auxL[b][i] = vbusL[b];
                if (auxR != nullptr && auxR[b]) auxR[b][i] = vbusR[b];
            }
        }

        if (inputMix_) {                 // input source: throws (direct) + effects, full -- the host
            outL[i] = dryL + wetL;       // crossfades this against the continuous loop via Dry/Wet
            outR[i] = dryR + wetR;
        } else {                         // synth source: internal dry/wet crossfade
            float dry = 1.0f - ep_.dryWet;
            outL[i] = dryL * dry + wetL * ep_.dryWet;
            outR[i] = dryR * dry + wetR * ep_.dryWet;
        }
    }
}
