#pragma once
// PsolaShifter.h — Grain-based PSOLA pitch shifter for monophonic vocals.
//
// Replaces SoundTouch with an algorithm tuned specifically for:
//   - Monophonic vocal correction (< 3 semitones)
//   - Transparency: ratio=1.0 gives perfect reconstruction (no processing artefacts)
//   - Low latency: one pitch period (T0) samples, typically 2–10ms
//
// How it works (Pitch-Synchronous Overlap-and-Add):
//   Analysis: every T0 input samples, extract a Hann-windowed grain of length 2*T0.
//   Synthesis: place each grain into the output, but advance by T1 = T0/ratio instead.
//   When ratio=1.0  → T1=T0 → 50% Hann OLA → perfect reconstruction.
//   When ratio>1.0  → T1<T0 → grains land closer together → higher pitch.
//   When ratio<1.0  → T1>T0 → grains land further apart  → lower pitch.
//
// Why this sounds better than SoundTouch (WSOLA) for vocals:
//   WSOLA searches for the best-matching position in a large seek window, which
//   works well for music and large shifts but introduces "smearing" and latency
//   jitter for small corrections on pitched, monophonic material.
//   PSOLA anchors each grain to the actual pitch period, so the fundamental
//   frequency is always reconstructed correctly without smearing.

#include <vector>
#include <cmath>
#include <algorithm>

class PsolaShifter
{
public:
    // Ring buffer size — power of 2, ~370ms at 44100 Hz
    static constexpr int RING      = 16384;
    static constexpr int RING_MASK = RING - 1;

    void prepare (double sampleRate)
    {
        sr = (float)sampleRate;

        inputRing  .assign (RING, 0.0f);
        outputAccum.assign (RING, 0.0f);
        outputNorm .assign (RING, 0.0f);

        // Default T0: A3 (220 Hz) — updated on first real pitch detection
        const float defaultT0 = sr / 220.0f;

        // Seed inputHead forward by defaultT0 so the first grain fires at
        // sample 0 and the output is valid immediately (inputRing is filled
        // with zeros, which is fine — silence in = silence out).
        inputHead    = (int)(defaultT0 + 0.5f);
        outputHead   = 0;
        nextAnalysis = 0.0f;
        nextSynth    = defaultT0;   // first grain center at defaultT0 in output
        lastT0       = defaultT0;
    }

    // Process one audio block.
    //   input/output   — audio buffers (numSamples each)
    //   shiftSemitones — pitch shift amount (0.0 = bypass → perfect reconstruction)
    //   T0             — pitch period in samples (sampleRate / detectedHz), 0 if unvoiced
    //
    // Latency = T0 samples (typically 2–10ms — very low).
    void process (const float* input, float* output, int numSamples,
                  float shiftSemitones, float T0)
    {
        // If no pitch is detected, use the last known T0 so the grain engine
        // keeps running at a consistent grain size (avoids size-change clicks).
        const float t0 = (T0 > 8.0f && T0 < (float)(RING / 4))
                         ? T0 : lastT0;
        lastT0 = t0;

        const float ratio    = std::pow (2.0f, shiftSemitones / 12.0f);
        const float t1       = t0 / ratio;   // synthesis hop
        const int   grainLen = std::min ((int)(2.0f * t0 + 0.5f), RING / 4);
        const int   half     = grainLen / 2;

        for (int i = 0; i < numSamples; ++i)
        {
            // ── 1. Write one sample of input ──────────────────────────────
            inputRing[inputHead & RING_MASK] = input[i];
            ++inputHead;

            // ── 2. Place grains whenever we have enough input buffered ────
            // Condition: the grain is fully inside the recorded input, i.e.
            // its rightmost sample (center + half) has already been written.
            while (nextAnalysis + (float)half < (float)inputHead)
            {
                placeGrain (grainLen, t1);
                nextAnalysis += t0;
                nextSynth    += t1;
            }

            // ── 3. Read one output sample from the accumulator ────────────
            const int   rpos = outputHead & RING_MASK;
            const float norm = outputNorm[rpos];
            output[i] = (norm > 1e-4f) ? (outputAccum[rpos] / norm) : 0.0f;

            // Clear the slot so it can be reused by future grains
            outputAccum[rpos] = 0.0f;
            outputNorm [rpos] = 0.0f;
            ++outputHead;
        }
    }

    // Approximate latency in samples (varies with detected pitch).
    // Report this to the host so it can compensate for track alignment.
    int latencySamples() const { return (int)(lastT0 + 0.5f); }

    void reset()
    {
        std::fill (inputRing  .begin(), inputRing  .end(), 0.0f);
        std::fill (outputAccum.begin(), outputAccum.end(), 0.0f);
        std::fill (outputNorm .begin(), outputNorm .end(), 0.0f);

        const float defaultT0 = sr / 220.0f;
        inputHead    = (int)(defaultT0 + 0.5f);
        outputHead   = 0;
        nextAnalysis = 0.0f;
        nextSynth    = defaultT0;
        lastT0       = defaultT0;
    }

private:
    float sr     = 44100.0f;
    float lastT0 = 200.0f;

    std::vector<float> inputRing;    // circular buffer of recent input audio
    std::vector<float> outputAccum;  // overlap-add accumulator for output
    std::vector<float> outputNorm;   // sum of window weights (for normalization)

    int   inputHead    = 0;
    int   outputHead   = 0;
    float nextAnalysis = 0.0f;  // absolute input sample of next grain center
    float nextSynth    = 0.0f;  // absolute output sample of next grain center

    // Extract one Hann-windowed grain from the input ring at nextAnalysis,
    // and overlap-add it into outputAccum at nextSynth.
    void placeGrain (int grainLen, float t1)
    {
        const int half      = grainLen / 2;
        const int anaCenter = (int)(nextAnalysis + 0.5f);
        const int synCenter = (int)(nextSynth    + 0.5f);

        for (int j = 0; j < grainLen; ++j)
        {
            // Hann window — smoothly tapers the grain edges to avoid clicks
            const float w = 0.5f * (1.0f - std::cos (
                2.0f * 3.14159265f * (float)j / (float)(grainLen - 1)));

            const int srcIdx = (anaCenter - half + j) & RING_MASK;
            const int dstIdx = (synCenter - half + j) & RING_MASK;

            outputAccum[dstIdx] += inputRing[srcIdx] * w;
            outputNorm [dstIdx] += w;
        }
    }
};
