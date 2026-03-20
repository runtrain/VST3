// PluginProcessor.cpp
// All the audio processing logic for RolyPolyFix.
// Read top-to-bottom: parameters → prepareToPlay → processBlock.

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Scale tables ──────────────────────────────────────────────────────────────
// Semitone intervals from the root note for each scale.
// e.g. C Major = C(0) D(2) E(4) F(5) G(7) A(9) B(11)
const int RolyPolyFixAudioProcessor::CHROMATIC  [12] = {0,1,2,3,4,5,6,7,8,9,10,11};
const int RolyPolyFixAudioProcessor::MAJOR      [7]  = {0,2,4,5,7,9,11};
const int RolyPolyFixAudioProcessor::MINOR      [7]  = {0,2,3,5,7,8,10};
const int RolyPolyFixAudioProcessor::PENTATONIC [5]  = {0,2,4,7,9};

// ── Constructor ───────────────────────────────────────────────────────────────
RolyPolyFixAudioProcessor::RolyPolyFixAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout()),
      pitchDetector (ANALYSIS_SIZE, 44100.0f, 0.12f)
{
    analysisRing.assign   (ANALYSIS_SIZE, 0.0f);
    analysisWindow.assign (ANALYSIS_SIZE, 0.0f);
}

RolyPolyFixAudioProcessor::~RolyPolyFixAudioProcessor() {}

// ── Parameter definitions ─────────────────────────────────────────────────────
juce::AudioProcessorValueTreeState::ParameterLayout
RolyPolyFixAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Key: which note is the "root" of the scale (C = 0, C# = 1, ... B = 11)
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "key", "Key",
        juce::StringArray {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},
        0));

    // Scale: which intervals are "allowed" notes
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "scale", "Scale",
        juce::StringArray {"Chromatic","Major","Minor","Pentatonic Major"},
        0));

    // Correction Strength: 0 = no change, 1 = fully snap to scale note
    // Around 0.7–0.9 sounds natural; 1.0 is robotic.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "strength", "Correction Strength",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.80f));

    // Stabilization: how many consecutive detections before locking to a new note.
    // Higher = less likely to get roly-polies, but slower to respond to intentional note changes.
    // 1 = instant (no stabilization), 8 = very stable.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        "stabilize", "Stabilization", 1, 10, 4));

    return layout;
}

// ── prepareToPlay ─────────────────────────────────────────────────────────────
// Called by the DAW before playback starts. Set up all stateful objects here
// because sampleRate and bufferSize are only known at this point.
void RolyPolyFixAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchDetector.setSampleRate ((float)sampleRate);

    // SoundTouch setup
    int numCh = juce::jmin (getTotalNumInputChannels(), 2);
    soundTouch.setSampleRate  ((uint)sampleRate);
    soundTouch.setChannels    ((uint)numCh);
    soundTouch.setTempoChange (0.0);     // change pitch only, not tempo
    soundTouch.setPitchSemiTones (0.0f);
    soundTouch.setSetting (SETTING_USE_QUICKSEEK,  1);
    soundTouch.setSetting (SETTING_USE_AA_FILTER,  1);
    soundTouch.clear();

    // Pre-prime SoundTouch with silence so its internal buffer is full on the
    // first processBlock call. Without this, receiveSamples returns 0 for the
    // first ~ANALYSIS_SIZE samples, which causes a zero-filled gap → click.
    {
        std::vector<float> zeros ((size_t)(ANALYSIS_SIZE * numCh), 0.0f);
        soundTouch.putSamples (zeros.data(), (uint)ANALYSIS_SIZE);
    }

    // Pre-allocate I/O interleave buffers for worst-case block size
    stInput .resize ((size_t)(samplesPerBlock * numCh));
    stOutput.resize ((size_t)(samplesPerBlock * numCh));

    // Smooth pitch transitions over 80ms to avoid clicks/zipper noise
    smoothedShift.reset ((int)sampleRate, 0.08);
    smoothedShift.setCurrentAndTargetValue (0.0f);

    // Declare our latency to the host so it can compensate (for track sync)
    setLatencySamples (ANALYSIS_SIZE / 2);

    // Reset state
    currentTargetNote  = -1;
    candidateNote      = -1;
    candidateCount     = 0;
    ringWritePos       = 0;
    samplesSinceAnalysis = 0;
    detectedHz   = -1.0f;
    detectedNote = -1;
    targetNote   = -1;
    correcting   = false;
}

void RolyPolyFixAudioProcessor::releaseResources()
{
    soundTouch.clear();
}

// ── Math helpers ──────────────────────────────────────────────────────────────

float RolyPolyFixAudioProcessor::freqToMidiF (float hz)
{
    if (hz <= 0.0f) return -1.0f;
    // MIDI note 69 = A4 = 440 Hz
    return 12.0f * std::log2f (hz / 440.0f) + 69.0f;
}

float RolyPolyFixAudioProcessor::midiToFreq (float midi)
{
    return 440.0f * std::powf (2.0f, (midi - 69.0f) / 12.0f);
}

juce::String RolyPolyFixAudioProcessor::midiNoteToName (int midi)
{
    if (midi < 0) return "--";
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = (midi / 12) - 1;
    return juce::String (names[midi % 12]) + juce::String (octave);
}

// ── Scale quantization ────────────────────────────────────────────────────────
// Returns the MIDI note number that is closest to 'midiNote' while being
// a member of the given scale (rooted at rootKey).
int RolyPolyFixAudioProcessor::quantizeToScale (int midiNote, int rootKey, int scaleIndex)
{
    const int* intervals = nullptr;
    int count = 0;
    switch (scaleIndex)
    {
        case 1:  intervals = MAJOR;      count = MAJOR_COUNT;      break;
        case 2:  intervals = MINOR;      count = MINOR_COUNT;      break;
        case 3:  intervals = PENTATONIC; count = PENTATONIC_COUNT; break;
        default: intervals = CHROMATIC;  count = CHROMATIC_COUNT;  break;
    }

    int bestNote = midiNote;
    int minDist  = 100;

    // Search across nearby octaves to find the closest allowed note
    for (int octave = -2; octave <= 2; ++octave)
    {
        for (int i = 0; i < count; ++i)
        {
            int candidate = rootKey + octave * 12 + intervals[i];
            int dist = std::abs (midiNote - candidate);
            if (dist < minDist)
            {
                minDist  = dist;
                bestNote = candidate;
            }
        }
    }

    return bestNote;
}

// ── Note stabilizer ───────────────────────────────────────────────────────────
// Prevents roly-polies: we don't switch the target note until the same
// quantized note has been detected 'requiredCount' times in a row.
void RolyPolyFixAudioProcessor::updateStabilizer (int quantizedNote, int requiredCount)
{
    if (quantizedNote < 0)
    {
        // Silence detected — reset candidate tracking but hold the current target
        candidateCount = 0;
        return;
    }

    if (quantizedNote == currentTargetNote)
    {
        // Still on the same note — nothing to do
        candidateNote  = -1;
        candidateCount = 0;
        return;
    }

    if (quantizedNote == candidateNote)
    {
        // Seen this new candidate again — increment confidence
        ++candidateCount;
        if (candidateCount >= requiredCount)
        {
            // Confident — switch target
            currentTargetNote = candidateNote;
            candidateNote     = -1;
            candidateCount    = 0;
        }
    }
    else
    {
        // A different candidate appeared — start tracking it from 1
        candidateNote  = quantizedNote;
        candidateCount = 1;
    }
}

// ── processBlock ─────────────────────────────────────────────────────────────
// This runs continuously during playback — hundreds of times per second.
// Everything here must be fast and must not allocate memory or block.
void RolyPolyFixAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                               juce::MidiBuffer& /*midi*/)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    const int numST       = soundTouch.numChannels(); // 1 or 2

    // ── 1. Accumulate audio for pitch detection ───────────────────────────
    // We only analyse channel 0 (left/mono). The pitch detection needs about
    // 2048 samples before it can make a measurement, so we buffer the incoming
    // audio in a circular ring buffer and run the analysis every HOP_SIZE samples.
    const float* ch0 = buffer.getReadPointer (0);

    for (int i = 0; i < numSamples; ++i)
    {
        analysisRing[ringWritePos] = ch0[i];
        ringWritePos = (ringWritePos + 1) % ANALYSIS_SIZE;
        ++samplesSinceAnalysis;

        if (samplesSinceAnalysis >= HOP_SIZE)
        {
            samplesSinceAnalysis = 0;

            // Copy the circular buffer into a contiguous window (oldest → newest)
            for (int j = 0; j < ANALYSIS_SIZE; ++j)
                analysisWindow[j] = analysisRing[(ringWritePos + j) % ANALYSIS_SIZE];

            // Run YIN pitch detection
            float hz = pitchDetector.detectPitch (analysisWindow.data(), ANALYSIS_SIZE);
            detectedHz.store (hz);

            // Read current parameter values
            int rootKey    = (int)apvts.getRawParameterValue ("key")->load() + 60; // C4 = MIDI 60
            int scaleIdx   = (int)apvts.getRawParameterValue ("scale")->load();
            int stabilize  = (int)apvts.getRawParameterValue ("stabilize")->load();

            if (hz > 0.0f)
            {
                int rawNote  = (int)std::round (freqToMidiF (hz));
                int snapNote = quantizeToScale (rawNote, rootKey, scaleIdx);
                detectedNote.store (rawNote);

                updateStabilizer (snapNote, stabilize);
                targetNote.store (currentTargetNote);
            }
            else
            {
                detectedNote.store (-1);
                // During silence we hold the last target note (don't reset it)
                // so corrections stay in place when the vocalist briefly pauses.
            }
        }
    }

    // ── 2. Calculate how many semitones to shift ──────────────────────────
    // If the detected note doesn't match the target (stabilized) note,
    // we need to shift the pitch to close that gap.
    float targetShiftSemitones = 0.0f;

    float hz = detectedHz.load();
    if (hz > 0.0f && currentTargetNote >= 0)
    {
        float detectedNoteF = freqToMidiF (hz);          // e.g. 63.7
        float shiftNeeded   = (float)currentTargetNote - detectedNoteF;

        // Octave fold: collapse the shift to [-6, +6] semitones so that octave
        // errors in YIN (e.g. detecting a harmonic instead of the fundamental)
        // never cause large correction jumps that multiply pitch wobble.
        shiftNeeded -= 12.0f * std::round (shiftNeeded / 12.0f);

        float strength = apvts.getRawParameterValue ("strength")->load();
        targetShiftSemitones = shiftNeeded * strength;

        // Clamp to ±3 semitones to prevent runaway corrections
        targetShiftSemitones = juce::jlimit (-3.0f, 3.0f, targetShiftSemitones);

        correcting.store (std::abs (targetShiftSemitones) > 0.02f);
    }
    else
    {
        correcting.store (false);
    }

    // Smooth the shift value to avoid sudden jumps (clicks/zipper noise)
    smoothedShift.setTargetValue (targetShiftSemitones);
    // Advance the smoother by the block size and take the end value
    float shiftNow = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        shiftNow = smoothedShift.getNextValue();

    soundTouch.setPitchSemiTones (shiftNow);

    // ── 3. Convert JUCE non-interleaved audio → SoundTouch interleaved ───
    // JUCE:      [L0 L1 L2 ...] [R0 R1 R2 ...]
    // SoundTouch: [L0 R0 L1 R1 L2 R2 ...]
    if ((size_t)(numSamples * numST) > stInput.size())
        stInput.resize ((size_t)(numSamples * numST));
    if ((size_t)(numSamples * numST) > stOutput.size())
        stOutput.resize ((size_t)(numSamples * numST));

    if (numST == 2 && numChannels >= 2)
    {
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (1);
        for (int i = 0; i < numSamples; ++i)
        {
            stInput[i * 2]     = L[i];
            stInput[i * 2 + 1] = R[i];
        }
    }
    else
    {
        const float* M = buffer.getReadPointer (0);
        for (int i = 0; i < numSamples; ++i)
            stInput[i] = M[i];
    }

    // ── 4. Run SoundTouch ─────────────────────────────────────────────────
    soundTouch.putSamples (stInput.data(), (uint)numSamples);
    int received = (int)soundTouch.receiveSamples (stOutput.data(), (uint)numSamples);

    // ── 5. Write output back to buffer ────────────────────────────────────
    if (numST == 2 && numChannels >= 2)
    {
        float* L = buffer.getWritePointer (0);
        float* R = buffer.getWritePointer (1);
        for (int i = 0; i < received; ++i)
        {
            L[i] = stOutput[i * 2];
            R[i] = stOutput[i * 2 + 1];
        }
        // If SoundTouch returned fewer samples than expected, zero the rest
        for (int i = received; i < numSamples; ++i)
            L[i] = R[i] = 0.0f;
    }
    else
    {
        float* M = buffer.getWritePointer (0);
        for (int i = 0; i < received; ++i)
            M[i] = stOutput[i];
        for (int i = received; i < numSamples; ++i)
            M[i] = 0.0f;
    }
}

// ── Boilerplate ───────────────────────────────────────────────────────────────

juce::AudioProcessorEditor* RolyPolyFixAudioProcessor::createEditor()
{
    return new RolyPolyFixAudioProcessorEditor (*this);
}
bool RolyPolyFixAudioProcessor::hasEditor() const { return true; }

const juce::String RolyPolyFixAudioProcessor::getName() const { return JucePlugin_Name; }
bool   RolyPolyFixAudioProcessor::acceptsMidi()  const { return false; }
bool   RolyPolyFixAudioProcessor::producesMidi() const { return false; }
bool   RolyPolyFixAudioProcessor::isMidiEffect() const { return false; }
double RolyPolyFixAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int  RolyPolyFixAudioProcessor::getNumPrograms()    { return 1; }
int  RolyPolyFixAudioProcessor::getCurrentProgram() { return 0; }
void RolyPolyFixAudioProcessor::setCurrentProgram (int) {}
const juce::String RolyPolyFixAudioProcessor::getProgramName (int) { return {}; }
void RolyPolyFixAudioProcessor::changeProgramName (int, const juce::String&) {}

void RolyPolyFixAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void RolyPolyFixAudioProcessor::setStateInformation (const void* data, int size)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, size));
    if (xml && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RolyPolyFixAudioProcessor();
}
