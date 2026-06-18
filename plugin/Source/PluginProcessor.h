#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <vector>
#include <memory>
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
    static BoardParams defaultBoard();   // the baseline starting board (for Revert)
    std::atomic<float> ballNX{ 0.5f }, ballNY{ 1.0f };  // normalized ball position (lock-free)
    std::atomic<float> ballR{ 0.03f };                  // live ball radius (board units) for the GUI
    std::atomic<float> boardW{ 1.0f };                  // live board width for the GUI (aspect + normalization)

    // GUI thread: enqueue a single live peg edit. The audio thread applies it to the
    // running physics next block (no re-init -> the ball keeps flowing).
    enum class EditType { Add, Move, Delete, SetType, SetDrop, Clear, Reset, BulkSet };
    struct Edit {
        EditType type; int idx = 0; float x = 0, y = 0, rest = 0.5f; int pegType = 0; float radius = 0.022f;
        std::shared_ptr<BoardParams> snapshot;  // BulkSet: full peg set to rebuild (bulk edits + undo)
    };
    void pushEdit(const Edit& e);

    std::atomic<bool> running_{ false };  // open STOPPED (ball parked at the draggable start point)

private:
    BoardParams board_;
    PhysicsWorld physics_;
    SoundEngine engine_;
    EngineParams ep_;
    double sr_ = 44100.0;
    std::vector<Collision> ev_;
    std::vector<ScheduledHit> hits_;

    juce::CriticalSection editLock_;
    std::vector<Edit> pendingEdits_, applyBuf_;
    std::atomic<bool> hasEdits_{ false };
    static constexpr uint64_t kSeed = 12345;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlinkoAudioProcessor)
};
