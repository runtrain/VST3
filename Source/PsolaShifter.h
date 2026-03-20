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

        // bypassDelay = the fixed number of samples the PSOLA output lags behind
        // the input (inputHead starts bypassDelay ahead of outputHead).
        // We reuse this exact value for the bypass path so both paths have the
        // same latency and the crossfade between them is click-free.
        bypassDelay = (int)(defaultT0 + 0.5f);

        inputHead    = bypassDelay;
        outputHead   = 0;
        nextAnalysis = 0.0f;
        nextSynth    = defaultT0;   // first grain center at defaultT0 in output
        lastT0       = defaultT0;
        crossfadePos = 0.0f;        // start in bypass mode
    }

    // Process one audio block.
    //   input/output   — audio buffers (numSamples each)
    //   shiftSemitones — pitch shift amount (0.0 = bypass, non-zero = PSOLA active)
    //   T0             — pitch period in samples (sampleRate / detectedHz), 0 if unvoiced
    //
    // Latency = bypassDelay samples (fixed, equal to T0 at A3 ≈ 200 samples / ~4ms).
    //
    // When shiftSemitones == 0 the output is a plain delayed copy of the input —
    // no grain processing artifacts. When non-zero, PSOLA kicks in and is
    // crossfaded in over ~11ms to avoid clicks at the transition.
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

        // Target crossfade position: 0 = bypass, 1 = PSOLA
        const float crossfadeTarget = (shiftSemitones == 0.0f) ? 0.0f : 1.0f;
        // Step size: transition over 512 samples (~11ms at 44100 Hz)
        const float crossfadeStep   = 1.0f / 512.0f;

        for (int i = 0; i < numSamples; ++i)
        {
            // ── 1. Write one sample of input ──────────────────────────────
            inputRing[inputHead & RING_MASK] = input[i];
            ++inputHead;

            // ── 2. Always advance the PSOLA grain engine ──────────────────
            // We keep the engine running even during bypass so its state is
            // current when correction kicks back in, avoiding a jump artifact.
            while (nextAnalysis + (float)half < (float)inputHead)
            {
                placeGrain (grainLen, t1);
                nextAnalysis += t0;
                nextSynth    += t1;
            }

            // ── 2b. Catch-up: re-sync if synthesis pointer has fallen behind ─
            // When shifting UP (ratio > 1), t1 < t0, so nextSynth drifts
            // behind outputHead at ~(t0-t1)/t0 per grain.  After ~t0/(t0-t1)
            // grains the grain center is past outputHead and the accumulated
            // samples are zero — audible as silence/noise.
            //
            // Fix: if the right edge of the last-placed grain (nextSynth + half)
            // is already behind outputHead, skip forward by one synthesis hop
            // without placing a grain.  Neighboring grains (spaced t1 apart,
            // length 2*t0 > 2*t1) always overlap enough to fill the gap, so
            // the OLA normaliser keeps the output level correct.
            while (nextSynth + (float)half < (float)outputHead)
            {
                nextSynth    += t1;
                nextAnalysis += t0;
            }

            // ── 3. Read PSOLA output sample and clear the slot ────────────
            const int   rpos    = outputHead & RING_MASK;
            const float norm    = outputNorm[rpos];
            const float psolaOut = (norm > 1e-4f) ? (outputAccum[rpos] / norm) : 0.0f;
            outputAccum[rpos] = 0.0f;
            outputNorm [rpos] = 0.0f;
            ++outputHead;

            // ── 4. Read bypass output (fixed delay, artifact-free) ────────
            // inputHead - 1 is the sample just written; go back bypassDelay
            // more to get the delayed version with the same latency as PSOLA.
            const float bypassOut = inputRing[(inputHead - 1 - bypassDelay) & RING_MASK];

            // ── 5. Smooth crossfade toward target ─────────────────────────
            if (crossfadePos < crossfadeTarget)
                crossfadePos = std::min (crossfadePos + crossfadeStep, crossfadeTarget);
            else if (crossfadePos > crossfadeTarget)
                crossfadePos = std::max (crossfadePos - crossfadeStep, crossfadeTarget);

            output[i] = bypassOut * (1.0f - crossfadePos) + psolaOut * crossfadePos;
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
        bypassDelay  = (int)(defaultT0 + 0.5f);
        inputHead    = bypassDelay;
        outputHead   = 0;
        nextAnalysis = 0.0f;
        nextSynth    = defaultT0;
        lastT0       = defaultT0;
        crossfadePos = 0.0f;
    }

private:
    float sr          = 44100.0f;
    float lastT0      = 200.0f;
    int   bypassDelay = 200;    // samples; set in prepare() to match PSOLA latency
    float crossfadePos = 0.0f;  // 0 = bypass, 1 = PSOLA

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
