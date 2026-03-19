// PluginProcessor.cpp
// This file IMPLEMENTS the audio processing logic declared in PluginProcessor.h
// This is the "brain" of your plugin — it's where audio actually gets changed.

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Constructor: runs once when the plugin is loaded ─────────────────────────
SimpleGainAudioProcessor::SimpleGainAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      // Initialize the APVTS with our parameter layout (defined below)
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

SimpleGainAudioProcessor::~SimpleGainAudioProcessor() {}

// ── Define your plugin's parameters ──────────────────────────────────────────
// This is called once when the plugin loads. Add a new entry here for each
// knob, slider, or button you want in your plugin.
juce::AudioProcessorValueTreeState::ParameterLayout
SimpleGainAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Add a "Gain" parameter:
    //   ID:      "gain"        — internal name used in code
    //   Name:    "Gain"        — displayed in the DAW's automation list
    //   Range:   0.0 to 2.0   — 0 = silence, 1.0 = original volume, 2.0 = double
    //   Default: 1.0          — starts at unity gain (no change)
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "gain",             // parameter ID
        "Gain",             // parameter name
        juce::NormalisableRange<float> (0.0f, 2.0f, 0.01f),  // min, max, step
        1.0f                // default value
    ));

    return layout;
}

// ── Prepare to play ───────────────────────────────────────────────────────────
// The DAW calls this before starting playback with the actual sample rate & buffer size.
void SimpleGainAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    // Set up the smoother so it transitions over 50 milliseconds
    smoothedGain.reset (sampleRate, 0.05);
    // Start at the current gain value (no initial glide from zero)
    smoothedGain.setCurrentAndTargetValue (
        apvts.getRawParameterValue ("gain")->load());
}

void SimpleGainAudioProcessor::releaseResources() {}

// ── Process Block — runs for every chunk of audio ────────────────────────────
// This function is called hundreds of times per second.
// 'buffer' arrives containing the audio from your DAW/instrument.
// You modify it in place to produce your effect.
void SimpleGainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;  // small performance optimization

    // Read the current value of the Gain knob
    float targetGain = apvts.getRawParameterValue ("gain")->load();
    smoothedGain.setTargetValue (targetGain);

    int numChannels = buffer.getNumChannels();
    int numSamples  = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Get the next smoothed gain value (moves toward targetGain gradually)
        float gain = smoothedGain.getNextValue();

        // Apply the gain to every channel (left, right, etc.)
        for (int channel = 0; channel < numChannels; ++channel)
        {
            // Get a pointer to the raw audio samples for this channel
            float* channelData = buffer.getWritePointer (channel);
            // Multiply each sample by the gain value
            channelData[sample] *= gain;
        }
    }
}

// ── Editor (UI) ───────────────────────────────────────────────────────────────
juce::AudioProcessorEditor* SimpleGainAudioProcessor::createEditor()
{
    return new SimpleGainAudioProcessorEditor (*this);
}
bool SimpleGainAudioProcessor::hasEditor() const { return true; }

// ── Plugin identity ───────────────────────────────────────────────────────────
const juce::String SimpleGainAudioProcessor::getName() const { return JucePlugin_Name; }
bool   SimpleGainAudioProcessor::acceptsMidi()  const { return false; }
bool   SimpleGainAudioProcessor::producesMidi() const { return false; }
bool   SimpleGainAudioProcessor::isMidiEffect() const { return false; }
double SimpleGainAudioProcessor::getTailLengthSeconds() const { return 0.0; }

// ── Presets ───────────────────────────────────────────────────────────────────
int  SimpleGainAudioProcessor::getNumPrograms()    { return 1; }
int  SimpleGainAudioProcessor::getCurrentProgram() { return 0; }
void SimpleGainAudioProcessor::setCurrentProgram (int) {}
const juce::String SimpleGainAudioProcessor::getProgramName (int) { return {}; }
void SimpleGainAudioProcessor::changeProgramName (int, const juce::String&) {}

// ── Save / Load state ─────────────────────────────────────────────────────────
// The DAW calls getStateInformation to save your project.
// It calls setStateInformation when you reopen the project.
void SimpleGainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Serialize all APVTS parameters to XML and store as binary
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void SimpleGainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Restore all APVTS parameters from the saved binary data
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

// ── Entry point — tells JUCE how to create your plugin ───────────────────────
// Every JUCE plugin needs this function at the bottom.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SimpleGainAudioProcessor();
}
