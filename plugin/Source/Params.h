#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Automatable macro parameters (board geometry stays as non-param state, ARCHITECTURE §6).
namespace pid {
    static constexpr const char* gravity     = "gravity";
    static constexpr const char* ballSize    = "ballSize";
    static constexpr const char* ballBounce  = "ballBounce";
    static constexpr const char* feedback    = "feedback";
    static constexpr const char* delayMix    = "delayMix";
    static constexpr const char* reverbMix   = "reverbMix";
    static constexpr const char* reverbDecay = "reverbDecay";
    static constexpr const char* tone        = "tone";
    static constexpr const char* panWidth    = "panWidth";
    static constexpr const char* dryWet      = "dryWet";
    static constexpr const char* level       = "level";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    auto add = [&](const char* id, const char* name, NormalisableRange<float> r, float def) {
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ id, 1 }, name, r, def));
    };
    {   // Gravity: Earth (~10) sits at NOON; far-left = very floaty, far-right = very strong.
        NormalisableRange<float> g(1.0f, 50.0f);
        g.setSkewForCentre(10.0f);
        p.push_back(std::make_unique<AudioParameterFloat>(ParameterID{ pid::gravity, 1 }, "Gravity", g, 10.0f));
    }
    add(pid::ballSize,    "Ball Size",    NormalisableRange<float>(0.015f, 0.06f),     0.03f);
    add(pid::ballBounce,  "Ball Bounce",  NormalisableRange<float>(0.0f, 2.0f),        1.0f);
    add(pid::feedback,    "Feedback",     NormalisableRange<float>(0.0f, 0.95f),       0.62f);
    add(pid::delayMix,    "Delay Mix",    NormalisableRange<float>(0.0f, 1.0f),        0.5f);
    add(pid::reverbMix,   "Reverb Mix",   NormalisableRange<float>(0.0f, 1.0f),        0.45f);
    add(pid::reverbDecay, "Reverb Size",  NormalisableRange<float>(0.5f, 0.95f),       0.85f);
    add(pid::tone,        "Tone",         NormalisableRange<float>(0.0f, 1.0f),        0.5f);  // neutral
    add(pid::panWidth,    "Width",        NormalisableRange<float>(0.0f, 1.0f),        1.0f);
    add(pid::dryWet,      "Dry/Wet",      NormalisableRange<float>(0.0f, 1.0f),        0.5f);  // half wet
    add(pid::level,       "Level",        NormalisableRange<float>(0.0f, 2.0f),        1.0f);  // unity at noon
    return { p.begin(), p.end() };
}
