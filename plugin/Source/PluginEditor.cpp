#include "PluginEditor.h"

static const juce::Colour kDelay  (0xffe0a458);
static const juce::Colour kReverb (0xff5bc0be);

void PlinkoAudioProcessorEditor::addKnob(Knob& k, const char* paramID, const juce::String& name) {
    k.s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    k.s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
    k.s.setColour(juce::Slider::rotarySliderFillColourId, kReverb);
    addAndMakeVisible(k.s);
    k.l.setText(name, juce::dontSendNotification);
    k.l.setJustificationType(juce::Justification::centred);
    k.l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(k.l);
    k.att = std::make_unique<SA>(proc_.apvts, paramID, k.s);
}

void PlinkoAudioProcessorEditor::addBrush(juce::Slider& s, juce::Label& l, const juce::String& name,
                                          double lo, double hi, double def, std::function<void(double)> onChange) {
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
    s.setRange(lo, hi);
    s.setValue(def, juce::dontSendNotification);
    s.onValueChange = [&s, onChange] { onChange(s.getValue()); };
    addAndMakeVisible(s);
    l.setText(name, juce::dontSendNotification);
    l.setJustificationType(juce::Justification::centred);
    l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(l);
}

void PlinkoAudioProcessorEditor::selectBrush(int type) {
    board_.setBrushType(type);
    delayBrushBtn_.setColour(juce::TextButton::buttonColourId, type == 0 ? kDelay  : juce::Colour(0xff2a2a32));
    reverbBrushBtn_.setColour(juce::TextButton::buttonColourId, type == 1 ? kReverb : juce::Colour(0xff2a2a32));
}

PlinkoAudioProcessorEditor::PlinkoAudioProcessorEditor(PlinkoAudioProcessor& p)
    : AudioProcessorEditor(&p), proc_(p), board_(p) {
    addAndMakeVisible(board_);

    addAndMakeVisible(playStop_);
    playStop_.setButtonText(proc_.running_.load() ? "Stop" : "Start");
    playStop_.onClick = [this] {
        bool r = !proc_.running_.load();
        proc_.running_.store(r);
        playStop_.setButtonText(r ? "Stop" : "Start");
        board_.repaint();
    };
    addAndMakeVisible(clearBtn_);  clearBtn_.setButtonText("Clear");   clearBtn_.onClick  = [this] { board_.clearAllPegs(); };
    addAndMakeVisible(revertBtn_);
    revertBtn_.setButtonText("Revert");
    revertBtn_.onClick = [this] {
        for (auto* prm : proc_.getParameters())          // all knobs back to default
            prm->setValueNotifyingHost(prm->getDefaultValue());
        delayBounce_.setValue(1.0);  delaySize_.setValue(0.011);   // brushes back to default
        reverbBounce_.setValue(1.0); reverbSize_.setValue(0.011);
        selectBrush(0);
        board_.revertToDefault();                        // board back to baseline
    };

    // Shape
    addKnob(gravity_,    pid::gravity,    "Gravity");
    addKnob(ballSize_,   pid::ballSize,   "Ball Size");
    addKnob(ballBounce_, pid::ballBounce, "Ball Bounce");

    // Delay panel (left)
    addAndMakeVisible(delayBrushBtn_);
    delayBrushBtn_.setButtonText("Delay Brush");
    delayBrushBtn_.onClick = [this] { selectBrush(0); };
    addBrush(delayBounce_, delayBounceL_, "Bounce", 0.0, 2.0, 1.0,  [this](double v) { board_.setBrushBounce(0, (float)v); });
    addBrush(delaySize_,   delaySizeL_,   "Size",   0.005, 0.03, 0.011, [this](double v) { board_.setBrushSize(0, (float)v); });
    addKnob(feedback_,  pid::feedback,  "Feedback");
    addKnob(delayMix_,  pid::delayMix,  "Mix");

    // Reverb panel (right)
    addAndMakeVisible(reverbBrushBtn_);
    reverbBrushBtn_.setButtonText("Reverb Brush");
    reverbBrushBtn_.onClick = [this] { selectBrush(1); };
    addBrush(reverbBounce_, reverbBounceL_, "Bounce", 0.0, 2.0, 1.0,  [this](double v) { board_.setBrushBounce(1, (float)v); });
    addBrush(reverbSize_,   reverbSizeL_,   "Size",   0.005, 0.03, 0.011, [this](double v) { board_.setBrushSize(1, (float)v); });
    addKnob(reverbDecay_, pid::reverbDecay, "Rev Size");
    addKnob(reverbMix_,   pid::reverbMix,   "Mix");

    // Master
    addKnob(tone_,   pid::tone,     "Tone");
    addKnob(width_,  pid::panWidth, "Width");
    addKnob(dryWet_, pid::dryWet,   "Dry/Wet");
    addKnob(level_,  pid::level,    "Level");

    // push initial brush values to the board, default to the delay brush
    board_.setBrushBounce(0, 1.0f); board_.setBrushSize(0, 0.011f);
    board_.setBrushBounce(1, 1.0f); board_.setBrushSize(1, 0.011f);
    selectBrush(0);

    setResizable(true, true);
    setResizeLimits(820, 520, 1600, 1100);
    setSize(940, 620);
}

void PlinkoAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff0e0e12));
}

void PlinkoAudioProcessorEditor::layRow(juce::Rectangle<int> area, std::initializer_list<Knob*> knobs) {
    int n = (int)knobs.size();
    if (n == 0) return;
    int w = area.getWidth() / n;
    int i = 0;
    for (auto* k : knobs) {
        auto cell = area.removeFromLeft(i == n - 1 ? area.getWidth() : w);
        k->l.setBounds(cell.removeFromTop(16));
        k->s.setBounds(cell.reduced(2));
        ++i;
    }
}

void PlinkoAudioProcessorEditor::layStacked(juce::Rectangle<int> cell, juce::Label& l, juce::Component& c) {
    l.setBounds(cell.removeFromTop(15));
    c.setBounds(cell.reduced(2));
}

void PlinkoAudioProcessorEditor::resized() {
    auto r = getLocalBounds().reduced(6);

    auto top = r.removeFromTop(26);
    playStop_.setBounds(top.removeFromLeft(80).reduced(2));
    clearBtn_.setBounds(top.removeFromLeft(70).reduced(2));
    revertBtn_.setBounds(top.removeFromLeft(70).reduced(2));

    auto shape = r.removeFromTop(82);   // Shape section
    layRow(shape, { &gravity_, &ballSize_, &ballBounce_ });

    auto master = r.removeFromBottom(92); // Master section
    layRow(master, { &tone_, &width_, &dryWet_, &level_ });

    auto left  = r.removeFromLeft(150);   // Delay panel
    auto right = r.removeFromRight(150);  // Reverb panel
    board_.setBounds(r.reduced(4));

    auto layPanel = [](juce::Rectangle<int> p, juce::TextButton& btn,
                       juce::Label& l1, juce::Component& c1, juce::Label& l2, juce::Component& c2,
                       Knob& k1, Knob& k2) {
        p.reduce(4, 2);
        btn.setBounds(p.removeFromTop(24));
        p.removeFromTop(4);
        int h = p.getHeight() / 4;
        layStacked(p.removeFromTop(h), l1, c1);
        layStacked(p.removeFromTop(h), l2, c2);
        layStacked(p.removeFromTop(h), k1.l, k1.s);
        layStacked(p.removeFromTop(h), k2.l, k2.s);
    };
    layPanel(left,  delayBrushBtn_,  delayBounceL_,  delayBounce_,  delaySizeL_,  delaySize_,  feedback_,   delayMix_);
    layPanel(right, reverbBrushBtn_, reverbBounceL_, reverbBounce_, reverbSizeL_, reverbSize_, reverbDecay_, reverbMix_);
}
