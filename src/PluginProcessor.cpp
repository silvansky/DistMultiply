#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
    oversampling = parameters.getRawParameterValue("oversampling");
    dcBlock = parameters.getRawParameterValue("dcBlock");
    mainOffset = parameters.getRawParameterValue("mainOffset");
    sidechainOffset = parameters.getRawParameterValue("sidechainOffset");
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
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"oversampling", 1}, "Oversampling",
        juce::StringArray{"1x", "2x", "4x"}, 1));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"dcBlock", 1}, "DC Block",
        juce::NormalisableRange<float>{0.0f, 20.0f, 0.1f}, 15.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"mainOffset", 1}, "Main Offset",
        juce::NormalisableRange<float>{-1.0f, 1.0f}, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{"sidechainOffset", 1}, "Sidechain Offset",
        juce::NormalisableRange<float>{-1.0f, 1.0f}, 0.0f));
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

void DistMultiplyProcessor::prepareToPlay(double rate, int maximumBlockSize)
{
    processingRate = rate;
    const auto capacity = juce::jmax(1, maximumBlockSize);
    scratch.setSize(4, capacity);
    for (size_t i = 0; i < oversamplers.size(); ++i)
    {
        oversamplers[i] = std::make_unique<juce::dsp::Oversampling<float>>(4, i + 1,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversamplers[i]->initProcessing(static_cast<size_t>(capacity));
    }
    const int latency2x = juce::roundToInt(oversamplers[0]->getLatencyInSamples());
    const int latency4x = juce::roundToInt(oversamplers[1]->getLatencyInSamples());
    const int latency = juce::jmax(latency2x, latency4x);
    jassert(latency < static_cast<int>(padding[0].size()));
    paddingSamples = {latency, latency - latency2x, latency - latency4x};
    for (auto* ramp : {&gainRamp, &mixRamp, &mainOffsetRamp, &sidechainOffsetRamp, &dcRamp})
        ramp->reset(rate, 0.01);
    reset();
    monoInput.store(getMainBusNumInputChannels() == 1);
    sidechainPresent.store(getChannelCountOfBus(true, 1) > 0);
    setLatencySamples(latency);
}

void DistMultiplyProcessor::reset()
{
    gainRamp.setCurrentAndTargetValue(gain->load());
    mixRamp.setCurrentAndTargetValue(mix->load() * 0.01f);
    mainOffsetRamp.setCurrentAndTargetValue(mainOffset->load());
    sidechainOffsetRamp.setCurrentAndTargetValue(sidechainOffset->load());
    dcRamp.setCurrentAndTargetValue(dcBlock->load());
    for (auto& sampler : oversamplers)
        if (sampler) sampler->reset();
    padding = {};
    paddingPosition = 0;
    activeOversampling = -1;
    dcInput = {};
    dcOutput = {};
    previousCutoff = -1.0;
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
    mainOffsetRamp.setTargetValue(mainOffset->load());
    sidechainOffsetRamp.setTargetValue(sidechainOffset->load());
    dcRamp.setTargetValue(dcBlock->load());
    const int selection = juce::jlimit(0, 2, juce::roundToInt(oversampling->load()));
    if (selection != activeOversampling)
    {
        if (selection > 0) oversamplers[static_cast<size_t>(selection - 1)]->reset();
        padding = {};
        paddingPosition = 0;
        activeOversampling = selection;
    }
    const int factor = 1 << selection;

    // Chunking also handles hosts that exceed the advertised maximum block size,
    // without allocating on the audio thread.
    for (int start = 0; start < buffer.getNumSamples(); start += scratch.getNumSamples())
    {
        const int count = juce::jmin(scratch.getNumSamples(), buffer.getNumSamples() - start);
        for (int i = 0; i < count; ++i)
        {
            // Snapshot every source before writing any outputs: mono main/stereo
            // output can alias output R with sidechain L.
            scratch.setSample(0, i, input.getSample(0, start + i));
            scratch.setSample(1, i, input.getSample(mono ? 0 : 1, start + i));
            scratch.setSample(2, i, sideAvailable ? side.getSample(0, start + i) : 0.0f);
            scratch.setSample(3, i, sideAvailable ? side.getSample(side.getNumChannels() == 1 ? 0 : 1, start + i) : 0.0f);
        }
        auto native = juce::dsp::AudioBlock<float>(scratch).getSubBlock(0, static_cast<size_t>(count));
        auto high = selection == 0 ? native
            : oversamplers[static_cast<size_t>(selection - 1)]->processSamplesUp(native);
        for (int i = 0; i < count; ++i)
        {
            const float offsetA = mainOffsetRamp.getNextValue();
            const float offsetB = sidechainOffsetRamp.getNextValue();
            for (int sub = 0; sub < factor; ++sub)
            {
                const int sample = i * factor + sub;
                const float aLeft = high.getSample(0, sample), aRight = high.getSample(1, sample);
                const float bLeft = high.getSample(2, sample), bRight = high.getSample(3, sample);
                float dryLeft = aLeft, dryRight = aRight;
                float wetLeft = aLeft, wetRight = aRight;
                if (!useSide)
                {
                    dryLeft = dryRight = (aLeft + aRight) * 0.5f;
                    wetLeft = wetRight = (aLeft + offsetA) * (aRight + offsetA);
                }
                else if (sideAvailable)
                {
                    wetLeft = (aLeft + offsetA) * (bLeft + offsetB);
                    wetRight = (aRight + offsetA) * (bRight + offsetB);
                }
                // Dry and wet share the resampling filters, avoiding phase
                // cancellation in the blend. Offsets affect only multiplication.
                high.setSample(0, sample, wetLeft);
                high.setSample(1, sample, wetRight);
                high.setSample(2, sample, dryLeft);
                high.setSample(3, sample, dryRight);
            }
        }
        if (selection > 0) oversamplers[static_cast<size_t>(selection - 1)]->processSamplesDown(native);
        for (int i = 0; i < count; ++i)
        {
            const float amount = mixRamp.getNextValue(), level = gainRamp.getNextValue();
            const double cutoff = dcRamp.getNextValue();
            if (!juce::exactlyEqual(cutoff, previousCutoff))
            {
                const double k = std::tan(juce::MathConstants<double>::pi * cutoff / processingRate);
                dcA = (1.0 - k) / (1.0 + k);
                dcB = (1.0 + dcA) * 0.5;
                previousCutoff = cutoff;
            }
            std::array<float, 2> result {};
            for (size_t channel = 0; channel < result.size(); ++channel)
            {
                const int c = static_cast<int>(channel);
                float value = ((1.0f - amount) * native.getSample(c + 2, i)
                               + amount * native.getSample(c, i)) * level;
                const int delay = paddingSamples[static_cast<size_t>(selection)];
                if (delay > 0)
                {
                    const auto read = static_cast<size_t>((paddingPosition + static_cast<int>(padding[channel].size()) - delay)
                                                          % static_cast<int>(padding[channel].size()));
                    const float delayed = padding[channel][read];
                    padding[channel][static_cast<size_t>(paddingPosition)] = value;
                    value = delayed;
                }
                const double filtered = dcB * (static_cast<double>(value) - dcInput[channel]) + dcA * dcOutput[channel];
                dcInput[channel] = value;
                dcOutput[channel] = cutoff > 0.0 ? filtered : 0.0;
                result[channel] = cutoff > 0.0 ? static_cast<float>(filtered) : value;
            }
            paddingPosition = (paddingPosition + 1) % static_cast<int>(padding[0].size());
            output.setSample(0, start + i, output.getNumChannels() == 1 ? (result[0] + result[1]) * 0.5f : result[0]);
            if (output.getNumChannels() == 2) output.setSample(1, start + i, result[1]);
        }
    }
}

void DistMultiplyProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = parameters.copyState().createXml()) copyXmlToBinary(*xml, dest);
}

void DistMultiplyProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size); xml && xml->hasTagName(parameters.state.getType()))
    {
        auto state = juce::ValueTree::fromXml(*xml);
        // Sessions saved before these controls existed use the new defaults,
        // including when loaded into an instance with previously edited values.
        for (const auto* id : {"oversampling", "dcBlock", "mainOffset", "sidechainOffset"})
            if (!state.getChildWithProperty("id", id).isValid())
            {
                const auto* parameter = parameters.getParameter(id);
                state.appendChild(juce::ValueTree("PARAM", {{"id", id},
                    {"value", parameter->convertFrom0to1(parameter->getDefaultValue())}}), nullptr);
            }
        parameters.replaceState(state);
    }
}

juce::AudioProcessorEditor* DistMultiplyProcessor::createEditor() { return new DistMultiplyEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DistMultiplyProcessor(); }
