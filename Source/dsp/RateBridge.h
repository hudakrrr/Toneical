// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace pg
{
//==============================================================================
// Runs a block of DSP at a different sample rate (e.g. a 44.1 kHz neural pedal model
// inside a 48 kHz session) and comes back, for any host block size (down to 1 sample).
// Windowed-sinc interpolation both ways, constant latency (about 0.3 ms at 48/44.1 kHz).
class RateBridge
{
public:
    static constexpr int half = 6, taps = 2 * half, phases = 512;

    void prepare (double hostRate, double modelRate, int maxBlock)
    {
        host = hostRate; model = modelRate;
        stepDown = host / model;           // host samples per model sample
        stepUp   = model / host;           // model samples per host sample
        buildTable (down, std::min (1.0, model / host) * 0.94);
        buildTable (up,   std::min (1.0, host / model) * 0.94);
        a.assign ((size_t) (maxBlock + 4 * taps + 16), 0.0f);
        b.assign ((size_t) (maxBlock * 2 + 4 * taps + 64), 0.0f);
        m.assign ((size_t) (maxBlock * 2 + 64), 0.0f);
        reset();
    }

    void reset()
    {
        std::fill (a.begin(), a.end(), 0.0f);
        std::fill (b.begin(), b.end(), 0.0f);
        // pre-filled with zero history so interpolation can look back from the first sample
        // the up-sampler runs 'lagModel' model samples behind - that's the fixed latency
        lagModel = half + 2 + std::ceil ((half + 1) * stepUp);
        aLen = half + 1;                              aBase = -(int64_t) aLen;
        bLen = (size_t) lagModel + half + 2;          bBase = -(int64_t) bLen;
        aTotal = 0; bTotal = 0;
        tA = 0.0;
        tB = -lagModel;
    }

    int latencyHostSamples() const { return (int) std::ceil (lagModel * stepDown + half); }

    // fn (float* modelRateSamples, int count): processes in place at the model rate
    template <typename Fn>
    void process (const float* in, float* out, int n, Fn&& fn)
    {
        // ---- host -> model rate
        append (a, aLen, in, n);
        aTotal += n;
        int made = 0;
        while (tA + half < (double) aTotal - 0.0 && made < (int) m.size())
        {
            m[(size_t) made++] = interp (a, aBase, tA, down);
            tA += stepDown;
        }
        // ---- the model
        if (made > 0) fn (m.data(), made);
        append (b, bLen, m.data(), made);
        bTotal += made;

        // ---- model -> host rate (exactly n samples)
        for (int i = 0; i < n; ++i)
        {
            out[i] = (tB + half < (double) bTotal) ? interp (b, bBase, tB, up) : 0.0f;
            tB += stepUp;
        }
        compact (a, aLen, aBase, tA);
        compact (b, bLen, bBase, tB);
    }

private:
    struct Table { float h[phases + 1][taps]; };

    static void buildTable (Table& t, double cutoff)
    {
        auto bessel0 = [] (double x) { double s = 1, d = 1; for (int k = 1; k < 30; ++k) { d *= (x / (2 * k)) * (x / (2 * k)); s += d; } return s; };
        const double beta = 6.0, norm = bessel0 (beta);
        for (int p = 0; p <= phases; ++p)
        {
            const double frac = (double) p / phases;
            double sum = 0;
            for (int k = 0; k < taps; ++k)
            {
                const double x = (double) (k - half + 1) - frac;      // distance from the wanted point
                const double sx = cutoff * x;
                const double sinc = std::abs (sx) < 1e-9 ? 1.0 : std::sin (M_PI * sx) / (M_PI * sx);
                const double w = std::abs (x) >= half ? 0.0 : bessel0 (beta * std::sqrt (1.0 - (x / half) * (x / half))) / norm;
                t.h[p][k] = (float) (cutoff * sinc * w);
                sum += t.h[p][k];
            }
            for (int k = 0; k < taps; ++k) t.h[p][k] = (float) (t.h[p][k] / sum);
        }
    }

    // buf holds samples with absolute indices base .. base+len-1
    static float interp (const std::vector<float>& buf, int64_t base, double t, const Table& tb)
    {
        const double fl = std::floor (t);
        const int64_t i0 = (int64_t) fl;
        const int p = (int) ((t - fl) * phases + 0.5);
        const float* h = tb.h[p];
        const float* x = buf.data() + (i0 - half + 1 - base);
        float s = 0.0f;
        for (int k = 0; k < taps; ++k) s += x[k] * h[k];
        return s;
    }

    static void append (std::vector<float>& buf, size_t& len, const float* x, int n)
    {
        if (len + (size_t) n > buf.size()) buf.resize (len + (size_t) n + 64);   // only if the host sends a huge block
        std::copy (x, x + n, buf.begin() + (std::ptrdiff_t) len);
        len += (size_t) n;
    }

    // drop everything the interpolator will never look at again
    static void compact (std::vector<float>& buf, size_t& len, int64_t& base, double t)
    {
        const int64_t keepFrom = (int64_t) std::floor (t) - half - 1;
        const int64_t drop = keepFrom - base;
        if (drop <= 0) return;
        const size_t d = (size_t) std::min<int64_t> (drop, (int64_t) len);
        std::copy (buf.begin() + (std::ptrdiff_t) d, buf.begin() + (std::ptrdiff_t) len, buf.begin());
        len -= d;
        base += (int64_t) d;
    }

    double host = 48000, model = 44100, stepDown = 1, stepUp = 1, tA = 0, tB = 0, lagModel = 0;
    Table down {}, up {};
    std::vector<float> a, b, m;
    size_t aLen = 0, bLen = 0;
    int64_t aBase = 0, bBase = 0, aTotal = 0, bTotal = 0;
};

} // namespace pg
