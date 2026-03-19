#pragma once
// PitchDetector.h — YIN Pitch Detection Algorithm (header-only)
//
// Given a buffer of audio samples, this class finds the fundamental frequency
// (the "note" being sung) in Hz. Returns -1 if no pitch is detected.
//
// Algorithm: "YIN, a fundamental frequency estimator for speech and music"
//            de Cheveigné & Kawahara, 2002.
//
// Why YIN? It's accurate enough for vocals, fast enough for realtime use,
// and straightforward to implement without external libraries.

#include <vector>
#include <cmath>
#include <algorithm>

class PitchDetector
{
public:
    // bufferSize: number of samples to analyze per call (2048 is a good default)
    // sampleRate: e.g. 44100
    // threshold:  confidence cutoff 0.0–1.0 (lower = stricter; 0.10–0.15 works well)
    PitchDetector (int bufferSize = 2048, float sampleRate = 44100.0f, float threshold = 0.12f)
        : bufferSize (bufferSize)
        , sampleRate (sampleRate)
        , threshold  (threshold)
        , d    (bufferSize / 2, 0.0f)
        , dNorm(bufferSize / 2, 0.0f)
    {}

    void setSampleRate (float sr) { sampleRate = sr; }

    // Returns fundamental frequency in Hz, or -1.0f if none found.
    float detectPitch (const float* buffer, int numSamples)
    {
        int halfLen = std::min (bufferSize / 2, numSamples / 2);
        if (halfLen < 10) return -1.0f;

        // ── Step 1: Difference function ───────────────────────────────────
        // d[tau] = how different the signal is from itself when shifted by tau samples.
        // When tau equals the pitch period, the shifted copy matches and d[tau] is low.
        d[0] = 0.0f;
        for (int tau = 1; tau < halfLen; ++tau)
        {
            d[tau] = 0.0f;
            for (int j = 0; j < halfLen; ++j)
            {
                float diff = buffer[j] - buffer[j + tau];
                d[tau] += diff * diff;
            }
        }

        // ── Step 2: Cumulative mean normalized difference ─────────────────
        // Normalizes d[] so the threshold is consistent across signal levels.
        dNorm[0] = 1.0f;
        float runningSum = 0.0f;
        for (int tau = 1; tau < halfLen; ++tau)
        {
            runningSum += d[tau];
            dNorm[tau] = (runningSum > 0.0f) ? d[tau] * (float)tau / runningSum : 1.0f;
        }

        // ── Step 3: Find the first dip below the threshold ────────────────
        // The first low dip corresponds to the fundamental period (not a harmonic).
        int tau = -1;
        for (int t = 2; t < halfLen - 1; ++t)
        {
            if (dNorm[t] < threshold)
            {
                // Walk to the local minimum of the dip
                while (t + 1 < halfLen - 1 && dNorm[t + 1] < dNorm[t])
                    ++t;
                tau = t;
                break;
            }
        }

        if (tau <= 0) return -1.0f;  // No clear pitch found

        // ── Step 4: Parabolic interpolation ──────────────────────────────
        // Refines the integer tau to a sub-sample estimate for more accurate pitch.
        float betterTau = (float)tau;
        if (tau > 0 && tau < halfLen - 1)
        {
            float s0 = dNorm[tau - 1];
            float s1 = dNorm[tau];
            float s2 = dNorm[tau + 1];
            float denom = 2.0f * (2.0f * s1 - s2 - s0);
            if (std::abs (denom) > 1e-6f)
                betterTau += (s2 - s0) / denom;
        }

        if (betterTau <= 0.0f) return -1.0f;

        float freq = sampleRate / betterTau;

        // Sanity-check: vocals sit roughly between 70 Hz (bass) and 1200 Hz (soprano)
        // Anything outside this range is noise, not a note.
        if (freq < 65.0f || freq > 1300.0f)
            return -1.0f;

        return freq;
    }

private:
    int   bufferSize;
    float sampleRate;
    float threshold;
    std::vector<float> d;
    std::vector<float> dNorm;
};
