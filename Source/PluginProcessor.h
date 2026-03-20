#pragma once
// PluginProcessor.h
// Declares the RolyPolyFix audio processor.
// This class handles all audio processing: pitch detection, note stabilization,
// and pitch correction via the SoundTouch library.

#include <JuceHeader.h>
#include "PitchDetector.h"
#include <SoundTouch.h>

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
    // These are atomics so the UI can read them safely without locking.
    std::atomic<float> detectedHz    { -1.0f }; // raw detected frequency
    std::atomic<int>   detectedNote  { -1 };     // rounded to nearest MIDI note
    std::atomic<int>   targetNote    { -1 };     // the stabilized, scale-corrected note
    std::atomic<bool>  correcting    { false };  // true when a correction is being applied

    // Convert MIDI note number to readable name, e.g. 69 → "A4"
    static juce::String midiNoteToName (int midiNote);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // ── Scale definitions (semitone intervals from root) ──────────────────
    // Each array lists which intervals belong to that scale.
    // e.g. Major = root, whole, whole, half, whole, whole, whole, half
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

    // We accumulate samples here until we have enough for one YIN analysis.
    static constexpr int ANALYSIS_SIZE = 2048;   // ~46ms at 44100 Hz
    static constexpr int HOP_SIZE      = 512;    // re-analyse every ~12ms

    std::vector<float> analysisRing;    // circular buffer of audio
    std::vector<float> analysisWindow;  // pre-allocated window for YIN (avoids audio-thread alloc)
    int   ringWritePos         = 0;
    int   samplesSinceAnalysis = 0;

    // ── Note stabilizer ───────────────────────────────────────────────────
    // Prevents roly-polies: a new note must be detected N times in a row
    // before we switch the target. N is controlled by the Stabilization knob.
    int currentTargetNote = -1;  // the note we're currently correcting to
    int candidateNote     = -1;  // the note we might switch to
    int candidateCount    = 0;   // how many times in a row we've seen it

    void updateStabilizer (int quantizedNote, int requiredCount, float rawMidiF);

    // ── Scale quantization ────────────────────────────────────────────────
    // Snaps a MIDI note to the nearest note allowed in the current scale.
    int quantizeToScale (int midiNote, int rootKey, int scaleIndex);

    // ── Pitch shifting (SoundTouch) ───────────────────────────────────────
    soundtouch::SoundTouch soundTouch;

    // Smooths the final shift value over 120ms to prevent clicks when the
    // correction amount changes (especially at onset and note transitions)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedShift;

    // Temporary interleaved buffers for SoundTouch I/O
    // (JUCE uses separate L/R arrays; SoundTouch uses interleaved LRLRLR...)
    std::vector<float> stInput;
    std::vector<float> stOutput;

    // ── Math helpers ──────────────────────────────────────────────────────
    static float freqToMidiF  (float hz);      // 440.0 Hz → 69.0
    static float midiToFreq   (float midi);    // 69.0 → 440.0 Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RolyPolyFixAudioProcessor)
};
