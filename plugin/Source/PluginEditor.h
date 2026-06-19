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

    juce::TextButton playStop_, clearBtn_, revertBtn_, loadWavBtn_, saveBtn_;
    juce::ComboBox presetBox_;                 // factory + user presets
    juce::Array<juce::File> userPresetFiles_;  // user .plinko files (combo ids 100+)
    void rebuildPresetMenu();
    juce::ComboBox sourceBox_;   // Synth (exciter) / live Input / WAV loop
    juce::ComboBox inputModeBox_;// Granular / Live (input behavior)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> sourceAtt_, inputModeAtt_;
    std::unique_ptr<juce::FileChooser> chooser_, patchChooser_;
    void refreshFromProcessor();   // sync the editor's non-APVTS controls after a patch load

    Knob gravity_, boardWidth_, ballSize_, ballBounce_;    // Shape
    juce::TextButton delayBrushBtn_;                        // Delay panel
    juce::ComboBox delayTypeBox_;                           // per-bus delay type
    juce::Slider delayBounce_, delaySize_;  juce::Label delayBounceL_, delaySizeL_;
    juce::Slider delaySend_, delayLevel_, delayTone_;  juce::Label delaySendL_, delayLevelL_, delayToneL_;
    juce::Slider feedback_, delayMix_;       juce::Label feedbackL_, delayMixL_;     // per-bus (active bus)
    juce::TextButton reverbBrushBtn_;                       // Reverb panel
    juce::ComboBox reverbTypeBox_;                          // per-bus reverb type
    juce::Slider reverbBounce_, reverbSize_; juce::Label reverbBounceL_, reverbSizeL_;
    juce::Slider reverbSend_, reverbLevel_, reverbTone_;  juce::Label reverbSendL_, reverbLevelL_, reverbToneL_;
    juce::Slider reverbDecay_, reverbMix_;   juce::Label reverbDecayL_, reverbMixL_; // per-bus (active bus)
    Knob tone_, width_, dryWet_, level_, hold_, impact_;    // Master (Hold = capture length, Impact = punch)

    juce::ComboBox busBox_;   // active effect bus (drives the brush + the per-bus sliders above)
    int activeBus_ = 0;
    void reloadBusSliders();  // load ALL per-bus peg settings (bounce/size + effect) for the active bus

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlinkoAudioProcessorEditor)
};
