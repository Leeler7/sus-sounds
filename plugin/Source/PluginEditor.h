#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include "PluginProcessor.h"
#include "BoardComponent.h"

class PlinkoAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
    explicit PlinkoAudioProcessorEditor(PlinkoAudioProcessor&);
    ~PlinkoAudioProcessorEditor() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<SA> att; };

    void addKnob(Knob&, const char* paramID, const juce::String& name);
    void addBrush(juce::Slider&, juce::Label&, const juce::String& name,
                  double lo, double hi, double def, std::function<void(double)> onChange);
    void selectBrush(int type);
    static void layRow(juce::Rectangle<int> area, std::initializer_list<Knob*> knobs);
    static void layStacked(juce::Rectangle<int> cell, juce::Label& l, juce::Component& c);

    PlinkoAudioProcessor& proc_;
    BoardComponent board_;

    juce::TextButton playStop_, clearBtn_, revertBtn_, loadWavBtn_;
    juce::ComboBox sourceBox_;   // Synth (exciter) / live Input / WAV loop
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAtt_;
    std::unique_ptr<juce::FileChooser> chooser_;

    Knob gravity_, boardWidth_, ballSize_, ballBounce_;    // Shape
    juce::TextButton delayBrushBtn_;                        // Delay panel
    juce::Slider delayBounce_, delaySize_;  juce::Label delayBounceL_, delaySizeL_;
    Knob feedback_, delayMix_;
    juce::TextButton reverbBrushBtn_;                       // Reverb panel
    juce::Slider reverbBounce_, reverbSize_; juce::Label reverbBounceL_, reverbSizeL_;
    Knob reverbDecay_, reverbMix_;
    Knob tone_, width_, dryWet_, level_;                    // Master

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlinkoAudioProcessorEditor)
};
