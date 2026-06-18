#include "BoardComponent.h"
#include "PluginProcessor.h"

BoardComponent::BoardComponent(PlinkoAudioProcessor& p) : proc_(p) { startTimerHz(60); }
BoardComponent::~BoardComponent() { stopTimer(); }

void BoardComponent::paint(juce::Graphics& g) {
    const BoardParams& b = proc_.board();
    auto area = getLocalBounds().toFloat().reduced(8.0f);

    // board coords: x in [0,width], y in [0,topY] with y=0 at the BOTTOM -> invert for screen
    auto sx = [&](float x) { return area.getX() + (x / b.width) * area.getWidth(); };
    auto sy = [&](float y) { return area.getY() + (1.0f - y / b.topY) * area.getHeight(); };

    g.fillAll(juce::Colour(0xff15151c));
    g.setColour(juce::Colour(0xff202028));
    g.fillRoundedRectangle(area, 6.0f);

    g.setColour(juce::Colours::dimgrey);
    g.drawLine(sx(0.0f), sy(0.0f), sx(0.0f), sy(b.topY), 2.0f);
    g.drawLine(sx(b.width), sy(0.0f), sx(b.width), sy(b.topY), 2.0f);

    float pr = (b.pegRadius / b.width) * area.getWidth();
    for (int i = 0; i < b.pegCount; ++i) {
        float cx = sx(b.pegX[i]), cy = sy(b.pegY[i]);
        juce::Colour c = (b.pegType[i] == 1) ? juce::Colour(0xff5bc0be)   // reverb = teal
                                             : juce::Colour(0xffe0a458);  // delay  = amber
        if (b.pegRest[i] > 1.0f) c = juce::Colour(0xffff4d6d);            // bumper = red
        g.setColour(c);
        g.fillEllipse(cx - pr, cy - pr, pr * 2.0f, pr * 2.0f);
    }

    float bx = sx(proc_.ballNX.load(std::memory_order_relaxed) * b.width);
    float by = sy(proc_.ballNY.load(std::memory_order_relaxed) * b.topY);
    float br = (b.ballRadius / b.width) * area.getWidth();
    g.setColour(juce::Colours::white);
    g.fillEllipse(bx - br, by - br, br * 2.0f, br * 2.0f);
}
