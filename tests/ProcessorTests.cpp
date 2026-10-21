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
std::vector<float> render(int blockSize, int main, int side, int out, int mode, float mix, float gain,
                          float offsetA = 0.0f, float offsetB = 0.0f)
{
    DistMultiplyProcessor p;
    layout(p, main, side, out);
    set(p, "mode", static_cast<float>(mode));
    set(p, "mix", mix);
    set(p, "gain", gain);
    set(p, "oversampling", 0);
    set(p, "dcBlock", 0);
    set(p, "mainOffset", offsetA);
    set(p, "sidechainOffset", offsetB);
    p.prepareToPlay(48000, blockSize);
    require(p.getLatencySamples() > 0 && p.getTailLengthSeconds() == 0, "Fixed oversampling latency must be reported with no tail");
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
            const int sample = start + i - p.getLatencySamples();
            const float aL = sample < 0 ? 0.0f : signal(0, sample), aR = sample < 0 ? 0.0f : signal(main - 1, sample);
            const bool useSide = main == 1 || mode == 1;
            const float dL = useSide ? aL : (aL + aR) * 0.5f;
            const float dR = useSide ? aR : dL;
            const float wL = sample < 0 ? 0.0f : (!useSide ? (aL + offsetA) * (aR + offsetA)
                : (side == 0 ? aL : (aL + offsetA) * (signal(main, sample) + offsetB)));
            const float wR = sample < 0 ? 0.0f : (!useSide ? wL
                : (side == 0 ? aR : (aR + offsetA) * (signal(main + side - 1, sample) + offsetB)));
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
    set(p, "oversampling", 0);
    set(p, "dcBlock", 0);
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
    require(result[static_cast<size_t>(p.getLatencySamples())] > 0.12f, "Automation must ramp instead of jumping");
    near(result[static_cast<size_t>(479 + p.getLatencySamples())], -0.25f, "Gain and Mix must reach their targets after 10 ms");
    near(result.back(), -0.25f, "Automation must remain at the target");
    return result;
}

std::vector<float> renderFiltered(int blockSize, int selection, bool automate = false,
                                  int main = 2, int side = 2, int out = 2, int mode = 1)
{
    DistMultiplyProcessor p;
    layout(p, main, side, out);
    set(p, "mode", static_cast<float>(mode));
    set(p, "oversampling", static_cast<float>(selection));
    set(p, "mainOffset", 0.2f);
    set(p, "sidechainOffset", -0.3f);
    p.prepareToPlay(48000, 63); // Exercise blocks larger than the advertised maximum.
    if (automate)
    {
        set(p, "mainOffset", -0.8f);
        set(p, "sidechainOffset", 0.6f);
        set(p, "dcBlock", 4.0f);
        set(p, "gain", 1.5f);
        set(p, "mix", 37.0f);
    }
    std::vector<float> result;
    juce::MidiBuffer midi;
    for (int start = 0; start < 2048; start += blockSize)
    {
        const int count = std::min(blockSize, 2048 - start);
        juce::AudioBuffer<float> block(std::max(main + side, out), count);
        for (int c = 0; c < main + side; ++c)
            for (int i = 0; i < count; ++i) block.setSample(c, i, signal(c, start + i));
        p.processBlock(block, midi);
        for (int i = 0; i < count; ++i)
            for (int c = 0; c < out; ++c) result.push_back(block.getSample(c, i));
    }
    return result;
}

void testOversampling()
{
    constexpr int count = 8192;
    std::array<double, 3> aliases {};
    int fixedLatency = -1;
    for (int selection : {0, 1, 2})
    {
        DistMultiplyProcessor p;
        set(p, "oversampling", static_cast<float>(selection));
        set(p, "dcBlock", 0);
        p.prepareToPlay(48000, 127);
        if (fixedLatency < 0) fixedLatency = p.getLatencySamples();
        require(p.getLatencySamples() == fixedLatency, "All settings must report identical latency");
        juce::MidiBuffer midi;
        double real = 0, imaginary = 0, average = 0;
        // 18 kHz squared creates a 36 kHz component that aliases to 12 kHz at 1x.
        for (int start = 0; start < count * 2; start += 127)
        {
            const int size = std::min(127, count * 2 - start);
            juce::AudioBuffer<float> block(2, size);
            for (int i = 0; i < size; ++i)
                for (int c = 0; c < 2; ++c)
                    block.setSample(c, i, static_cast<float>(std::sin(2.0 * juce::MathConstants<double>::pi * 18000.0 * (start + i) / 48000.0)));
            p.processBlock(block, midi);
            for (int i = 0; i < size; ++i)
                if (start + i >= count)
                {
                    const double phase = 2.0 * juce::MathConstants<double>::pi * 12000.0 * (start + i) / 48000.0;
                    const double value = block.getSample(0, i);
                    real += value * std::cos(phase);
                    imaginary += value * std::sin(phase);
                    average += value;
                    near(block.getSample(1, i), static_cast<float>(value), "L/R must remain dual mono when oversampled");
                }
        }
        aliases[static_cast<size_t>(selection)] = 2.0 * std::hypot(real, imaginary) / count;
        require(std::abs(average / count - 0.5) < 0.005, "Oversampling must preserve the input's passband power");
    }
    require(aliases[0] > 0.49, "The reference 1x product must contain the expected alias");
    require(aliases[1] < aliases[0] * 0.01 && aliases[2] < aliases[0] * 0.01,
            "2x and 4x must reduce the multiplication alias by at least 40 dB");
}

void testDCBlock()
{
    for (float cutoff : {0.0f, 15.0f, 20.0f})
    {
        DistMultiplyProcessor p;
        layout(p, 2, 2, 2);
        set(p, "mode", 1);
        set(p, "mix", 0); // Verify that the HPF also processes the dry path, after Gain.
        set(p, "gain", 2);
        set(p, "oversampling", 0);
        set(p, "dcBlock", cutoff);
        p.prepareToPlay(48000, 127);
        juce::MidiBuffer midi;
        double previousInput = 0, previousOutput = 0;
        const double k = std::tan(juce::MathConstants<double>::pi * cutoff / 48000.0);
        const double a = (1.0 - k) / (1.0 + k), b = (1.0 + a) * 0.5;
        float last = 0;
        for (int start = 0; start < 48000; start += 127)
        {
            const int count = std::min(127, 48000 - start);
            juce::AudioBuffer<float> block(4, count);
            block.clear();
            for (int i = 0; i < count; ++i) block.setSample(0, i, 0.5f);
            p.processBlock(block, midi);
            for (int i = 0; i < count; ++i)
            {
                const double value = start + i < p.getLatencySamples() ? 0.0 : 1.0;
                const double expected = cutoff == 0.0f ? value : b * (value - previousInput) + a * previousOutput;
                previousInput = value;
                previousOutput = expected;
                near(block.getSample(0, i), static_cast<float>(expected), "DC blocker must be a final first-order HPF with the selected cutoff");
                near(block.getSample(1, i), 0, "DC filter history must stay independent per channel");
                last = block.getSample(0, i);
            }
        }
        near(last, cutoff == 0.0f ? 1.0f : 0.0f, "0 Hz must bypass; enabled DC blocking must reject a constant signal");
    }
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
    for (int main : {1, 2})
        for (int side : {0, 1, 2})
            for (int out : {1, 2})
                for (int mode : {0, 1})
                    for (float mix : {0.0f, 37.0f, 100.0f})
                        require(render(1, main, side, out, mode, mix, 1.75f, -0.3f, 0.7f)
                                    == render(127, main, side, out, mode, mix, 1.75f, -0.3f, 0.7f),
                                "Offsets must respect routing, dry blend, and block boundaries");
    require(renderAutomation(1) == renderAutomation(127), "Smoothing must be independent of block size");

    for (int main : {1, 2})
        for (int side : {0, 1, 2})
            for (int out : {1, 2})
                for (int mode : {0, 1})
                    for (int selection : {0, 1, 2})
                        for (bool automate : {false, true})
                        {
                            const auto reference = renderFiltered(1, selection, automate, main, side, out, mode);
                            const auto blocked = renderFiltered(257, selection, automate, main, side, out, mode);
                            require(reference.size() == blocked.size(), "Render sizes must match");
                            for (size_t i = 0; i < reference.size(); ++i)
                                near(blocked[i], reference[i], "Oversampling, offsets, and HPF must be independent of host block size in every layout");
                        }
    testOversampling();
    testDCBlock();

    // Full-wet multiplication must not lose tiny products to cancellation with
    // the much larger dry signal, nor apply the reference plugin's silence gate.
    DistMultiplyProcessor quiet;
    layout(quiet, 1, 1, 2);
    set(quiet, "oversampling", 0);
    set(quiet, "dcBlock", 0);
    quiet.prepareToPlay(48000, 1);
    juce::AudioBuffer<float> tiny(2, 1);
    tiny.setSample(0, 0, 0.5f);
    tiny.setSample(1, 0, 1.0e-9f);
    juce::MidiBuffer midi;
    for (int i = 0; i <= quiet.getLatencySamples(); ++i)
    {
        tiny.setSample(0, 0, 0.5f);
        tiny.setSample(1, 0, 1.0e-9f);
        quiet.processBlock(tiny, midi);
    }
    require(tiny.getSample(0, 0) == 0.5e-9f && tiny.getSample(1, 0) == 0.5e-9f,
            "Tiny products must survive full-wet mixing exactly, including aliased output");

    DistMultiplyProcessor p;
    require(p.getParameters().size() == 7, "All seven controls must be exposed");
    near(p.parameters.getRawParameterValue("oversampling")->load(), 1, "Default oversampling must be 2x");
    near(p.parameters.getRawParameterValue("dcBlock")->load(), 15, "Default DC block must be 15 Hz");
    near(p.parameters.getRawParameterValue("mainOffset")->load(), 0, "Default main offset must be zero");
    near(p.parameters.getRawParameterValue("sidechainOffset")->load(), 0, "Default sidechain offset must be zero");
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
    set(p, "oversampling", 2); set(p, "dcBlock", 8.5f);
    set(p, "mainOffset", -0.4f); set(p, "sidechainOffset", 0.7f);
    juce::MemoryBlock state;
    p.getStateInformation(state);
    DistMultiplyProcessor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    for (const char* id : {"mode", "gain", "mix", "oversampling", "dcBlock", "mainOffset", "sidechainOffset"})
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
    auto legacy = p.parameters.copyState();
    for (const auto* id : {"oversampling", "dcBlock", "mainOffset", "sidechainOffset"})
        legacy.removeChild(legacy.getChildWithProperty("id", id), nullptr);
    juce::MemoryBlock legacyState;
    juce::AudioProcessor::copyXmlToBinary(*legacy.createXml(), legacyState);
    restored.setStateInformation(legacyState.getData(), static_cast<int>(legacyState.getSize()));
    near(restored.parameters.getRawParameterValue("oversampling")->load(), 1, "Old states must receive default 2x oversampling");
    near(restored.parameters.getRawParameterValue("dcBlock")->load(), 15, "Old states must receive default 15 Hz DC block");
    near(restored.parameters.getRawParameterValue("mainOffset")->load(), 0, "Old states must receive zero main offset");
    near(restored.parameters.getRawParameterValue("sidechainOffset")->load(), 0, "Old states must receive zero sidechain offset");
    near(restored.parameters.getRawParameterValue("mix")->load(), 37, "Old states must preserve existing controls");
    std::cout << "Processor tests passed: multiplication, routing, oversampling, offsets, DC blocking, smoothing, and state\n";
}
