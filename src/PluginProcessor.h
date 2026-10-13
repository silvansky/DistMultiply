#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

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
    std::atomic<bool> monoInput { false }, sidechainPresent { false };
    juce::SmoothedValue<float> gainRamp, mixRamp;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistMultiplyProcessor)
};
