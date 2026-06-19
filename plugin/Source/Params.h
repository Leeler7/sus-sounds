#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Automatable macro parameters (board geometry stays as non-param state, ARCHITECTURE §6).
namespace pid {
    static constexpr const char* gravity     = "gravity";
    static constexpr const char* boardWidth  = "boardWidth";
    static constexpr const char* ballSize    = "ballSize";
    static constexpr const char* ballBounce  = "ballBounce";
    // feedback / delayMix / reverbMix / reverbDecay are now PER-BUS (non-APVTS atomics in the processor)
    static constexpr const char* tone        = "tone";
    static constexpr const char* panWidth    = "panWidth";
    static constexpr const char* dryWet      = "dryWet";
    static constexpr const char* level       = "level";
    static constexpr const char* source      = "source";   // 0 = Synth (exciter), 1 = Input, 2 = WAV loop
    static constexpr const char* inputMode   = "inputMode"; // 0 = Granular (snapshots), 1 = Live (gated)
    static constexpr const char* hold        = "hold";      // Live mode: gate hold/release time (seconds)
    static constexpr const char* impact      = "impact";    // input transient designer (0 smooth .. 1 stab)
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add = [&](const char* id, const char* name, NormalisableRange<float> r, float def) {
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, r, def));
    };
    // Same, but skews the range so the default value sits at NOON (slider centre).
    auto addSkew = [&](const char* id, const char* name, float lo, float hi, float def) {
        NormalisableRange<float> r(lo, hi);
        r.setSkewForCentre(def);
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, r, def));
    };
    {   // Gravity: Earth (~10) sits at NOON; far-left = very floaty, far-right = very strong.
        NormalisableRange<float> g(1.0f, 50.0f);
        g.setSkewForCentre(10.0f);
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ pid::gravity, 1 }, "Gravity", g, 10.0f));
    }
    addSkew(pid::boardWidth, "Board Width", 0.6f, 1.8f, 1.0f);    // default width at noon
    addSkew(pid::ballSize, "Ball Size", 0.015f, 0.06f, 0.045f);   // default ball size at noon
    add(pid::ballBounce,  "Ball Bounce",  NormalisableRange<float>(0.0f, 2.0f),        1.0f);  // 1 = noon
    add(pid::tone,        "Tone",         NormalisableRange<float>(0.0f, 1.0f),        0.5f);  // noon
    add(pid::panWidth,    "Width",        NormalisableRange<float>(0.0f, 1.0f),        0.5f);  // noon (modulate wider/narrower)
    add(pid::dryWet,      "Dry/Wet",      NormalisableRange<float>(0.0f, 1.0f),        0.5f);  // half wet
    addSkew(pid::level, "Level", 0.0f, 4.0f, 1.0f);   // unity at noon, up to 4x (~+12 dB headroom)
    addSkew(pid::hold,  "Hold",  0.05f, 2.0f, 0.3f);  // Live-mode gate hold time; noon ~0.3s
    add(pid::impact, "Impact", NormalisableRange<float>(0.0f, 1.0f), 0.6f);  // input transient punch
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ pid::source, 1 }, "Source",
                                                       StringArray{ "Synth", "Input", "WAV Loop" }, 0));
    p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{ pid::inputMode, 1 }, "Input Mode",
                                                       StringArray{ "Granular", "Live" }, 1));
    return { p.begin(), p.end() };
}
