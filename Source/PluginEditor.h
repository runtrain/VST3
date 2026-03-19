#pragma once
// PluginEditor.h
// Declares the visual interface for RolyPolyFix.

#include <JuceHeader.h>
#include "PluginProcessor.h"

class RolyPolyFixAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit RolyPolyFixAudioProcessorEditor (RolyPolyFixAudioProcessor&);
    ~RolyPolyFixAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;

private:
    // Called by the Timer every ~100ms to refresh the note display
    void timerCallback() override;

    RolyPolyFixAudioProcessor& processor;

    // ── Controls ──────────────────────────────────────────────────────────
    juce::ComboBox keyBox;       // Key selector (C, C#, D, ...)
    juce::ComboBox scaleBox;     // Scale selector (Major, Minor, ...)
    juce::Slider   strengthSlider;
    juce::Slider   stabilizeSlider;

    // Labels above/below each control
    juce::Label keyLabel;
    juce::Label scaleLabel;
    juce::Label strengthLabel;
    juce::Label stabilizeLabel;

    // ── Note display ──────────────────────────────────────────────────────
    // Shows what the plugin detects and what it's correcting to in realtime.
    juce::Label detectedNoteLabel;
    juce::Label targetNoteLabel;
    juce::Label statusLabel;  // "Correcting" or "On target"

    // ── APVTS attachments ─────────────────────────────────────────────────
    // These keep the UI controls in sync with the processor parameters.
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> scaleAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   strengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   stabilizeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RolyPolyFixAudioProcessorEditor)
};
