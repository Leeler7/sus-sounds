#include "PluginProcessor.h"
#include "PluginEditor.h"

PlinkoAudioProcessor::PlinkoAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    board_ = defaultBoard();
    ev_.reserve(512);
    hits_.reserve(512);
    pendingEdits_.reserve(256);
    applyBuf_.reserve(256);
}

void PlinkoAudioProcessor::prepareToPlay(double sampleRate, int) {
    sr_ = sampleRate;
    physics_.init(kSeed, board_);
    engine_.prepare(sampleRate, ep_);
}

BoardParams PlinkoAudioProcessor::defaultBoard() {
    BoardParams b;
    b.pegRadius  = 0.0225f;          // set BEFORE building so the staggered pegs use it (matches the
    b.ballRadius = 0.045f;           // brush noon size) -- board pegs and new pegs are now equal
    makeStaggeredBoard(b, 7, 5);     // ~half-density, alternating delay/reverb rows
    return b;
}

void PlinkoAudioProcessor::pushEdit(const Edit& e) {
    const juce::ScopedLock sl(editLock_);
    pendingEdits_.push_back(e);
    hasEdits_.store(true, std::memory_order_release);
}

bool PlinkoAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == out;
}

void PlinkoAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();

    // Apply queued live peg edits to the running world (no re-init -> ball keeps flowing).
    // Try-lock so we never block the GUI; if we miss it, edits apply next block.
    if (hasEdits_.load(std::memory_order_acquire)) {
        const juce::ScopedTryLock stl(editLock_);
        if (stl.isLocked()) {
            applyBuf_.swap(pendingEdits_);
            hasEdits_.store(false, std::memory_order_release);
            for (const auto& e : applyBuf_) {
                switch (e.type) {
                    case EditType::Add:     physics_.addPeg(e.x, e.y, e.rest, e.pegType, e.radius); break;
                    case EditType::Move:    physics_.movePeg(e.idx, e.x, e.y); break;
                    case EditType::Delete:  physics_.removePeg(e.idx); break;
                    case EditType::SetType: physics_.setPegType(e.idx, e.pegType); break;
                    case EditType::SetDrop: physics_.setDropPoint(e.x, e.y); break;
                    case EditType::Clear:   physics_.clearPegs(); break;
                    case EditType::Reset:   board_ = defaultBoard(); physics_.init(kSeed, board_); break;
                    case EditType::BulkSet: if (e.snapshot) physics_.setPegs(*e.snapshot); break;
                }
            }
            applyBuf_.clear();
            board_ = physics_.boardParams();   // keep mirror current (for re-prepare)
        }
    }

    // pull live params
    ep_.feedback    = apvts.getRawParameterValue(pid::feedback)->load();
    ep_.delayMix    = apvts.getRawParameterValue(pid::delayMix)->load();
    ep_.reverbMix   = apvts.getRawParameterValue(pid::reverbMix)->load();
    ep_.reverbDecay = apvts.getRawParameterValue(pid::reverbDecay)->load();
    ep_.tone        = apvts.getRawParameterValue(pid::tone)->load();
    ep_.panWidth    = apvts.getRawParameterValue(pid::panWidth)->load();
    ep_.dryWet      = apvts.getRawParameterValue(pid::dryWet)->load();
    const float gravity = apvts.getRawParameterValue(pid::gravity)->load();
    const float level   = apvts.getRawParameterValue(pid::level)->load();

    physics_.setGravity(gravity);
    physics_.setBallBounce(apvts.getRawParameterValue(pid::ballBounce)->load());
    physics_.setBallSize(apvts.getRawParameterValue(pid::ballSize)->load());
    physics_.setWidth(apvts.getRawParameterValue(pid::boardWidth)->load());
    board_.width = physics_.boardParams().width;   // keep the mirror current (re-prepare + GUI)
    engine_.setParams(ep_);

    hits_.clear();
    if (running_.load(std::memory_order_relaxed)) {
        // advance physics by exactly this block's duration
        const double t0 = physics_.simTime();
        ev_.clear();
        physics_.advance((double)n / sr_, ev_);
        for (const auto& c : ev_) {
            int off = (int)((c.t - t0) * sr_ + 0.5);
            if (off < 0) off = 0;
            if (off >= n) off = n - 1;
            ScheduledHit sh;
            sh.offset = off;
            sh.hit = pegToTap(c, ep_);
            sh.hit.inputStart = 0;   // exciter mode (input path is a later GUI pass)
            hits_.push_back(sh);
        }
    } else {
        physics_.holdAtDrop();       // stopped: ball waits at the (draggable) start point
    }

    buffer.clear();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
    engine_.process(L, R, n, hits_.data(), (int)hits_.size());
    buffer.applyGain(level);

    ballNX.store(physics_.dbgBallX() / board_.width, std::memory_order_relaxed);
    ballNY.store(physics_.dbgBallY() / board_.topY, std::memory_order_relaxed);
    ballR.store(physics_.boardParams().ballRadius, std::memory_order_relaxed);
    boardW.store(board_.width, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PlinkoAudioProcessor::createEditor() {
    return new PlinkoAudioProcessorEditor(*this);
}

void PlinkoAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void PlinkoAudioProcessor::setStateInformation(const void* data, int size) {
    // Standalone always opens at default settings (the app's own state save is ignored). A VST3 in
    // a DAW still restores its saved session state normally.
    if (wrapperType == wrapperType_Standalone)
        return;
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PlinkoAudioProcessor();
}
