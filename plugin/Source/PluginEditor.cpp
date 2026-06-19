#include "PluginEditor.h"
#include "Wav.h"
#include <cmath>
#include <utility>
#include <initializer_list>

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

void PlinkoAudioProcessorEditor::reloadBusSliders() {
    // effect character (from the processor's per-bus atomics)
    feedback_.setValue(proc_.busFeedback_[activeBus_].load(),       juce::dontSendNotification);
    delayMix_.setValue(proc_.busDelayMix_[activeBus_].load(),       juce::dontSendNotification);
    reverbDecay_.setValue(proc_.busReverbDecay_[activeBus_].load(), juce::dontSendNotification);
    reverbMix_.setValue(proc_.busReverbMix_[activeBus_].load(),     juce::dontSendNotification);
    // brush peg profile (read from the board's per-bus presets, the single source of truth)
    delayBounce_.setValue(proc_.busBounce(activeBus_, 0),  juce::dontSendNotification);
    delaySize_.setValue(proc_.busSize(activeBus_, 0),      juce::dontSendNotification);
    delaySend_.setValue(proc_.busSend(activeBus_, 0),      juce::dontSendNotification);
    delayLevel_.setValue(proc_.busLevel(activeBus_, 0),    juce::dontSendNotification);
    delayTone_.setValue(proc_.busTone(activeBus_, 0),      juce::dontSendNotification);
    reverbBounce_.setValue(proc_.busBounce(activeBus_, 1), juce::dontSendNotification);
    reverbSize_.setValue(proc_.busSize(activeBus_, 1),     juce::dontSendNotification);
    reverbSend_.setValue(proc_.busSend(activeBus_, 1),     juce::dontSendNotification);
    reverbLevel_.setValue(proc_.busLevel(activeBus_, 1),   juce::dontSendNotification);
    reverbTone_.setValue(proc_.busTone(activeBus_, 1),     juce::dontSendNotification);
    delayTypeBox_.setSelectedId(proc_.busDelayType_[activeBus_].load() + 1,   juce::dontSendNotification);
    reverbTypeBox_.setSelectedId(proc_.busReverbType_[activeBus_].load() + 1, juce::dontSendNotification);
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
        for (int b = 0; b < kNumBuses; ++b) {            // per-bus effects back to default
            proc_.busFeedback_[b] = 0.62f; proc_.busDelayMix_[b] = 0.5f;
            proc_.busReverbDecay_[b] = 0.85f; proc_.busReverbMix_[b] = 0.5f;
            proc_.busDelayType_[b] = 0; proc_.busReverbType_[b] = 1;   // Digital / Hall
        }
        proc_.resetBusPresets();                        // per-bus bounce/size back to default
        busBox_.setSelectedId(1, juce::dontSendNotification); activeBus_ = 0;
        reloadBusSliders();
        selectBrush(0);
        board_.setBrushBus(0);
        board_.revertToDefault();                        // board back to baseline
    };

    addAndMakeVisible(sourceBox_);
    sourceBox_.addItem("Synth", 1);
    sourceBox_.addItem("Input", 2);
    sourceBox_.addItem("WAV Loop", 3);
    sourceAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc_.apvts, pid::source, sourceBox_);

    addAndMakeVisible(inputModeBox_);
    inputModeBox_.addItem("Granular", 1);
    inputModeBox_.addItem("Live", 2);
    inputModeAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        proc_.apvts, pid::inputMode, inputModeBox_);

    addAndMakeVisible(loadWavBtn_);
    loadWavBtn_.setButtonText("Load WAV");
    loadWavBtn_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load a WAV loop to test the input path",
                                                       juce::File{}, "*.wav");
        auto self = juce::Component::SafePointer<PlinkoAudioProcessorEditor>(this);
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [self](const juce::FileChooser& fc) {
                if (self == nullptr) return;
                auto file = fc.getResult();
                if (file == juce::File{}) return;
                double srcRate = 44100.0;
                auto mono = readWavMono(file.getFullPathName().toStdString(), srcRate);
                if (mono.empty()) return;
                const double hostRate = self->proc_.getSampleRate();
                std::vector<float> out;
                if (hostRate <= 0.0 || std::fabs(srcRate - hostRate) < 1.0) {
                    out = std::move(mono);                       // same rate -> use as-is
                } else {                                         // linear resample to the host rate
                    const double ratio = srcRate / hostRate;
                    const int outN = juce::jmax(1, (int)((double)mono.size() / ratio));
                    out.resize((size_t)outN);
                    for (int i = 0; i < outN; ++i) {
                        double sp = i * ratio; int i0 = (int)sp; double fr = sp - i0;
                        float a = mono[(size_t)i0];
                        float b = (i0 + 1 < (int)mono.size()) ? mono[(size_t)i0 + 1] : a;
                        out[(size_t)i] = (float)(a + (b - a) * fr);
                    }
                }
                self->proc_.loadWavLoop(std::move(out));
                self->sourceBox_.setSelectedId(3, juce::sendNotificationSync);  // switch to WAV Loop
            });
    };

    // Shape
    addKnob(gravity_,    pid::gravity,    "Gravity");
    addKnob(boardWidth_, pid::boardWidth, "Width");
    addKnob(ballSize_,   pid::ballSize,   "Ball Size");
    addKnob(ballBounce_, pid::ballBounce, "Ball Bounce");

    // Delay panel (left)
    addAndMakeVisible(delayBrushBtn_);
    delayBrushBtn_.setButtonText("Delay Brush");
    delayBrushBtn_.onClick = [this] { selectBrush(0); };
    addAndMakeVisible(delayTypeBox_);
    delayTypeBox_.addItem("Digital", 1); delayTypeBox_.addItem("Analogue", 2);
    delayTypeBox_.addItem("Tape", 3);    delayTypeBox_.addItem("Ping-pong", 4);
    delayTypeBox_.onChange = [this] { proc_.busDelayType_[activeBus_].store(delayTypeBox_.getSelectedId() - 1); };
    addBrush(delayBounce_, delayBounceL_, "Bounce", 0.0, 2.0, 1.0,  [this](double v) { proc_.setBusBounce(activeBus_, 0, (float)v); });
    addBrush(delaySize_,   delaySizeL_,   "Size",   0.005, 0.06, 0.0225, [this](double v) { proc_.setBusSize(activeBus_, 0, (float)v); });
    delaySize_.setSkewFactorFromMidPoint(0.0225);   // default peg size at noon (matches the board)
    addBrush(delaySend_,  delaySendL_,  "Send",  0.0, 2.0, 1.0, [this](double v) { proc_.setBusSend(activeBus_, 0, (float)v); });
    addBrush(delayLevel_, delayLevelL_, "Level", 0.0, 4.0, 1.0, [this](double v) { proc_.setBusLevel(activeBus_, 0, (float)v); });
    addBrush(delayTone_,  delayToneL_,  "Tone",  0.0, 1.0, 0.5, [this](double v) { proc_.setBusTone(activeBus_, 0, (float)v); });

    // Per-bus effect sliders (edit the ACTIVE bus; non-APVTS, written straight to the processor).
    auto setupBus = [this](juce::Slider& s, juce::Label& l, const juce::String& name, double lo, double hi, double mid) {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 56, 14);
        s.setRange(lo, hi);
        if (mid > lo && mid < hi) s.setSkewFactorFromMidPoint(mid);
        addAndMakeVisible(s);
        l.setText(name, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centred);
        l.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(l);
    };
    setupBus(feedback_, feedbackL_, "Feedback", 0.0, 0.95, 0.62);
    feedback_.onValueChange = [this] { proc_.busFeedback_[activeBus_].store((float)feedback_.getValue()); };
    setupBus(delayMix_, delayMixL_, "Mix", 0.0, 2.0, 0.0);
    delayMix_.onValueChange = [this] { proc_.busDelayMix_[activeBus_].store((float)delayMix_.getValue()); };

    // Reverb panel (right)
    addAndMakeVisible(reverbBrushBtn_);
    reverbBrushBtn_.setButtonText("Reverb Brush");
    reverbBrushBtn_.onClick = [this] { selectBrush(1); };
    addAndMakeVisible(reverbTypeBox_);
    reverbTypeBox_.addItem("Room", 1);      reverbTypeBox_.addItem("Hall", 2);
    reverbTypeBox_.addItem("Cathedral", 3); reverbTypeBox_.addItem("Plate", 4);
    reverbTypeBox_.onChange = [this] { proc_.busReverbType_[activeBus_].store(reverbTypeBox_.getSelectedId() - 1); };
    addBrush(reverbBounce_, reverbBounceL_, "Bounce", 0.0, 2.0, 1.0,  [this](double v) { proc_.setBusBounce(activeBus_, 1, (float)v); });
    addBrush(reverbSize_,   reverbSizeL_,   "Size",   0.005, 0.06, 0.0225, [this](double v) { proc_.setBusSize(activeBus_, 1, (float)v); });
    reverbSize_.setSkewFactorFromMidPoint(0.0225);   // default peg size at noon (matches the board)
    addBrush(reverbSend_,  reverbSendL_,  "Send",  0.0, 2.0, 1.0, [this](double v) { proc_.setBusSend(activeBus_, 1, (float)v); });
    addBrush(reverbLevel_, reverbLevelL_, "Level", 0.0, 4.0, 1.0, [this](double v) { proc_.setBusLevel(activeBus_, 1, (float)v); });
    addBrush(reverbTone_,  reverbToneL_,  "Tone",  0.0, 1.0, 0.5, [this](double v) { proc_.setBusTone(activeBus_, 1, (float)v); });
    setupBus(reverbDecay_, reverbDecayL_, "Rev Size", 0.5, 0.95, 0.85);
    reverbDecay_.onValueChange = [this] { proc_.busReverbDecay_[activeBus_].store((float)reverbDecay_.getValue()); };
    setupBus(reverbMix_, reverbMixL_, "Mix", 0.0, 2.0, 0.0);
    reverbMix_.onValueChange = [this] { proc_.busReverbMix_[activeBus_].store((float)reverbMix_.getValue()); };

    addAndMakeVisible(busBox_);
    for (int b = 0; b < kNumBuses; ++b) busBox_.addItem("Bus " + juce::String(b + 1), b + 1);
    busBox_.onChange = [this] {
        activeBus_ = juce::jlimit(0, kNumBuses - 1, busBox_.getSelectedId() - 1);
        reloadBusSliders();
        board_.setBrushBus(activeBus_);
        board_.repaint();
    };
    busBox_.setSelectedId(1, juce::dontSendNotification);
    reloadBusSliders();
    board_.setBrushBus(0);

    // Master
    addKnob(tone_,   pid::tone,     "Tone");
    addKnob(width_,  pid::panWidth, "Stereo");
    addKnob(dryWet_, pid::dryWet,   "Dry/Wet");
    addKnob(level_,  pid::level,    "Level");
    addKnob(hold_,   pid::hold,     "Grain");
    addKnob(impact_, pid::impact,   "Impact");

    // default to the delay brush (bus presets are initialized in BoardComponent)
    selectBrush(0);

    addAndMakeVisible(saveBtn_);
    saveBtn_.setButtonText("Save");
    saveBtn_.onClick = [this] {
        patchChooser_ = std::make_unique<juce::FileChooser>("Save patch", juce::File{}, "*.plinko");
        auto self = juce::Component::SafePointer<PlinkoAudioProcessorEditor>(this);
        patchChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::warnAboutOverwriting,
            [self](const juce::FileChooser& fc) {
                if (self == nullptr) return;
                auto f = fc.getResult();
                if (f != juce::File{}) self->proc_.savePatch(f.withFileExtension("plinko"));
            });
    };
    addAndMakeVisible(loadBtn_);
    loadBtn_.setButtonText("Load");
    loadBtn_.onClick = [this] {
        patchChooser_ = std::make_unique<juce::FileChooser>("Load patch", juce::File{}, "*.plinko");
        auto self = juce::Component::SafePointer<PlinkoAudioProcessorEditor>(this);
        patchChooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [self](const juce::FileChooser& fc) {
                if (self == nullptr) return;
                auto f = fc.getResult();
                if (f != juce::File{} && self->proc_.loadPatch(f)) self->refreshFromProcessor();
            });
    };

    setResizable(true, true);
    setResizeLimits(820, 520, 1600, 1100);
    setSize(940, 620);
}

void PlinkoAudioProcessorEditor::refreshFromProcessor() {
    // APVTS-attached controls (knobs, Source/Input Mode) update themselves on replaceState;
    // sync the non-APVTS ones (per-bus sliders + the board working copy).
    reloadBusSliders();
    board_.reloadFromProcessor();
    repaint();
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
    sourceBox_.setBounds(top.removeFromLeft(96).reduced(2));
    inputModeBox_.setBounds(top.removeFromLeft(96).reduced(2));
    loadWavBtn_.setBounds(top.removeFromLeft(84).reduced(2));
    busBox_.setBounds(top.removeFromLeft(74).reduced(2));
    saveBtn_.setBounds(top.removeFromLeft(56).reduced(2));
    loadBtn_.setBounds(top.removeFromLeft(56).reduced(2));

    auto shape = r.removeFromTop(82);   // Shape section
    layRow(shape, { &gravity_, &boardWidth_, &ballSize_, &ballBounce_ });

    auto master = r.removeFromBottom(92); // Master section
    layRow(master, { &tone_, &width_, &dryWet_, &level_, &hold_, &impact_ });

    auto left  = r.removeFromLeft(180);   // Delay panel
    auto right = r.removeFromRight(180);  // Reverb panel
    board_.setBounds(r.reduced(4));

    // The panels carry a lot of controls; lay them in a 2-column grid so each rotary keeps enough
    // height to render (single-column collapsed the dials).
    auto layPanel = [](juce::Rectangle<int> p, juce::TextButton& btn, juce::ComboBox& typeBox,
                       std::initializer_list<std::pair<juce::Label*, juce::Component*>> rows) {
        p.reduce(4, 2);
        btn.setBounds(p.removeFromTop(22));
        p.removeFromTop(2);
        typeBox.setBounds(p.removeFromTop(22));
        p.removeFromTop(4);
        const int n = (int)rows.size();
        const int cols = 2;
        const int nrows = (n + cols - 1) / cols;
        const int cw = p.getWidth() / cols;
        const int ch = nrows > 0 ? p.getHeight() / nrows : p.getHeight();
        int i = 0;
        for (auto& row : rows) {
            int c = i % cols, rr = i / cols;
            juce::Rectangle<int> cell(p.getX() + c * cw, p.getY() + rr * ch, cw, ch);
            layStacked(cell.reduced(3), *row.first, *row.second);
            ++i;
        }
    };
    layPanel(left, delayBrushBtn_, delayTypeBox_, {
        { &delayBounceL_, &delayBounce_ }, { &delaySizeL_, &delaySize_ },
        { &delaySendL_, &delaySend_ }, { &delayLevelL_, &delayLevel_ }, { &delayToneL_, &delayTone_ },
        { &feedbackL_, &feedback_ }, { &delayMixL_, &delayMix_ } });
    layPanel(right, reverbBrushBtn_, reverbTypeBox_, {
        { &reverbBounceL_, &reverbBounce_ }, { &reverbSizeL_, &reverbSize_ },
        { &reverbSendL_, &reverbSend_ }, { &reverbLevelL_, &reverbLevel_ }, { &reverbToneL_, &reverbTone_ },
        { &reverbDecayL_, &reverbDecay_ }, { &reverbMixL_, &reverbMix_ } });
}
