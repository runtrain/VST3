#pragma once
// PluginProcessor.h
// This file DECLARES your plugin's audio processing class.
// Think of it as a "table of contents" — it lists what the plugin can do,
// and PluginProcessor.cpp contains the actual instructions.

#include <JuceHeader.h>

// AudioProcessorValueTreeState (APVTS) manages all your plugin's parameters.
// It handles saving/loading, automation in DAWs, and connecting UI controls
// to audio processing — all automatically.

class SimpleGainAudioProcessor : public juce::AudioProcessor
{
public:
    SimpleGainAudioProcessor();
    ~SimpleGainAudioProcessor() override;

    // ── Called by the DAW before playback starts ──────────────────────────
    // Use this to set up anything that depends on sample rate or buffer size.
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    // ── Called by the DAW after playback stops ────────────────────────────
    void releaseResources() override;

    // ── Called repeatedly during playback — this is where audio is processed
    // buffer: contains the audio samples coming in; you modify it in place
    // midiMessages: MIDI data (we're not using it for this effect plugin)
    void processBlock (juce::AudioBuffer<float>& buffer,
                       juce::MidiBuffer& midiMessages) override;

    // ── UI editor ─────────────────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    // ── Plugin identity ───────────────────────────────────────────────────
    const juce::String getName() const override;
    bool   acceptsMidi() const override;
    bool   producesMidi() const override;
    bool   isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    // ── Preset handling (programs) ────────────────────────────────────────
    int  getNumPrograms() override;
    int  getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    // ── Save / Load plugin state ──────────────────────────────────────────
    // The DAW calls these to save your project and reload it later.
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ── Parameters ────────────────────────────────────────────────────────
    // APVTS stores all knob/slider values and handles automation for you.
    juce::AudioProcessorValueTreeState apvts;

private:
    // This helper function defines all the parameters your plugin has.
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // SmoothedValue prevents clicks when the gain knob is turned quickly.
    // Instead of jumping instantly to the new value, it glides smoothly.
    juce::SmoothedValue<float> smoothedGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SimpleGainAudioProcessor)
};
