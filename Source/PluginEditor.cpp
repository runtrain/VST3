// PluginEditor.cpp
// This file implements the visual interface of your plugin.
// Modify this file to change what the plugin looks like.

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Constructor: build the UI ─────────────────────────────────────────────────
SimpleGainAudioProcessorEditor::SimpleGainAudioProcessorEditor (SimpleGainAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // ── Gain Knob ─────────────────────────────────────────────────────────
    gainSlider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    gainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 20);
    gainSlider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colours::cornflowerblue);
    addAndMakeVisible (gainSlider);

    // Link the slider to the "gain" parameter (defined in PluginProcessor.cpp)
    gainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        audioProcessor.apvts, "gain", gainSlider);

    // ── Label ─────────────────────────────────────────────────────────────
    gainLabel.setText ("Gain", juce::dontSendNotification);
    gainLabel.setJustificationType (juce::Justification::centred);
    gainLabel.setFont (juce::Font (16.0f, juce::Font::bold));
    addAndMakeVisible (gainLabel);

    // Set the size of the plugin window (width x height in pixels)
    setSize (200, 200);
}

SimpleGainAudioProcessorEditor::~SimpleGainAudioProcessorEditor() {}

// ── Paint: draw the background and any custom graphics ───────────────────────
// g is a drawing context — use it to fill shapes, draw text, etc.
void SimpleGainAudioProcessorEditor::paint (juce::Graphics& g)
{
    // Fill the background with a dark colour
    g.fillAll (juce::Colour (0xff1e1e2e));

    // Draw the plugin title at the top
    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawFittedText ("Simple Gain", getLocalBounds().removeFromTop (40),
                      juce::Justification::centred, 1);
}

// ── Resized: position your UI components ─────────────────────────────────────
// This is called once when the window opens, and again if it's resized.
// getLocalBounds() returns a rectangle covering the full plugin window.
void SimpleGainAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Reserve 40px at the top for the title
    area.removeFromTop (40);

    // Reserve 25px at the bottom for the label
    gainLabel.setBounds (area.removeFromBottom (25));

    // The rest is for the knob
    gainSlider.setBounds (area.reduced (20));
}
