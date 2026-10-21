#pragma once
#include "PluginProcessor.h"
#include "LookAndFeel.h"

class FineSlider final : public juce::Slider
{
public:
    void mouseDown(const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity(e.mods.isShiftDown() ? 1800 : 250);
        juce::Slider::mouseDown(e);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        setMouseDragSensitivity(e.mods.isShiftDown() ? 1800 : 250);
        juce::Slider::mouseDrag(e);
    }
};

class DistMultiplyEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DistMultiplyEditor(DistMultiplyProcessor&);
    ~DistMultiplyEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    std::function<juce::String()> audioDeviceName;
private:
    void timerCallback() override;
    void updateRouting();
    void updateStatus();
    DistMultiplyProcessor& effectProcessor;
    VarispeedLookAndFeel look;
    juce::Component surface;
    juce::ComboBox mode, oversampling;
    FineSlider gain, mix, dcBlock, mainOffset, sidechainOffset;
    juce::Label helpMark, contextHelpLabel;
    juce::Component::SafePointer<juce::Component> hoveredHelp;
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    std::unique_ptr<ComboAttachment> modeAttachment, oversamplingAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment, mixAttachment, dcAttachment, mainOffsetAttachment, sidechainOffsetAttachment;
    bool lastMono = false, lastSide = false, lastConnected = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DistMultiplyEditor)
};
