#include "PluginProcessor.h"
#include "PluginEditor.h"

DistMultiplyProcessor::DistMultiplyProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Input", juce::AudioChannelSet::stereo(), true)
          .withInput("Sidechain", juce::AudioChannelSet::stereo(), false)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "DistMultiplyState", makeParameters())
{
    mode = parameters.getRawParameterValue("mode");
    gain = parameters.getRawParameterValue("gain");
    mix = parameters.getRawParameterValue("mix");
}

juce::AudioProcessorValueTreeState::ParameterLayout DistMultiplyProcessor::makeParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"mode", 1}, "Mode",
        juce::StringArray{"L/R", "In/Sidechain"}, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"gain", 1}, "Gain",
        juce::NormalisableRange<float>{0.0f, 2.0f, 0.001f}, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mix", 1}, "Mix",
        juce::NormalisableRange<float>{0.0f, 100.0f, 0.01f}, 100.0f));
    return layout;
}

bool DistMultiplyProcessor::isBusesLayoutSupported(const BusesLayout& layout) const
{
    const auto supported = [](const juce::AudioChannelSet& set)
    { return set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo(); };
    return layout.inputBuses.size() == 2 && layout.outputBuses.size() == 1
        && supported(layout.getMainInputChannelSet()) && supported(layout.getMainOutputChannelSet())
        && (layout.inputBuses[1].isDisabled() || supported(layout.inputBuses[1]));
}

void DistMultiplyProcessor::prepareToPlay(double rate, int)
{
    gainRamp.reset(rate, 0.01);
    mixRamp.reset(rate, 0.01);
    reset();
    monoInput.store(getMainBusNumInputChannels() == 1);
    sidechainPresent.store(getChannelCountOfBus(true, 1) > 0);
    setLatencySamples(0);
}

void DistMultiplyProcessor::reset()
{
    gainRamp.setCurrentAndTargetValue(gain->load());
    mixRamp.setCurrentAndTargetValue(mix->load() * 0.01f);
}

void DistMultiplyProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    midi.clear();
    auto input = getBusBuffer(buffer, true, 0);
    auto output = getBusBuffer(buffer, false, 0);
    auto side = getBusBuffer(buffer, true, 1);
    const bool mono = input.getNumChannels() == 1;
    const bool useSide = mono || mode->load() >= 0.5f;
    const bool sideAvailable = side.getNumChannels() > 0;
    monoInput.store(mono);
    sidechainPresent.store(sideAvailable);
    gainRamp.setTargetValue(gain->load());
    mixRamp.setTargetValue(mix->load() * 0.01f);

    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        // Read all inputs before writing: with mono main input, output R can
        // share storage with sidechain L in JUCE's process buffer.
        const float aLeft = input.getSample(0, i);
        const float aRight = input.getSample(mono ? 0 : 1, i);
        const float bLeft = sideAvailable ? side.getSample(0, i) : 0.0f;
        const float bRight = sideAvailable ? side.getSample(side.getNumChannels() == 1 ? 0 : 1, i) : 0.0f;
        float dryLeft = aLeft, dryRight = aRight;
        float wetLeft = aLeft, wetRight = aRight;
        if (!useSide)
        {
            dryLeft = dryRight = (aLeft + aRight) * 0.5f;
            wetLeft = wetRight = aLeft * aRight;
        }
        else if (sideAvailable)
        {
            wetLeft = aLeft * bLeft;
            wetRight = aRight * bRight;
        }
        // An absent sidechain passes the main input through. A connected silent
        // sidechain naturally produces a zero wet signal; Mix still applies.
        const float wetAmount = mixRamp.getNextValue(), outputGain = gainRamp.getNextValue();
        const float left = ((1.0f - wetAmount) * dryLeft + wetAmount * wetLeft) * outputGain;
        const float right = ((1.0f - wetAmount) * dryRight + wetAmount * wetRight) * outputGain;
        output.setSample(0, i, output.getNumChannels() == 1 ? (left + right) * 0.5f : left);
        if (output.getNumChannels() == 2) output.setSample(1, i, right);
    }
}

void DistMultiplyProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void DistMultiplyProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* DistMultiplyProcessor::createEditor() { return new DistMultiplyEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DistMultiplyProcessor(); }
