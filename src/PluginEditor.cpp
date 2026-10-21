#include "PluginEditor.h"

using namespace vspd;
namespace
{
constexpr int designWidth = 780, designHeight = 390;
constexpr auto defaultHint = "shift-drag fine  .  double-click reset  .  type values";
void setHelp(juce::Component& component, const juce::String& text)
{
    component.getProperties().set("help", text);
}
void caption(juce::Graphics& g, const juce::String& text, float x, float y)
{
    g.setFont(uiFont(9.5f));
    g.setColour(juce::Colour(col::mid));
    g.drawSingleLineText(text, juce::roundToInt(x), juce::roundToInt(y), juce::Justification::horizontallyCentred);
}
}

DistMultiplyEditor::DistMultiplyEditor(DistMultiplyProcessor& p)
    : AudioProcessorEditor(p), effectProcessor(p)
{
    setLookAndFeel(&look);
    addMouseListener(this, true);
    addAndMakeVisible(surface);
    surface.setInterceptsMouseClicks(false, true);
    for (auto* component : std::initializer_list<juce::Component*>{&mode, &oversampling, &gain, &mix, &dcBlock, &mainOffset, &sidechainOffset})
        surface.addAndMakeVisible(component);
    mode.addItemList({"L/R", "In/Sidechain"}, 1);
    oversampling.addItemList({"1x", "2x", "4x"}, 1);
    setHelp(oversampling, "Oversampling reduces multiplication aliasing. Fixed host-compensated latency at every setting.");
    const auto setupKnob = [](juce::Slider& slider, double initial, const juce::String& help)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setRotaryParameters(kRotaryStart, kRotaryEnd, true);
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 112, 26);
        slider.setDoubleClickReturnValue(true, initial);
        setHelp(slider, help);
    };
    setupKnob(gain, 1.0, "Linear output gain after the dry/wet blend. 0-2x; no limiter or clipping stage.");
    setupKnob(mix, 100.0, "Dry/wet blend. L/R dry is the mono average of both inputs.");
    setupKnob(dcBlock, 15.0, "Final output high-pass filter removes DC. 0-20 Hz; 0 Hz bypasses the filter.");
    setupKnob(mainOffset, 0.0, "Offset added to main inputs before multiplication. -1 to +1; dry signal is unchanged.");
    setupKnob(sidechainOffset, 0.0, "Offset added to sidechain before multiplication. -1 to +1; requires a connected sidechain.");
    oversamplingAttachment = std::make_unique<ComboAttachment>(p.parameters, "oversampling", oversampling);
    dcAttachment = std::make_unique<SliderAttachment>(p.parameters, "dcBlock", dcBlock);
    mainOffsetAttachment = std::make_unique<SliderAttachment>(p.parameters, "mainOffset", mainOffset);
    sidechainOffsetAttachment = std::make_unique<SliderAttachment>(p.parameters, "sidechainOffset", sidechainOffset);
    modeAttachment = std::make_unique<ComboAttachment>(p.parameters, "mode", mode);
    gainAttachment = std::make_unique<SliderAttachment>(p.parameters, "gain", gain);
    mixAttachment = std::make_unique<SliderAttachment>(p.parameters, "mix", mix);
    gain.textFromValueFunction = [](double v) { return juce::String(v, 2) + " x"; };
    mix.textFromValueFunction = [](double v) { return juce::String(v, 1) + " %"; };
    dcBlock.textFromValueFunction = [](double v) { return v == 0.0 ? juce::String("Off") : juce::String(v, 1) + " Hz"; };
    for (auto* slider : {&mainOffset, &sidechainOffset})
        slider->textFromValueFunction = [](double v) { return (v > 0.0 ? "+" : "") + juce::String(v, 3); };
    for (auto* slider : {&gain, &mix, &dcBlock, &mainOffset, &sidechainOffset})
    {
        slider->valueFromTextFunction = [](const juce::String& text) { return text.getDoubleValue(); };
        slider->updateText();
    }
    mode.onChange = [this] { updateRouting(); repaint(); };
    helpMark.setText("?", juce::dontSendNotification);
    helpMark.setFont(monoFont(9.0f));
    helpMark.setJustificationType(juce::Justification::centred);
    helpMark.setColour(juce::Label::textColourId, juce::Colour(col::dim));
    setHelp(helpMark, "Hover controls for help");
    surface.addAndMakeVisible(helpMark);
    contextHelpLabel.setBorderSize({});
    contextHelpLabel.setFont(monoFont(8.0f));
    contextHelpLabel.setColour(juce::Label::textColourId, juce::Colour(col::dim).withAlpha(0.8f));
    contextHelpLabel.setJustificationType(juce::Justification::centredLeft);
    contextHelpLabel.setMinimumHorizontalScale(0.8f);
    contextHelpLabel.setInterceptsMouseClicks(false, false);
    contextHelpLabel.setText(defaultHint, juce::dontSendNotification);
    surface.addAndMakeVisible(contextHelpLabel);
    setResizable(true, true);
    setResizeLimits(624, 312, 1560, 780);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(designWidth) / designHeight);
    setSize(designWidth, designHeight);
    updateRouting();
    startTimerHz(15);
}

DistMultiplyEditor::~DistMultiplyEditor()
{
    stopTimer();
    removeMouseListener(this);
    setLookAndFeel(nullptr);
}

void DistMultiplyEditor::resized()
{
    surface.setBounds(0, 0, designWidth, designHeight);
    surface.setTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / designWidth));
    mode.setBounds(80, 114, 190, 28);
    oversampling.setBounds(480, 114, 170, 28);
    mainOffset.setBounds(34, 192, 112, 142);
    sidechainOffset.setBounds(204, 192, 112, 142);
    gain.setBounds(374, 192, 112, 142);
    mix.setBounds(514, 192, 112, 142);
    dcBlock.setBounds(654, 192, 112, 142);
    helpMark.setBounds(748, 366, 16, 18);
    contextHelpLabel.setBounds(20, 366, 718, 18);
}

void DistMultiplyEditor::mouseEnter(const juce::MouseEvent& event)
{
    hoveredHelp = nullptr;
    // Text fields and other children inherit their control's description.
    for (auto* component = event.eventComponent; component != nullptr; component = component->getParentComponent())
    {
        if (component->getProperties()["help"].toString().isNotEmpty())
        {
            hoveredHelp = component;
            break;
        }
        if (component == this) break;
    }
    updateStatus();
}

void DistMultiplyEditor::mouseExit(const juce::MouseEvent&)
{
    hoveredHelp = nullptr;
    updateStatus();
}

void DistMultiplyEditor::updateStatus()
{
    auto text = hoveredHelp != nullptr ? hoveredHelp->getProperties()["help"].toString()
                                       : juce::String(defaultHint);
    if (hoveredHelp == &helpMark)
    {
        const auto sampleRate = effectProcessor.getSampleRate();
        const auto blockSize = effectProcessor.getBlockSize();
        text << "  |  " << juce::String(sampleRate, 0) << " Hz | " << blockSize << " smp";
        if (sampleRate > 0.0) text << " | " << juce::String(blockSize * 1000.0 / sampleRate, 1) << " ms";
        if (audioDeviceName)
        {
            const auto name = audioDeviceName();
            if (name.isNotEmpty()) text << " | " << name;
        }
        text << " | v" << DISTMULTIPLY_VERSION << " (" << __DATE__ << ")";
    }
    if (contextHelpLabel.getText() != text)
        contextHelpLabel.setText(text, juce::dontSendNotification);
}

void DistMultiplyEditor::updateRouting()
{
    const bool mono = effectProcessor.isMonoInput();
    mode.setItemEnabled(1, !mono);
    mode.setEnabled(!mono);
    if (mono) mode.setSelectedId(2, juce::dontSendNotification);
    else mode.setSelectedId(juce::roundToInt(effectProcessor.parameters.getRawParameterValue("mode")->load()) + 1,
                            juce::dontSendNotification);
    setHelp(mode, mono ? "Mono input always uses In/Sidechain. Mode is fixed."
                       : "L/R multiplies left by right into mono. In/Sidechain multiplies matching channels.");
    const bool multiplying = !effectProcessor.usesSidechain() || effectProcessor.hasSidechain();
    mainOffset.setEnabled(multiplying);
    sidechainOffset.setEnabled(effectProcessor.usesSidechain() && effectProcessor.hasSidechain());
    setHelp(mainOffset, !effectProcessor.usesSidechain()
        ? "Offset added to both L/R inputs before multiplication. -1 to +1; dry signal is unchanged."
        : "Offset added to main inputs before multiplication. -1 to +1; dry signal is unchanged.");
    lastMono = mono;
    lastSide = effectProcessor.usesSidechain();
    lastConnected = effectProcessor.hasSidechain();
    updateStatus();
}

void DistMultiplyEditor::timerCallback()
{
    // Also correct the visible mode when host automation changes a disabled
    // mode parameter on a mono instance: the effective mode remains sidechain.
    if (lastMono != effectProcessor.isMonoInput() || lastSide != effectProcessor.usesSidechain()
        || lastConnected != effectProcessor.hasSidechain() || (lastMono && mode.getSelectedId() != 2))
    {
        updateRouting();
        repaint();
    }
    updateStatus();
}

void DistMultiplyEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(col::background));
    g.addTransform(juce::AffineTransform::scale(static_cast<float>(getWidth()) / designWidth));
    g.setColour(juce::Colour(col::mid));
    drawTracked(g, uiFont(10.0f, true), "DISTORT : MULTIPLY", 24.0f, 25.0f, 3.0f);
    g.setFont(monoFont(8.0f));
    g.setColour(juce::Colour(col::dim));
    g.drawText("v" DISTMULTIPLY_VERSION, 670, 11, 86, 20, juce::Justification::centredRight);
    g.setColour(juce::Colour(col::divider).withAlpha(0.6f));
    g.drawHorizontalLine(38, 0.0f, static_cast<float>(designWidth));
    g.drawHorizontalLine(356, 0.0f, static_cast<float>(designWidth));
    g.drawVerticalLine(350, 52.0f, 342.0f);
    g.setColour(juce::Colour(col::heading));
    drawTrackedCentred(g, uiFont(14.0f), "SOURCES", 175.0f, 77.0f, 2.6f);
    drawTrackedCentred(g, uiFont(14.0f), "OUTPUT", 565.0f, 77.0f, 2.6f);
    caption(g, "Mode", 175.0f, 106.0f);
    caption(g, "Oversampling", 565.0f, 106.0f);
    caption(g, "Main offset", 90.0f, 185.0f);
    caption(g, "Sidechain offset", 260.0f, 185.0f);
    caption(g, "Gain", 430.0f, 185.0f);
    caption(g, "Mix", 570.0f, 185.0f);
    caption(g, "DC block", 710.0f, 185.0f);
    g.setFont(monoFont(7.5f));
    g.setColour(juce::Colour(col::dim));
    g.drawText(lastSide ? (lastMono ? "mono input / sidechain" : "main input / sidechain")
                        : "left * right -> mono", 24, 148, 302, 18, juce::Justification::centred);
    if (lastSide && !lastConnected)
    {
        g.setColour(juce::Colour(col::warn));
        g.drawText("connect sidechain / dry through", 24, 335, 302, 17, juce::Justification::centred);
    }
    else
        g.drawText(lastSide ? "independent channels" : "main offset applies to L and R",
                   24, 335, 302, 17, juce::Justification::centred);
    g.setColour(juce::Colour(col::dim));
    g.drawText("0 - 2x", 374, 335, 112, 18, juce::Justification::centred);
    g.drawText("dry / wet", 514, 335, 112, 18, juce::Justification::centred);
    g.drawText("0 Hz = off", 654, 335, 112, 18, juce::Justification::centred);
}
