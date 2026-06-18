#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include "PhysicsCore.h"
#include "SoundEngine.h"
#include "Params.h"

class PlinkoAudioProcessor : public juce::AudioProcessor {
public:
    PlinkoAudioProcessor();
    ~PlinkoAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Plinko Delay"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState apvts{ *this, nullptr, "PARAMS", createParameterLayout() };

    // Read-only access for the editor.
    const BoardParams& board() const { return board_; }
    std::atomic<float> ballNX{ 0.5f }, ballNY{ 1.0f };  // normalized ball position (lock-free)

private:
    BoardParams board_;
    PhysicsWorld physics_;
    SoundEngine engine_;
    EngineParams ep_;
    double sr_ = 44100.0;
    std::vector<Collision> ev_;
    std::vector<ScheduledHit> hits_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlinkoAudioProcessor)
};
