#include "PluginEditor.h"

void PlinkoAudioProcessorEditor::addKnob(Knob& k, const char* paramID, const juce::String& name) {
    k.slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 64, 16);
    k.slider.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff5bc0be));
    addAndMakeVisible(k.slider);

    k.label.setText(name, juce::dontSendNotification);
    k.label.setJustificationType(juce::Justification::centred);
    k.label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(k.label);

    k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc_.apvts, paramID, k.slider);
}

PlinkoAudioProcessorEditor::PlinkoAudioProcessorEditor(PlinkoAudioProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), board_(p) {
    addAndMakeVisible(board_);
    addKnob(gravity_,     pid::gravity,     "Gravity");
    addKnob(feedback_,    pid::feedback,    "Feedback");
    addKnob(delayMix_,    pid::delayMix,    "Delay");
    addKnob(reverbMix_,   pid::reverbMix,   "Reverb");
    addKnob(reverbDecay_, pid::reverbDecay, "Size");
    addKnob(tone_,        pid::tone,        "Tone");
    addKnob(width_,       pid::panWidth,    "Width");
    addKnob(level_,       pid::level,       "Level");
    setResizable(true, true);
    setResizeLimits(560, 380, 1400, 1000);
    setSize(760, 500);
}

void PlinkoAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0e0e12));
}

void PlinkoAudioProcessorEditor::resized() {
    auto r = getLocalBounds();
    auto knobRow = r.removeFromBottom(104);
    board_.setBounds(r.reduced(8));

    Knob* ks[] = { &gravity_, &feedback_, &delayMix_, &reverbMix_, &reverbDecay_, &tone_, &width_, &level_ };
    const int count = 8;
    int w = knobRow.getWidth() / count;
    for (int i = 0; i < count; ++i) {
        auto cell = knobRow.removeFromLeft(i == count - 1 ? knobRow.getWidth() : w);
        ks[i]->label.setBounds(cell.removeFromTop(18));
        ks[i]->slider.setBounds(cell.reduced(4));
    }
}
