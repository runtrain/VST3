#pragma once
// PluginProcessor.h
// Declares the RolyPolyFix audio processor.
// This class handles all audio processing: pitch detection, note stabilization,
// and pitch correction via a custom PSOLA pitch shifter.

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PhaseVocoderShifter.h"

class RolyPolyFixAudioProcessor : public juce::AudioProcessor
{
public:
    RolyPolyFixAudioProcessor();
    ~RolyPolyFixAudioProcessor() override;

    void prepareToPlay  (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock   (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName()          const override;
    bool   acceptsMidi()                  const override;
    bool   producesMidi()                 const override;
    bool   isMidiEffect()                 const override;
    double getTailLengthSeconds()         const override;

    int  getNumPrograms()                          override;
    int  getCurrentProgram()                       override;
    void setCurrentProgram (int)                   override;
    const juce::String getProgramName (int)        override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock&)         override;
    void setStateInformation (const void*, int)           override;

    // ── Parameters ────────────────────────────────────────────────────────
    juce::AudioProcessorValueTreeState apvts;

    // ── Status for the UI (written from audio thread, read from UI thread) ─
    std::atomic<float> detectedHz    { -1.0f };
    std::atomic<int>   detectedNote  { -1 };
    std::atomic<int>   targetNote    { -1 };
    std::atomic<bool>  correcting    { false };

    static juce::String midiNoteToName (int midiNote);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Scale definitions ─────────────────────────────────────────────────
    static constexpr int CHROMATIC_COUNT   = 12;
    static constexpr int MAJOR_COUNT       = 7;
    static constexpr int MINOR_COUNT       = 7;
    static constexpr int PENTATONIC_COUNT  = 5;

    static const int CHROMATIC  [12];
    static const int MAJOR      [7];
    static const int MINOR      [7];
    static const int PENTATONIC [5];

    // ── Pitch detection ───────────────────────────────────────────────────
    PitchDetector pitchDetector;

    static constexpr int ANALYSIS_SIZE = 2048;
    static constexpr int HOP_SIZE      = 512;

    std::vector<float> analysisRing;
    std::vector<float> analysisWindow;
    int   ringWritePos         = 0;
    int   samplesSinceAnalysis = 0;

    // ── Note stabilizer ───────────────────────────────────────────────────
    int currentTargetNote = -1;
    int candidateNote     = -1;
    int candidateCount    = 0;

    void updateStabilizer (int quantizedNote, int requiredCount, float rawMidiF);

    // ── Scale quantization ────────────────────────────────────────────────
    int quantizeToScale (int midiNote, int rootKey, int scaleIndex);

    // ── Phase vocoder pitch shifter ───────────────────────────────────────
    // One shifter per channel (left and right processed independently)
    PhaseVocoderShifter psolaL;
    PhaseVocoderShifter psolaR;

    // Current detected T0 (shared between channels — same singer, same pitch)
    float currentT0 = 0.0f;

    // ── Math helpers ──────────────────────────────────────────────────────
    static float freqToMidiF  (float hz);
    static float midiToFreq   (float midi);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RolyPolyFixAudioProcessor)
};
