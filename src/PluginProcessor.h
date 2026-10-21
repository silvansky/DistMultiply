#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

class DistMultiplyProcessor final : public juce::AudioProcessor
{
public:
    DistMultiplyProcessor();
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "DistMultiply"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    bool isMonoInput() const noexcept { return monoInput.load(); }
    bool usesSidechain() const noexcept { return isMonoInput() || mode->load() >= 0.5f; }
    bool hasSidechain() const noexcept { return sidechainPresent.load(); }
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters();
    std::atomic<float>* mode = nullptr;
    std::atomic<float>* gain = nullptr;
    std::atomic<float>* mix = nullptr;
    std::atomic<float>* oversampling = nullptr;
    std::atomic<float>* dcBlock = nullptr;
    std::atomic<float>* mainOffset = nullptr;
    std::atomic<float>* sidechainOffset = nullptr;
    std::atomic<bool> monoInput { false }, sidechainPresent { false };
    juce::SmoothedValue<float> gainRamp, mixRamp, mainOffsetRamp, sidechainOffsetRamp, dcRamp;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers;
    juce::AudioBuffer<float> scratch;
    std::array<int, 3> paddingSamples {};
    // A fixed latency across settings keeps automation independent of host PDC updates.
    std::array<std::array<float, 32>, 2> padding {};
    int paddingPosition = 0, activeOversampling = -1;
    double processingRate = 48000.0, previousCutoff = -1.0, dcA = 0.0, dcB = 1.0;
    std::array<double, 2> dcInput {}, dcOutput {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistMultiplyProcessor)
};
