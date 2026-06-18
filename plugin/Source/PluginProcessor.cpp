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

void PlinkoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sr_ = sampleRate;
    physics_.init(kSeed, board_);
    engine_.prepare(sampleRate, ep_);
    inRingLen_ = juce::jmax(1, (int)(sampleRate * 2.0));   // 2s of input history for grains
    inRing_.assign(inRingLen_, 0.0f);
    inWrite_ = 0;
    dryCopyL_.assign(juce::jmax(1, samplesPerBlock), 0.0f);
    dryCopyR_.assign(juce::jmax(1, samplesPerBlock), 0.0f);
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

void PlinkoAudioProcessor::loadWavLoop(std::vector<float>&& mono) {
    const juce::ScopedLock sl(wavLock_);
    wavPending_ = std::move(mono);
    wavReady_.store(true, std::memory_order_release);
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

    // Capture the incoming audio: a mono copy into the input ring (grain source) and, in input
    // mode, the dry stereo block for passthrough. w0 = ring write index at the start of the block.
    if (wavReady_.load(std::memory_order_acquire)) {   // pick up a freshly loaded WAV loop
        const juce::ScopedTryLock stl(wavLock_);
        if (stl.isLocked()) { wav_.swap(wavPending_); wavReady_.store(false, std::memory_order_release); wavPos_ = 0; }
    }
    const int  source    = (int)apvts.getRawParameterValue(pid::source)->load();  // 0 Synth, 1 Input, 2 WAV
    const bool inputMode = (source >= 1);
    const bool wavMode   = (source == 2);
    const bool running   = running_.load(std::memory_order_relaxed);
    const int  w0 = inWrite_;
    const bool capDry = inputMode && n <= (int)dryCopyL_.size();
    if (wavMode && !running) wavPos_ = 0;  // WAV loop is synced to the ball: rewind while stopped
    {
        const int nch = buffer.getNumChannels();
        const float* inL = buffer.getReadPointer(0);
        const float* inR = nch > 1 ? buffer.getReadPointer(1) : inL;
        for (int i = 0; i < n; ++i) {
            float m, dl, dr;
            if (wavMode) {                 // looping WAV plays ONLY while the ball runs (synced to Start/Stop)
                float s = 0.0f;
                if (running && !wav_.empty()) { s = wav_[wavPos_]; if (++wavPos_ >= (int)wav_.size()) wavPos_ = 0; }
                m = dl = dr = s;
            } else {                       // live input (dry unused in Synth mode)
                dl = inL[i]; dr = inR[i]; m = 0.5f * (dl + dr);
            }
            inRing_[inWrite_] = m;
            if (++inWrite_ >= inRingLen_) inWrite_ = 0;
            if (capDry) { dryCopyL_[i] = dl; dryCopyR_[i] = dr; }
        }
    }

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

    // Input mode: feed the ring to the engine and let it output full wet (grain-fed delay/reverb);
    // the dry live signal is crossfaded back in below. Synth mode: no input, engine handles dry/wet.
    const float userDryWet = ep_.dryWet;
    if (inputMode) { ep_.dryWet = 1.0f; engine_.setInput(inRing_.data(), inRingLen_); }
    else             engine_.setInput(nullptr, 0);
    engine_.setParams(ep_);

    hits_.clear();
    if (running) {
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
            if (inputMode) {          // grab the last ~grainSeconds of the input at this hit
                int gl = (int)(ep_.grainSeconds * sr_);
                sh.hit.inputStart = ((w0 + off - gl) % inRingLen_ + inRingLen_) % inRingLen_;
            } else {
                sh.hit.inputStart = 0;
            }
            hits_.push_back(sh);
        }
    } else {
        physics_.holdAtDrop();       // stopped: ball waits at the (draggable) start point
    }

    buffer.clear();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
    engine_.process(L, R, n, hits_.data(), (int)hits_.size());
    if (capDry) {                       // input mode: crossfade the live dry signal back in
        const bool stereo = buffer.getNumChannels() > 1;
        const float dw = userDryWet;
        for (int i = 0; i < n; ++i) {
            float wl = L[i], wr = stereo ? R[i] : wl;
            L[i] = dryCopyL_[i] * (1.0f - dw) + wl * dw;
            if (stereo) R[i] = dryCopyR_[i] * (1.0f - dw) + wr * dw;
        }
    }
    buffer.applyGain(level);

    const float xMin = kBoardCenterX - board_.width * 0.5f;   // centered span -> 0..1 across the board
    ballNX.store((physics_.dbgBallX() - xMin) / board_.width, std::memory_order_relaxed);
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
