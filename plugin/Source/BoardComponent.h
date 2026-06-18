#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class PlinkoAudioProcessor;

// Draws the Plinko board (pegs colored by type, bumpers highlighted) and the live ball,
// reading the lock-free ball position from the processor. Repaints at 60 Hz.
class BoardComponent : public juce::Component, private juce::Timer {
public:
    explicit BoardComponent(PlinkoAudioProcessor& p);
    ~BoardComponent() override;
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override { repaint(); }
    PlinkoAudioProcessor& proc_;
};
