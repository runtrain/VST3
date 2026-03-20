// PluginProcessor.cpp
// All the audio processing logic for RolyPolyFix.
// Read top-to-bottom: parameters → prepareToPlay → processBlock.

#include "PluginProcessor.h"
#include "PluginEditor.h"

// ── Scale tables ──────────────────────────────────────────────────────────────
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

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "key", "Key",
        juce::StringArray {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"},
        0));

    layout.add (std::make_unique<juce::AudioParameterChoice> (
        "scale", "Scale",
        juce::StringArray {"Chromatic","Major","Minor","Pentatonic Major"},
        0));

    // Correction Strength: 0 = no change, 1 = fully snap to scale note.
    // Default 0.85 — strong enough to fix a miss without sounding robotic.
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        "strength", "Correction Strength",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.01f),
        0.85f));

    // Stabilization: how many consecutive detections before locking to a new note.
    layout.add (std::make_unique<juce::AudioParameterInt> (
        "stabilize", "Stabilization", 1, 10, 4));

    return layout;
}

// ── prepareToPlay ─────────────────────────────────────────────────────────────
void RolyPolyFixAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitchDetector.setSampleRate ((float)sampleRate);

    psolaL.prepare (sampleRate);
    psolaR.prepare (sampleRate);

    currentT0 = (float)(sampleRate / 220.0);  // default until first pitch detection

    // Declare our latency to the host.
    // YIN analysis window is ANALYSIS_SIZE samples; PSOLA adds ~T0 ≤ 680 samples.
    // We report ANALYSIS_SIZE/2 to the host — conservative, keeps track sync clean.
    setLatencySamples (ANALYSIS_SIZE / 2);

    // Reset state
    currentTargetNote    = -1;
    candidateNote        = -1;
    candidateCount       = 0;
    ringWritePos         = 0;
    samplesSinceAnalysis = 0;
    detectedHz   = -1.0f;
    detectedNote = -1;
    targetNote   = -1;
    correcting   = false;
}

void RolyPolyFixAudioProcessor::releaseResources()
{
    psolaL.reset();
    psolaR.reset();
}

// ── Math helpers ──────────────────────────────────────────────────────────────

float RolyPolyFixAudioProcessor::freqToMidiF (float hz)
{
    if (hz <= 0.0f) return -1.0f;
    return 12.0f * std::log2f (hz / 440.0f) + 69.0f;
}

float RolyPolyFixAudioProcessor::midiToFreq (float midi)
{
    return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f);
}

juce::String RolyPolyFixAudioProcessor::midiNoteToName (int midi)
{
    if (midi < 0) return "--";
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    int octave = (midi / 12) - 1;
    return juce::String (names[midi % 12]) + juce::String (octave);
}

// ── Scale quantization ────────────────────────────────────────────────────────
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

    for (int octave = -2; octave <= 2; ++octave)
    {
        for (int i = 0; i < count; ++i)
        {
            int candidate = rootKey + octave * 12 + intervals[i];
            int dist = std::abs (midiNote - candidate);
            if (dist < minDist) { minDist = dist; bestNote = candidate; }
        }
    }

    return bestNote;
}

// ── Note stabilizer ───────────────────────────────────────────────────────────
void RolyPolyFixAudioProcessor::updateStabilizer (int quantizedNote, int requiredCount,
                                                    float rawMidiF)
{
    if (quantizedNote < 0)
    {
        candidateCount = 0;
        return;
    }

    if (quantizedNote == currentTargetNote)
    {
        candidateNote  = -1;
        candidateCount = 0;
        return;
    }

    // Dead zone: if the singer's raw pitch is within 0.8 semitones of the
    // current target, don't consider switching notes.
    if (currentTargetNote >= 0)
    {
        float dist = rawMidiF - (float)currentTargetNote;
        dist -= 12.0f * std::round (dist / 12.0f);
        if (std::abs (dist) < 0.8f)
        {
            candidateNote  = -1;
            candidateCount = 0;
            return;
        }
    }

    if (quantizedNote == candidateNote)
    {
        ++candidateCount;
        if (candidateCount >= requiredCount)
        {
            currentTargetNote = candidateNote;
            candidateNote     = -1;
            candidateCount    = 0;
        }
    }
    else
    {
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

    // ── 1. Pitch detection ────────────────────────────────────────────────
    const float* ch0 = buffer.getReadPointer (0);

    for (int i = 0; i < numSamples; ++i)
    {
        analysisRing[ringWritePos] = ch0[i];
        ringWritePos = (ringWritePos + 1) % ANALYSIS_SIZE;
        ++samplesSinceAnalysis;

        if (samplesSinceAnalysis >= HOP_SIZE)
        {
            samplesSinceAnalysis = 0;

            for (int j = 0; j < ANALYSIS_SIZE; ++j)
                analysisWindow[j] = analysisRing[(ringWritePos + j) % ANALYSIS_SIZE];

            float hz = pitchDetector.detectPitch (analysisWindow.data(), ANALYSIS_SIZE);
            detectedHz.store (hz);

            int rootKey   = (int)apvts.getRawParameterValue ("key")->load() + 60;
            int scaleIdx  = (int)apvts.getRawParameterValue ("scale")->load();
            int stabilize = (int)apvts.getRawParameterValue ("stabilize")->load();

            if (hz > 0.0f)
            {
                currentT0 = getSampleRate() / hz;

                float rawMidiF = freqToMidiF (hz);
                int rawNote  = (int)std::round (rawMidiF);
                int snapNote = quantizeToScale (rawNote, rootKey, scaleIdx);
                detectedNote.store (rawNote);

                updateStabilizer (snapNote, stabilize, rawMidiF);
                targetNote.store (currentTargetNote);
            }
            else
            {
                detectedNote.store (-1);
                // Hold last target note during brief silence
            }
        }
    }

    // ── 2. Calculate shift amount ─────────────────────────────────────────
    float shiftSemitones = 0.0f;

    float hz = detectedHz.load();
    if (hz > 0.0f && currentTargetNote >= 0)
    {
        float detectedNoteF = freqToMidiF (hz);
        float shiftNeeded   = (float)currentTargetNote - detectedNoteF;

        // Octave fold — prevents large jumps when YIN returns an octave error
        shiftNeeded -= 12.0f * std::round (shiftNeeded / 12.0f);

        float strength = apvts.getRawParameterValue ("strength")->load();
        shiftSemitones = shiftNeeded * strength;

        // ── Transparency dead zone ───────────────────────────────────────
        // If the singer is within 45 cents of the target note, the voice
        // already sounds on-pitch to a listener. Leave it completely alone.
        // This is the key to sounding invisible: only fire when it's a real
        // miss (> 45 cents off), not just natural pitch expression.
        if (std::abs (shiftSemitones) < 0.45f)
            shiftSemitones = 0.0f;

        // Hard cap at ±3 semitones to prevent runaway corrections
        shiftSemitones = juce::jlimit (-3.0f, 3.0f, shiftSemitones);

        correcting.store (std::abs (shiftSemitones) > 0.01f);
    }
    else
    {
        correcting.store (false);
    }

    // ── 3. PSOLA pitch shift ──────────────────────────────────────────────
    // Process each channel independently. Both channels share the same
    // shiftSemitones and T0 — it's the same singer on both.
    //
    // PSOLA at shiftSemitones=0 is a perfect reconstruction (identity) filter,
    // so when we're in the dead zone the voice passes through unchanged except
    // for a tiny fixed delay (T0 samples, ~2–10ms, declared to host).

    if (numChannels >= 1)
    {
        const float* inL = buffer.getReadPointer (0);
        float* outL      = buffer.getWritePointer (0);
        psolaL.process (inL, outL, numSamples, shiftSemitones, currentT0);
    }

    if (numChannels >= 2)
    {
        const float* inR = buffer.getReadPointer (1);
        float* outR      = buffer.getWritePointer (1);
        psolaR.process (inR, outR, numSamples, shiftSemitones, currentT0);
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
