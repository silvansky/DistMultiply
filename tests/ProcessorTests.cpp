#include "PluginProcessor.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
void near(float actual, float expected, const char* message)
{
    require(std::isfinite(actual) && std::abs(actual - expected) < 2.0e-6f, message);
}
void set(DistMultiplyProcessor& p, const char* id, float value)
{
    auto* parameter = p.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}
void layout(DistMultiplyProcessor& p, int main, int side, int out)
{
    auto buses = p.getBusesLayout();
    buses.inputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(main));
    buses.inputBuses.set(1, juce::AudioChannelSet::canonicalChannelSet(side));
    buses.outputBuses.set(0, juce::AudioChannelSet::canonicalChannelSet(out));
    require(p.setBusesLayout(buses), "All mono/stereo bus combinations must be supported");
}
float signal(int channel, int sample)
{
    // Includes exact zeros, tiny values, DC, sign changes, and products above unity.
    constexpr std::array<float, 7> values {0.0f, 0.000001f, -0.25f, 0.5f, -1.5f, 2.0f, 0.125f};
    return values[static_cast<size_t>((sample / 3 + channel * 2) % 7)];
}
std::vector<float> render(int blockSize, int main, int side, int out, int mode, float mix, float gain)
{
    DistMultiplyProcessor p;
    layout(p, main, side, out);
    set(p, "mode", static_cast<float>(mode));
    set(p, "mix", mix);
    set(p, "gain", gain);
    p.prepareToPlay(48000, blockSize);
    require(p.getLatencySamples() == 0 && p.getTailLengthSeconds() == 0, "Multiplication has no latency or tail");
    std::vector<float> result;
    juce::MidiBuffer midi;
    for (int start = 0; start < 257; start += blockSize)
    {
        const int count = std::min(blockSize, 257 - start);
        juce::AudioBuffer<float> block(std::max(main + side, out), count);
        block.clear();
        for (int c = 0; c < main + side; ++c)
            for (int i = 0; i < count; ++i) block.setSample(c, i, signal(c, start + i));
        p.processBlock(block, midi);
        for (int i = 0; i < count; ++i)
        {
            const float aL = signal(0, start + i), aR = signal(main - 1, start + i);
            const bool useSide = main == 1 || mode == 1;
            const float dL = useSide ? aL : (aL + aR) * 0.5f;
            const float dR = useSide ? aR : dL;
            const float wL = !useSide ? aL * aR : (side == 0 ? aL : aL * signal(main, start + i));
            const float wR = !useSide ? wL : (side == 0 ? aR : aR * signal(main + side - 1, start + i));
            const float left = ((1 - mix * 0.01f) * dL + mix * 0.01f * wL) * gain;
            const float right = ((1 - mix * 0.01f) * dR + mix * 0.01f * wR) * gain;
            for (int c = 0; c < out; ++c)
            {
                const float actual = block.getSample(c, i);
                near(actual, out == 1 ? (left + right) * 0.5f : (c == 0 ? left : right),
                     "Every sample must match the product, dry/wet blend, gain, and channel mapping");
                result.push_back(actual);
            }
        }
    }
    return result;
}

std::vector<float> renderAutomation(int blockSize)
{
    DistMultiplyProcessor p;
    set(p, "mix", 0);
    p.prepareToPlay(48000, blockSize);
    set(p, "mix", 100);
    set(p, "gain", 2);
    juce::MidiBuffer midi;
    std::vector<float> result;
    for (int start = 0; start < 600; start += blockSize)
    {
        const int count = std::min(blockSize, 600 - start);
        juce::AudioBuffer<float> block(2, count);
        for (int i = 0; i < count; ++i)
        {
            block.setSample(0, i, 0.5f);
            block.setSample(1, i, -0.25f);
        }
        p.processBlock(block, midi);
        for (int i = 0; i < count; ++i) result.push_back(block.getSample(0, i));
    }
    require(result.front() > 0.12f, "Automation must ramp instead of jumping");
    near(result[479], -0.25f, "Gain and Mix must reach their targets after 10 ms");
    near(result.back(), -0.25f, "Automation must remain at the target");
    return result;
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI gui;
    for (int main : {1, 2})
        for (int side : {0, 1, 2})
            for (int out : {1, 2})
                for (int mode : {0, 1})
                    for (float mix : {0.0f, 37.0f, 100.0f})
                        for (float gain : {0.0f, 1.0f, 2.0f})
                            require(render(1, main, side, out, mode, mix, gain)
                                        == render(127, main, side, out, mode, mix, gain),
                                    "Output must be independent of host block size");
    require(renderAutomation(1) == renderAutomation(127), "Smoothing must be independent of block size");

    // Full-wet multiplication must not lose tiny products to cancellation with
    // the much larger dry signal, nor apply the reference plugin's silence gate.
    DistMultiplyProcessor quiet;
    layout(quiet, 1, 1, 2);
    quiet.prepareToPlay(48000, 1);
    juce::AudioBuffer<float> tiny(2, 1);
    tiny.setSample(0, 0, 0.5f);
    tiny.setSample(1, 0, 1.0e-9f);
    juce::MidiBuffer midi;
    quiet.processBlock(tiny, midi);
    require(tiny.getSample(0, 0) == 0.5e-9f && tiny.getSample(1, 0) == 0.5e-9f,
            "Tiny products must survive full-wet mixing exactly, including aliased output");

    DistMultiplyProcessor p;
    require(p.getParameters().size() == 3, "Only Mode, Gain, and Mix must be exposed");
    require(p.parameters.getRawParameterValue("mode")->load() == 0, "Default Mode must be L/R");
    require(p.parameters.getRawParameterValue("gain")->load() == 1, "Default Gain must be 1x");
    require(p.parameters.getRawParameterValue("mix")->load() == 100, "Default Mix must be 100%");
    for (int bus : {0, 1})
    {
        auto buses = p.getBusesLayout();
        buses.inputBuses.set(bus, juce::AudioChannelSet::create5point1());
        require(!p.isBusesLayoutSupported(buses), "Surround inputs must be rejected");
    }
    auto buses = p.getBusesLayout();
    buses.outputBuses.set(0, juce::AudioChannelSet::create5point1());
    require(!p.isBusesLayoutSupported(buses), "Surround output must be rejected");
    buses = p.getBusesLayout();
    buses.inputBuses.set(0, juce::AudioChannelSet::disabled());
    require(!p.isBusesLayoutSupported(buses), "Main input must be enabled");

    set(p, "mode", 1); set(p, "gain", 1.75f); set(p, "mix", 37);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    DistMultiplyProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const char* id : {"mode", "gain", "mix"})
        require(p.parameters.getRawParameterValue(id)->load() == restored.parameters.getRawParameterValue(id)->load(),
                "All parameters must round-trip through host state");
    const char invalid[] = "invalid state";
    restored.setStateInformation(invalid, sizeof(invalid));
    near(restored.parameters.getRawParameterValue("mix")->load(), 37, "Invalid state must be ignored");
    auto foreign = p.parameters.copyState().createCopy();
    juce::ValueTree wrongType("DistInterleaveState");
    wrongType.copyPropertiesAndChildrenFrom(foreign, nullptr);
    wrongType.getChildWithProperty("id", "mix").setProperty("value", 99.0f, nullptr);
    juce::MemoryBlock foreignState;
    juce::AudioProcessor::copyXmlToBinary(*wrongType.createXml(), foreignState);
    restored.setStateInformation(foreignState.getData(), static_cast<int>(foreignState.getSize()));
    near(restored.parameters.getRawParameterValue("mix")->load(), 37, "Another plugin's state must be ignored");
    std::cout << "Processor tests passed: multiplication, routing, smoothing, defaults, and state\n";
}
