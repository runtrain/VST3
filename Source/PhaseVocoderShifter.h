#pragma once
// PhaseVocoderShifter.h — Frequency-domain pitch shifter for monophonic vocals.
//
// Replaces the OLA/PSOLA approach with a proper Phase Vocoder, which eliminates
// the "phasiness" (underwater/flanging) artifact that OLA produces during pitch
// correction.
//
// Why Phase Vocoder is better:
//   OLA shifts pitch by placing time-domain grains at different intervals.
//   Adjacent grains come from different input positions, so their phases rarely
//   align — that's the "gargling" you hear.  The Phase Vocoder works in the
//   frequency domain and explicitly tracks and corrects the phase of every
//   partial, so sustained vowels stay clean.
//
// Algorithm:
//   Every HOP samples, apply a Hann window to the most recent FFT_SIZE input
//   samples and take an FFT.  For each bin k, measure the instantaneous
//   frequency from the phase difference between consecutive frames.  To shift
//   pitch by `ratio`, map output bin k from source bin k/ratio (interpolated),
//   scaling the instantaneous frequency by ratio.  Accumulate the synthesis
//   phase, build the complex output spectrum, IFFT, window, and overlap-add
//   into an output ring buffer.
//
// Latency = FFT_SIZE = 2048 samples (~46 ms at 44100 Hz).
// Reported via latencySamples() so the DAW compensates automatically.
//
// Same public interface as PsolaShifter — drop-in replacement.

#include <complex>
#include <cmath>
#include <algorithm>
#include <cstring>

class PhaseVocoderShifter
{
public:
    // ── Constants ────────────────────────────────────────────────────────────
    static constexpr int FFT_SIZE = 2048;                // must be power of 2
    static constexpr int HOP      = FFT_SIZE / 4;        // 75 % overlap
    static constexpr int BINS     = FFT_SIZE / 2 + 1;
    // Effective system latency (measured via delta-impulse):
    //   outWr starts at FFT_SIZE-HOP, so the impulse at input[0] peaks in the
    //   synthesis at outWr_1 + FFT_SIZE/2 = (FFT_SIZE-HOP+HOP) + FFT_SIZE/2
    //                                      = FFT_SIZE + FFT_SIZE/2 = 3*FFT_SIZE/2... actually:
    //   Frame k=1 (center of analysis = 0) → outWr=FFT_SIZE, syn center=FFT_SIZE+FFT_SIZE/2-HOP
    //   Simplified: latency = FFT_SIZE + 2*HOP  (verified empirically = 3072 @ 44100)
    static constexpr int ACTUAL_LATENCY = FFT_SIZE + 2 * HOP;   // 3072 samples

    // ── Public interface (same as PsolaShifter) ───────────────────────────────

    void prepare (double /*sampleRate*/)
    {
        buildWindow();
        reset();
    }

    void reset()
    {
        std::fill (inRing,    inRing    + BUF,      0.0f);
        std::fill (outAccum,  outAccum  + BUF,      0.0f);
        std::fill (prevPhase, prevPhase + BINS,     0.0f);
        std::fill (synthPhs,  synthPhs  + BINS,     0.0f);
        std::fill (bypassBuf, bypassBuf + ACTUAL_LATENCY, 0.0f);
        inWr         = 0;
        outRd        = 0;
        outWr        = FFT_SIZE - HOP;  // frames must write ahead of outRd; see ACTUAL_LATENCY
        bypassWr     = 0;
        hopCountdown = HOP;
        crossPos     = 0.0f;
        prevShift    = 0.0f;
        seedPhases   = false;
    }

    // Latency reported to host — DAW compensates automatically.
    int latencySamples() const { return ACTUAL_LATENCY; }

    // shiftSemitones = 0 → perfect-reconstruction bypass (crossfaded).
    // T0             = pitch period in samples (unused; kept for API compat).
    void process (const float* input, float* output, int numSamples,
                  float shiftSemitones, float /*T0*/)
    {
        const float ratio      = std::pow (2.0f, shiftSemitones / 12.0f);
        const float crossTarget = (shiftSemitones == 0.0f) ? 0.0f : 1.0f;
        constexpr float kStep  = 1.0f / 2048.0f;  // ~46 ms crossfade — must match PV settling time (4 grains × HOP)

        // Seed synthesis phases from the analysis on the bypass→correction
        // transition so the first corrected frame is phase-aligned with the
        // input, preventing a click at the moment correction starts.
        if (shiftSemitones != 0.0f && prevShift == 0.0f)
            seedPhases = true;
        prevShift = shiftSemitones;

        for (int i = 0; i < numSamples; ++i)
        {
            // ── Write input to ring buffer ─────────────────────────────────
            inRing[inWr & MASK] = input[i];
            ++inWr;

            // ── Bypass: circular delay of ACTUAL_LATENCY samples ──────────
            // Read the oldest slot first (= ACTUAL_LATENCY samples ago), then
            // overwrite it with the new sample.
            const float bypassOut = bypassBuf[bypassWr];
            bypassBuf[bypassWr]   = input[i];
            bypassWr = (bypassWr + 1) % ACTUAL_LATENCY;

            // ── Phase vocoder: process one frame every HOP samples ─────────
            if (--hopCountdown == 0)
            {
                processFrame (ratio);
                hopCountdown = HOP;
            }

            // ── Read phase-vocoder output ──────────────────────────────────
            const float pvOut = outAccum[outRd & MASK];
            outAccum[outRd & MASK] = 0.0f;
            ++outRd;

            // ── Crossfade between bypass and PV ───────────────────────────
            if      (crossPos < crossTarget) crossPos = std::min (crossPos + kStep, crossTarget);
            else if (crossPos > crossTarget) crossPos = std::max (crossPos - kStep, crossTarget);

            output[i] = bypassOut * (1.0f - crossPos) + pvOut * crossPos;
        }
    }

private:
    // ── Sizes ─────────────────────────────────────────────────────────────────
    static constexpr int BUF  = FFT_SIZE * 2;   // ring buffer size (power of 2)
    static constexpr int MASK = BUF - 1;

    // ── Buffers ───────────────────────────────────────────────────────────────
    float inRing  [BUF]             = {};   // input ring buffer
    float outAccum[BUF]             = {};   // OLA output accumulator
    float bypassBuf[ACTUAL_LATENCY] = {};   // fixed-delay bypass line
    float win     [FFT_SIZE]        = {};   // Hann window

    // ── Phase vocoder state ───────────────────────────────────────────────────
    float prevPhase[BINS]    = {};   // analysis phase from previous frame
    float synthPhs [BINS]    = {};   // accumulated synthesis phase

    // ── Frame work buffers (members avoid large stack allocs per frame) ───────
    using Cx = std::complex<float>;
    Cx    frameCx [FFT_SIZE] = {};
    float mag     [BINS]     = {};
    float iFreq   [BINS]     = {};
    float magOut  [BINS]     = {};
    float freqOut [BINS]     = {};

    // ── Counters ──────────────────────────────────────────────────────────────
    int   inWr         = 0;
    int   outRd        = 0;
    int   outWr        = 0;
    int   bypassWr     = 0;
    int   hopCountdown = HOP;

    // ── Crossfade / transition state ──────────────────────────────────────────
    float crossPos  = 0.0f;
    float prevShift = 0.0f;
    bool  seedPhases = false;

    // ── Build Hann analysis/synthesis window ──────────────────────────────────
    void buildWindow()
    {
        constexpr float k2Pi = 6.28318530717959f;
        for (int i = 0; i < FFT_SIZE; ++i)
            win[i] = 0.5f * (1.0f - std::cos (k2Pi * i / (FFT_SIZE - 1)));
    }

    // ── In-place radix-2 Cooley-Tukey FFT ────────────────────────────────────
    // Negative exponent convention: X[k] = Σ x[n] e^{-j2πkn/N}
    static void fft (Cx* x, int N)
    {
        // Bit-reversal permutation
        for (int i = 1, j = 0; i < N; ++i)
        {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (x[i], x[j]);
        }
        // Butterfly stages
        for (int len = 2; len <= N; len <<= 1)
        {
            const float ang = -6.28318530717959f / (float)len;
            const Cx wlen (std::cos (ang), std::sin (ang));
            for (int i = 0; i < N; i += len)
            {
                Cx w (1.0f, 0.0f);
                for (int j = 0; j < len / 2; ++j)
                {
                    Cx u = x[i + j];
                    Cx v = x[i + j + len / 2] * w;
                    x[i + j]           = u + v;
                    x[i + j + len / 2] = u - v;
                    w *= wlen;
                }
            }
        }
    }

    static void ifft (Cx* x, int N)
    {
        // IFFT via conjugate symmetry: IFFT(x) = conj(FFT(conj(x))) / N
        for (int i = 0; i < N; ++i) x[i] = std::conj (x[i]);
        fft (x, N);
        const float inv = 1.0f / (float)N;
        for (int i = 0; i < N; ++i) x[i] = std::conj (x[i]) * inv;
    }

    // ── Process one PV frame ──────────────────────────────────────────────────
    void processFrame (float ratio)
    {
        // ── 1. Copy FFT_SIZE samples ending at the current write position ───
        for (int k = 0; k < FFT_SIZE; ++k)
            frameCx[k] = inRing[(inWr - FFT_SIZE + k) & MASK] * win[k];

        // ── 2. Forward FFT ───────────────────────────────────────────────────
        fft (frameCx, FFT_SIZE);

        // ── 3. Analysis: magnitude + instantaneous frequency ─────────────────
        //
        // Expected phase advance per hop for bin k:  phAdv = 2π k H / N
        // Measured advance:                          delta  = phi_k - prevPhase_k
        // Deviation from expected:                   dPhi   = delta - phAdv (wrapped)
        // True instantaneous freq (rad/sample):      iFreq  = (phAdv + dPhi) / H
        const float phAdv = 6.28318530717959f * (float)HOP / (float)FFT_SIZE;

        for (int k = 0; k < BINS; ++k)
        {
            mag[k]  = std::abs (frameCx[k]);
            float phi = std::arg (frameCx[k]);

            float dPhi = phi - prevPhase[k] - (float)k * phAdv;
            // Wrap to [-π, π]
            dPhi -= 6.28318530717959f * std::round (dPhi * (1.0f / 6.28318530717959f));

            iFreq[k]      = ((float)k * phAdv + dPhi) / (float)HOP;
            prevPhase[k]  = phi;
        }

        // ── 4. Seed synthesis phases on bypass→correction transition ─────────
        // Ensures the first corrected frame is phase-aligned with the input,
        // avoiding a click when correction first kicks in.
        if (seedPhases)
        {
            std::copy (prevPhase, prevPhase + BINS, synthPhs);
            seedPhases = false;
        }

        // ── 5. Pitch-shift by resampling bins ────────────────────────────────
        //
        // For output bin k, the source bin is k/ratio (fractional).
        // Linearly interpolate magnitude and instantaneous frequency.
        // Scale instantaneous frequency by ratio so the output pitch is ratio
        // times the input pitch.
        std::fill (magOut,  magOut  + BINS, 0.0f);
        std::fill (freqOut, freqOut + BINS, 0.0f);

        for (int k = 0; k < BINS; ++k)
        {
            const float kSrc = (float)k / ratio;
            const int   k0   = (int)kSrc;
            const float frac = kSrc - (float)k0;
            if (k0 < 0 || k0 + 1 >= BINS) continue;

            magOut [k] =  mag[k0]   * (1.0f - frac) +  mag[k0 + 1] * frac;
            freqOut[k] = (iFreq[k0] * (1.0f - frac) + iFreq[k0 + 1] * frac) * ratio;
        }

        // ── 6. Advance synthesis phases, build complex output spectrum ───────
        // DC and Nyquist bins are real-only.
        synthPhs[0] += freqOut[0] * (float)HOP;
        frameCx[0]   = Cx (magOut[0] * std::cos (synthPhs[0]), 0.0f);

        for (int k = 1; k < FFT_SIZE / 2; ++k)
        {
            synthPhs[k] += freqOut[k] * (float)HOP;
            frameCx[k]            = Cx (magOut[k] * std::cos (synthPhs[k]),
                                        magOut[k] * std::sin (synthPhs[k]));
            frameCx[FFT_SIZE - k] = std::conj (frameCx[k]); // conjugate symmetry
        }

        const int nyq = FFT_SIZE / 2;
        synthPhs[nyq] += freqOut[nyq] * (float)HOP;
        frameCx[nyq]   = Cx (magOut[nyq] * std::cos (synthPhs[nyq]), 0.0f);

        // ── 7. Inverse FFT ───────────────────────────────────────────────────
        ifft (frameCx, FFT_SIZE);

        // ── 8. Windowed overlap-add ───────────────────────────────────────────
        // Apply synthesis Hann window and accumulate.
        // Normalisation: for Hann² at 4× overlap, Σ w[n-kH]² = 1.5 exactly,
        // so multiply by 2/3 so the summed output has unity gain.
        constexpr float kNorm = 2.0f / 3.0f;
        for (int k = 0; k < FFT_SIZE; ++k)
            outAccum[(outWr + k) & MASK] += frameCx[k].real() * win[k] * kNorm;

        outWr = (outWr + HOP) & MASK;
    }
};
