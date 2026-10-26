// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

namespace pg
{
//==============================================================================
// Chromatic tuner. The audio thread pushes (decimated) input into a lock-free
// FIFO; the GUI pulls it and runs the YIN pitch detector (de Cheveigné & Kawahara 2002).
class Tuner
{
public:
    static constexpr int decimation = 2;
    static constexpr int window = 2048;          // at sr/2: ~85 ms at 48 kHz

    struct Reading { bool valid = false; float hz = 0, cents = 0; juce::String note; int octave = 0; };

    void prepare (double sampleRate)
    {
        rate = sampleRate / decimation;
        fifo.reset(); acc = 0; accCount = 0;
        history.assign (window, 0.0f);
    }

    // audio thread
    void push (const float* x, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            acc += x[i];
            if (++accCount == decimation)
            {
                const float s = acc / decimation;
                acc = 0; accCount = 0;
                int s1, z1, s2, z2;
                fifo.prepareToWrite (1, s1, z1, s2, z2);
                if (z1 > 0) ring[(size_t) s1] = s;
                fifo.finishedWrite (z1);
            }
        }
    }

    // GUI thread
    Reading analyse()
    {
        // drain the FIFO into the sliding history window
        int s1, z1, s2, z2;
        const int ready = fifo.getNumReady();
        fifo.prepareToRead (ready, s1, z1, s2, z2);
        auto append = [this] (const float* src, int n)
        {
            if (n <= 0) return;
            if (n >= window) { std::copy (src + n - window, src + n, history.begin()); return; }
            std::move (history.begin() + n, history.end(), history.begin());
            std::copy (src, src + n, history.end() - n);
        };
        append (ring.data() + s1, z1);
        append (ring.data() + s2, z2);
        fifo.finishedRead (z1 + z2);

        Reading r;
        float rms = 0;
        for (float v : history) rms += v * v;
        rms = std::sqrt (rms / window);
        if (rms < 0.003f) return r; // too quiet

        const int minTau = (int) (rate / 1100.0), maxTau = std::min (window / 2, (int) (rate / 60.0));
        diff.assign ((size_t) maxTau + 2, 0.0f);
        for (int tau = 1; tau <= maxTau + 1; ++tau)
        {
            float sum = 0;
            for (int j = 0; j < window / 2; ++j)
            {
                const float d = history[(size_t) j] - history[(size_t) (j + tau)];
                sum += d * d;
            }
            diff[(size_t) tau] = sum;
        }
        // cumulative mean normalised difference
        float running = 0;
        diff[0] = 1;
        for (int tau = 1; tau <= maxTau + 1; ++tau)
        {
            running += diff[(size_t) tau];
            diff[(size_t) tau] = running > 0 ? diff[(size_t) tau] * (float) tau / running : 1.0f;
        }
        int best = -1;
        for (int tau = std::max (2, minTau); tau <= maxTau; ++tau)
        {
            if (diff[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= maxTau && diff[(size_t) tau + 1] < diff[(size_t) tau]) ++tau;
                best = tau; break;
            }
        }
        if (best < 0) return r;

        // parabolic interpolation around the minimum
        const float a = diff[(size_t) best - 1], b = diff[(size_t) best], c = diff[(size_t) best + 1];
        const float denom = a - 2 * b + c;
        const float tau = (float) best + (std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f);

        r.hz = (float) (rate / tau);
        const float midi = 69.0f + 12.0f * std::log2 (r.hz / 440.0f);
        const int nearest = (int) std::lround (midi);
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        r.note = names[((nearest % 12) + 12) % 12];
        r.octave = nearest / 12 - 1;
        r.cents = (midi - (float) nearest) * 100.0f;
        r.valid = true;
        return r;
    }

private:
    double rate = 24000.0;
    std::array<float, 8192> ring {};
    juce::AbstractFifo fifo { 8192 };
    float acc = 0; int accCount = 0;
    std::vector<float> history, diff;
};

} // namespace pg
