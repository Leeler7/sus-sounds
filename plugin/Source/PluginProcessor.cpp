#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

PlinkoAudioProcessor::PlinkoAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Main",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Bus 1", juce::AudioChannelSet::stereo(), true)   // per-bus dry throws (route to your FX)
        .withOutput("Bus 2", juce::AudioChannelSet::stereo(), true)
        .withOutput("Bus 3", juce::AudioChannelSet::stereo(), true)
        .withOutput("Bus 4", juce::AudioChannelSet::stereo(), true)) {
    board_ = defaultBoard();
    resetBusPresets();
    ev_.reserve(512);
    hits_.reserve(512);
    pendingEdits_.reserve(256);
    applyBuf_.reserve(256);
}

void PlinkoAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    sr_ = sampleRate;
    physics_.init(kSeed, board_);
    engine_.prepare(sampleRate, ep_);
    inRingLen_ = juce::jmax(1, (int)(sampleRate * 8.0));   // 8s input history (> max Grain 6s) so a big
                                                           // grain is a true snapshot, not a wrap to live
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
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())   // main must be stereo
        return false;
    auto in = layouts.getMainInputChannelSet();
    if (!in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono())
        return false;
    for (int b = 1; b <= kNumBuses; ++b) {   // aux bus outputs: stereo or disabled
        auto aux = layouts.getChannelSet(false, b);
        if (!aux.isDisabled() && aux != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
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
    const bool liveOn    = apvts.getRawParameterValue(pid::inputMode)->load() > 0.5f;  // Granular(0)/Live(1)
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
                    case EditType::Add:     physics_.addPeg(e.x, e.y, e.rest, e.pegType, e.radius, e.bus, e.send, e.level, e.tone); break;
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
    for (int b = 0; b < kNumBuses; ++b) {   // per-bus effect character
        ep_.feedback[b]    = busFeedback_[b].load(std::memory_order_relaxed);
        ep_.delayMix[b]    = busDelayMix_[b].load(std::memory_order_relaxed);
        ep_.reverbMix[b]   = busReverbMix_[b].load(std::memory_order_relaxed);
        ep_.reverbDecay[b] = busReverbDecay_[b].load(std::memory_order_relaxed);
        ep_.delayType[b]   = busDelayType_[b].load(std::memory_order_relaxed);
        ep_.reverbType[b]  = busReverbType_[b].load(std::memory_order_relaxed);
    }
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
    if (inputMode) engine_.setInput(inRing_.data(), inRingLen_);
    else           engine_.setInput(nullptr, 0);
    ep_.holdSeconds = apvts.getRawParameterValue(pid::hold)->load();
    ep_.grainSeconds = ep_.holdSeconds;   // the Grain knob sizes both: granular grain + live throw length
    ep_.impact      = apvts.getRawParameterValue(pid::impact)->load();
    engine_.setInputMix(inputMode);             // input throws heard directly + effects
    engine_.setLiveMode(inputMode && liveOn);   // Live = forward Hold-length throw vs granular snapshot
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
            const auto& bp = physics_.boardParams();
            if (c.peg >= 0 && c.peg < bp.pegCount) {        // apply this peg's per-peg trims
                sh.hit.bus    = bp.pegBus[c.peg];
                sh.hit.send   = bp.pegSend[c.peg];
                sh.hit.level *= bp.pegLevel[c.peg];
                sh.hit.brightness = juce::jlimit(0.03f, 1.0f, sh.hit.brightness * (0.5f + bp.pegTone[c.peg]));
            }
            if (inputMode) {          // Live: read FORWARD from the strike; Granular: a short past snapshot
                int gl = (int)(ep_.grainSeconds * sr_);
                int start = liveOn ? (w0 + off) : (w0 + off - gl);
                sh.hit.inputStart = ((start % inRingLen_) + inRingLen_) % inRingLen_;
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
    float* R = buffer.getWritePointer(1);   // Main is required stereo

    float* auxL[kNumBuses] = { nullptr };    // per-bus dry-throw outputs (route to your own FX)
    float* auxR[kNumBuses] = { nullptr };
    for (int b = 0; b < kNumBuses; ++b) {
        auto* bus = getBus(false, b + 1);    // output buses 1..kNumBuses
        if (bus != nullptr && bus->isEnabled()) {
            auto ab = getBusBuffer(buffer, false, b + 1);
            auxL[b] = ab.getWritePointer(0);
            auxR[b] = ab.getNumChannels() > 1 ? ab.getWritePointer(1) : auxL[b];
        }
    }
    engine_.process(L, R, n, hits_.data(), (int)hits_.size(), auxL, auxR);

    if (capDry) {                       // input mode: crossfade the continuous loop into Main
        const float dw = userDryWet;
        for (int i = 0; i < n; ++i) {
            float wl = L[i], wr = R[i];
            L[i] = dryCopyL_[i] * (1.0f - dw) + wl * dw;
            R[i] = dryCopyR_[i] * (1.0f - dw) + wr * dw;
        }
    }
    for (int i = 0; i < n; ++i) {       // Main: master level + soft clip (aux outs stay raw for routing)
        L[i] = std::tanh(L[i] * level);
        R[i] = std::tanh(R[i] * level);
    }

    const float xMin = kBoardCenterX - board_.width * 0.5f;   // centered span -> 0..1 across the board
    ballNX.store((physics_.dbgBallX() - xMin) / board_.width, std::memory_order_relaxed);
    ballNY.store(physics_.dbgBallY() / board_.topY, std::memory_order_relaxed);
    ballR.store(physics_.boardParams().ballRadius, std::memory_order_relaxed);
    boardW.store(board_.width, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PlinkoAudioProcessor::createEditor() {
    return new PlinkoAudioProcessorEditor(*this);
}

void PlinkoAudioProcessor::resetBusPresets() {
    for (int b = 0; b < kNumBuses; ++b) {
        busFeedback_[b] = 0.62f; busDelayMix_[b] = 0.5f; busReverbDecay_[b] = 0.85f; busReverbMix_[b] = 0.5f;
        busDelayType_[b] = 0; busReverbType_[b] = 1;            // Digital / Hall
        for (int t = 0; t < 2; ++t) {
            busBounce_[b][t] = 1.0f; busSize_[b][t] = 0.0225f;
            busSend_[b][t] = 1.0f; busLevel_[b][t] = 1.0f; busTone_[b][t] = 0.5f;
        }
    }
}

// Serialize the FULL patch: APVTS knobs + board geometry + per-bus effects/types + brush presets.
juce::ValueTree PlinkoAudioProcessor::stateToTree() {
    juce::ValueTree root("PLINKO_PATCH");
    root.setProperty("version", 1, nullptr);
    root.appendChild(apvts.copyState(), nullptr);     // the "PARAMS" subtree

    juce::MemoryBlock bb;                              // board geometry
    { juce::MemoryOutputStream os(bb, false);
      os.writeInt(board_.pegCount);
      os.writeFloat(board_.dropX); os.writeFloat(board_.dropY); os.writeFloat(board_.width);
      for (int i = 0; i < board_.pegCount; ++i) {
          os.writeFloat(board_.pegX[i]); os.writeFloat(board_.pegY[i]); os.writeFloat(board_.pegRest[i]);
          os.writeFloat(board_.pegRad[i]); os.writeInt(board_.pegType[i]); os.writeInt(board_.pegBus[i]);
          os.writeFloat(board_.pegSend[i]); os.writeFloat(board_.pegLevel[i]); os.writeFloat(board_.pegTone[i]);
      } }
    root.setProperty("board", bb, nullptr);

    juce::MemoryBlock ub;                              // per-bus effects + brush presets
    { juce::MemoryOutputStream os(ub, false);
      for (int b = 0; b < kNumBuses; ++b) {
          os.writeFloat(busFeedback_[b].load()); os.writeFloat(busDelayMix_[b].load());
          os.writeFloat(busReverbDecay_[b].load()); os.writeFloat(busReverbMix_[b].load());
          os.writeInt(busDelayType_[b].load()); os.writeInt(busReverbType_[b].load());
          for (int t = 0; t < 2; ++t) {
              os.writeFloat(busBounce_[b][t]); os.writeFloat(busSize_[b][t]); os.writeFloat(busSend_[b][t]);
              os.writeFloat(busLevel_[b][t]); os.writeFloat(busTone_[b][t]);
          } } }
    root.setProperty("buses", ub, nullptr);
    return root;
}

void PlinkoAudioProcessor::treeToState(const juce::ValueTree& root) {
    if (!root.isValid() || !root.hasType("PLINKO_PATCH")) return;

    auto params = root.getChildWithName(apvts.state.getType());   // "PARAMS"
    if (params.isValid()) apvts.replaceState(params);

    if (auto* mb = root.getProperty("board").getBinaryData()) {
        juce::MemoryInputStream is(*mb, false);
        BoardParams nb = board_;
        int n = is.readInt();
        nb.dropX = is.readFloat(); nb.dropY = is.readFloat(); nb.width = is.readFloat();
        n = juce::jlimit(0, 128, n);
        nb.pegCount = n;
        for (int i = 0; i < n; ++i) {
            nb.pegX[i] = is.readFloat(); nb.pegY[i] = is.readFloat(); nb.pegRest[i] = is.readFloat();
            nb.pegRad[i] = is.readFloat(); nb.pegType[i] = is.readInt(); nb.pegBus[i] = is.readInt();
            nb.pegSend[i] = is.readFloat(); nb.pegLevel[i] = is.readFloat(); nb.pegTone[i] = is.readFloat();
        }
        board_ = nb;
        Edit ed; ed.type = EditType::BulkSet; ed.snapshot = std::make_shared<BoardParams>(board_); pushEdit(ed);
        Edit sd; sd.type = EditType::SetDrop; sd.x = board_.dropX; sd.y = board_.dropY; pushEdit(sd);
    }

    if (auto* mb = root.getProperty("buses").getBinaryData()) {
        juce::MemoryInputStream is(*mb, false);
        for (int b = 0; b < kNumBuses; ++b) {
            busFeedback_[b] = is.readFloat(); busDelayMix_[b] = is.readFloat();
            busReverbDecay_[b] = is.readFloat(); busReverbMix_[b] = is.readFloat();
            busDelayType_[b] = is.readInt(); busReverbType_[b] = is.readInt();
            for (int t = 0; t < 2; ++t) {
                busBounce_[b][t] = is.readFloat(); busSize_[b][t] = is.readFloat(); busSend_[b][t] = is.readFloat();
                busLevel_[b][t] = is.readFloat(); busTone_[b][t] = is.readFloat();
            }
        }
    }
}

void PlinkoAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = stateToTree().createXml())
        copyXmlToBinary(*xml, dest);
}

void PlinkoAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        treeToState(juce::ValueTree::fromXml(*xml));
}

void PlinkoAudioProcessor::savePatch(const juce::File& f) {
    if (auto xml = stateToTree().createXml())
        xml->writeTo(f);
}

bool PlinkoAudioProcessor::loadPatch(const juce::File& f) {
    if (auto xml = juce::XmlDocument::parse(f)) {
        treeToState(juce::ValueTree::fromXml(*xml));
        return true;
    }
    return false;
}

juce::StringArray PlinkoAudioProcessor::factoryPresetNames() const {
    return { "Init", "Classic Plinko" };
}

juce::File PlinkoAudioProcessor::presetsDir() {
    auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                 .getChildFile("PlinkoDelay").getChildFile("Presets");
    d.createDirectory();
    return d;
}

void PlinkoAudioProcessor::loadFactoryPreset(int index) {
    resetBusPresets();                                   // buses + brush presets to default
    board_ = defaultBoard();
    { Edit ed; ed.type = EditType::Reset; pushEdit(ed); }// physics back to the default board
    for (auto* p : getParameters()) p->setValueNotifyingHost(p->getDefaultValue());  // knobs to default

    if (index == 1) {   // Classic Plinko: real-Plinko physics feel (synth source, default board/buses)
        auto setP = [this](const char* id, float v) {
            if (auto* pr = apvts.getParameter(id)) pr->setValueNotifyingHost(pr->convertTo0to1(v));
        };
        setP(pid::gravity,    20.33f);
        setP(pid::ballSize,   0.06f);
        setP(pid::ballBounce, 0.864f);
    }
    // index 0 = Init (pure defaults, already applied)
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PlinkoAudioProcessor();
}
