#include "PluginProcessor.h"
#include "PluginEditor.h"

PlinkoAudioProcessor::PlinkoAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    makeStaggeredBoard(board_);
    for (int i = 0; i < board_.pegCount; ++i) {
        if (i % 3 == 1) board_.pegType[i] = 1;     // ~1/3 reverb pegs
        if (i % 9 == 0) board_.pegRest[i] = 1.4f;   // bumpers
    }
    ev_.reserve(512);
    hits_.reserve(512);
}

void PlinkoAudioProcessor::prepareToPlay(double sampleRate, int) {
    sr_ = sampleRate;
    physics_.init(12345, board_);
    engine_.prepare(sampleRate, ep_);
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

    // pull live params
    ep_.feedback    = apvts.getRawParameterValue(pid::feedback)->load();
    ep_.delayMix    = apvts.getRawParameterValue(pid::delayMix)->load();
    ep_.reverbMix   = apvts.getRawParameterValue(pid::reverbMix)->load();
    ep_.reverbDecay = apvts.getRawParameterValue(pid::reverbDecay)->load();
    ep_.tone        = apvts.getRawParameterValue(pid::tone)->load();
    ep_.panWidth    = apvts.getRawParameterValue(pid::panWidth)->load();
    const float gravity = apvts.getRawParameterValue(pid::gravity)->load();
    const float level   = apvts.getRawParameterValue(pid::level)->load();

    physics_.setGravity(gravity);
    engine_.setParams(ep_);

    // advance physics by exactly this block's duration
    const double t0 = physics_.simTime();
    ev_.clear();
    physics_.advance((double)n / sr_, ev_);

    hits_.clear();
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

    buffer.clear();
    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : L;
    engine_.process(L, R, n, hits_.data(), (int)hits_.size());
    buffer.applyGain(level);

    ballNX.store(physics_.dbgBallX() / board_.width, std::memory_order_relaxed);
    ballNY.store(physics_.dbgBallY() / board_.topY, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* PlinkoAudioProcessor::createEditor() {
    return new PlinkoAudioProcessorEditor(*this);
}

void PlinkoAudioProcessor::getStateInformation(juce::MemoryBlock& dest) {
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void PlinkoAudioProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new PlinkoAudioProcessor();
}
