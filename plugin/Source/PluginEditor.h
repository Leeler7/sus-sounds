#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "BoardComponent.h"

class PlinkoAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit PlinkoAudioProcessorEditor(PlinkoAudioProcessor&);
    ~PlinkoAudioProcessorEditor() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    void addKnob(Knob&, const char* paramID, const juce::String& name);

    PlinkoAudioProcessor& proc_;
    BoardComponent board_;
    Knob gravity_, feedback_, delayMix_, reverbMix_, reverbDecay_, tone_, width_, dryWet_, level_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlinkoAudioProcessorEditor)
};
