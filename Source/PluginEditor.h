#pragma once
// PluginEditor.h
// This file declares the UI (visual interface) of your plugin.
// The editor is what you SEE when you open the plugin in your DAW.

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SimpleGainAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit SimpleGainAudioProcessorEditor (SimpleGainAudioProcessor& p);
    ~SimpleGainAudioProcessorEditor() override;

    // paint() draws graphics (backgrounds, labels, etc.)
    void paint (juce::Graphics& g) override;

    // resized() is called when the window is created or resized.
    // Use it to set the position and size of your UI components.
    void resized() override;

private:
    // Reference back to the processor so we can read/write parameters
    SimpleGainAudioProcessor& audioProcessor;

    // A rotary knob (dial) for the gain control
    juce::Slider gainSlider;

    // A text label below the knob
    juce::Label gainLabel;

    // Attachment links the slider to the "gain" APVTS parameter automatically.
    // When the user moves the slider, the parameter updates.
    // When the DAW automates the parameter, the slider moves.
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGainAudioProcessorEditor)
};
