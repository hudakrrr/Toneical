// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <functional>
// Small, allocation-free DSP building blocks used by the modules.
// Everything here is safe to call on the audio thread.

#include <cmath>
#include <vector>
#include <algorithm>

namespace pg
{
constexpr float kPi = 3.14159265358979323846f;

// Rational tanh approximation (the one Neural Amp Modeler ships): about 2x faster than
// std::tanh, error far below audibility - matters at 8x oversampling and tiny buffers.
inline float fastTanh (float x) noexcept
{
    const float ax = std::fabs (x), x2 = x * x;
    return (x * (2.45550750702956f + 2.45550750702956f * ax + (0.893229853513558f + 0.821226666969744f * ax) * x2))
         / (2.44506634652299f + (2.44506634652299f + x2) * std::fabs (x + 0.814642734961073f * x * ax));
}

// "did this value change?" - exact comparison on purpose (knob values are copied, not computed)
template <typename T> inline bool differs (T a, T b) noexcept { return std::not_equal_to<T>{} (a, b); }

inline float dbToGain (float db) noexcept { return std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g) noexcept  { return 20.0f * std::log10 (std::max (g, 1.0e-9f)); }

//==============================================================================
// RBJ "Audio EQ Cookbook" biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void setNormalised (double B0, double B1, double B2, double A0, double A1, double A2) noexcept
    {
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    static double clampF (double f, double sr) { return std::clamp (f, 5.0, sr * 0.49); }

    void lowPass (double sr, double f, double q = 0.7071)
    {
        f = clampF (f, sr);
        const double w = 2.0 * M_PI * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double sr, double f, double q = 0.7071)
    {
        f = clampF (f, sr);
        const double w = 2.0 * M_PI * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandPass (double sr, double f, double q)
    {
        f = clampF (f, sr);
        const double w = 2.0 * M_PI * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double sr, double f, double q, double gainDb)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        setNormalised (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (double sr, double f, double gainDb, double slope = 1.0)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2);
        const double sq = 2 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c),
                       A * ((A + 1) - (A - 1) * c - sq), (A + 1) + (A - 1) * c + sq,
                       -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
    }
    void highShelf (double sr, double f, double gainDb, double slope = 1.0)
    {
        f = clampF (f, sr);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = 2.0 * M_PI * f / sr, c = std::cos (w), s = std::sin (w);
        const double al = s / 2.0 * std::sqrt ((A + 1 / A) * (1 / slope - 1) + 2);
        const double sq = 2 * std::sqrt (A) * al;
        setNormalised (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c),
                       A * ((A + 1) + (A - 1) * c - sq), (A + 1) - (A - 1) * c + sq,
                       2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
    }
};

//==============================================================================
// One-pole filters (topology preserving transform – stable under modulation).
struct OnePole
{
    float g = 0, s = 0;
    void reset() noexcept { s = 0; }
    void setCutoff (double sr, double f) noexcept
    {
        f = std::clamp (f, 1.0, sr * 0.49);
        const double wc = std::tan (M_PI * f / sr);
        g = (float) (wc / (1.0 + wc));
    }
    inline float lp (float x) noexcept { const float v = (x - s) * g; const float y = v + s; s = y + v; return y; }
    inline float hp (float x) noexcept { return x - lp (x); }
};

//==============================================================================
// DC blocker (~10 Hz high-pass).
struct DcBlocker
{
    float r = 0.9995f, x1 = 0, y1 = 0;
    void prepare (double sr) noexcept { r = (float) (1.0 - (2.0 * M_PI * 10.0 / sr)); }
    void reset() noexcept { x1 = y1 = 0; }
    inline float process (float x) noexcept { const float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

//==============================================================================
// Fractional delay line (cubic interpolation). Allocate in prepare() only.
struct DelayLine
{
    std::vector<float> buf;
    int mask = 0, w = 0;

    void prepare (int maxSamples)
    {
        int size = 1;
        while (size < maxSamples + 4) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1; w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

    // Call after push(): d == 1 returns the sample just pushed. d is clamped to >= 2
    // so the interpolator never reads the (oldest) sample on the other side of the write head.
    inline float read (float d) const noexcept
    {
        d = std::clamp (d, 2.0f, (float) (mask - 3));
        const int   di = (int) d;
        const float f  = d - (float) di;
        const int   i0 = (w - di) & mask;
        const float xm1 = buf[(size_t) ((i0 + 1) & mask)];
        const float x0  = buf[(size_t) i0];
        const float x1  = buf[(size_t) ((i0 - 1) & mask)];
        const float x2  = buf[(size_t) ((i0 - 2) & mask)];
        // 4-point, 3rd-order Hermite
        const float c0 = x0, c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
};

//==============================================================================
// Simple linear parameter smoother (no allocation, no juce dependency).
struct Smoother
{
    float current = 0, target = 0, step = 0; int left = 0, len = 64;
    void prepare (double sr, double seconds) { len = std::max (1, (int) (sr * seconds)); }
    void snap (float v) noexcept { current = target = v; left = 0; }
    void set (float v) noexcept
    {
        if (! differs (v, target)) return;
        target = v; left = len; step = (target - current) / (float) len;
    }
    inline float next() noexcept
    {
        if (left > 0) { current += step; if (--left == 0) current = target; }
        return current;
    }
    bool isSmoothing() const noexcept { return left > 0; }
};

//==============================================================================
// Waveshapers
inline float softClip (float x) noexcept { return std::tanh (x); }

inline float hardishClip (float x) noexcept
{
    // x / (1 + |x|^2.5)^(1/2.5) – between tanh and a hard clip (diodes to ground)
    const float a = std::abs (x);
    return x / std::pow (1.0f + std::pow (a, 2.5f), 0.4f);
}

// Asymmetric "triode" curve – even harmonics, slightly compressive.
inline float triode (float x, float bias) noexcept
{
    return std::tanh (x + bias) - std::tanh (bias);
}

//==============================================================================
// Third-order analog filter discretised with the bilinear transform.
// H(s) = (b0 + b1 s + b2 s^2 + b3 s^3) / (a0 + a1 s + a2 s^2 + a3 s^3)
struct Iir3
{
    // Kept in double precision: at 4x oversampled rates the poles sit very close to
    // z = 1 and a float direct-form third-order section can go unstable.
    double b[4] {}, a[4] {}; // a[0] == 1
    double x1 = 0, x2 = 0, x3 = 0, y1 = 0, y2 = 0, y3 = 0;

    void reset() noexcept { x1 = x2 = x3 = y1 = y2 = y3 = 0; }

    void setAnalog (const double (&bs)[4], const double (&as)[4], double sr)
    {
        const double c = 2.0 * sr;
        // (1 - z^-1)^k (1 + z^-1)^(3-k) expanded coefficients for k = 0..3
        static const double P[4][4] = { { 1, 3, 3, 1 },    // (1+z)^3
                                        { 1, 1, -1, -1 },  // (1-z)(1+z)^2
                                        { 1, -1, -1, 1 },  // (1-z)^2(1+z)
                                        { 1, -3, 3, -1 } };// (1-z)^3
        double B[4] {}, A[4] {};
        double ck = 1.0;
        for (int k = 0; k < 4; ++k)
        {
            for (int i = 0; i < 4; ++i)
            {
                B[i] += bs[k] * ck * P[k][i];
                A[i] += as[k] * ck * P[k][i];
            }
            ck *= c;
        }
        for (int i = 0; i < 4; ++i) { b[i] = B[i] / A[0]; a[i] = A[i] / A[0]; }
    }

    inline float process (float xf) noexcept
    {
        const double x = xf;
        double y = b[0] * x + b[1] * x1 + b[2] * x2 + b[3] * x3
                 - a[1] * y1 - a[2] * y2 - a[3] * y3;
        if (std::abs (y) < 1.0e-30) y = 0.0; // flush denormals
        x3 = x2; x2 = x1; x1 = x;
        y3 = y2; y2 = y1; y1 = y;
        return (float) y;
    }
};

//==============================================================================
// Passive guitar-amp tone stack (bass / mid / treble), after D. T. Yeh &
// J. O. Smith, "Discretization of the '59 Fender Bassman Tone Stack" (DAFx 2006).
struct ToneStack
{
    struct Values { double R1, R2, R3, R4, C1, C2, C3; };
    static constexpr Values fender   { 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9 };
    static constexpr Values marshall { 220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9 };
    static constexpr Values vox      { 1e6,   1e6, 10e3, 100e3, 50e-12, 22e-9, 22e-9 };

    Iir3 filt;

    void reset() noexcept { filt.reset(); }

    // bass, mid, treble in 0..1
    void set (const Values& v, double bass, double mid, double treble, double sr)
    {
        const double R1 = v.R1, R2 = v.R2, R3 = v.R3, R4 = v.R4, C1 = v.C1, C2 = v.C2, C3 = v.C3;
        const double t = std::clamp (treble, 0.0, 1.0);
        const double m = std::clamp (mid, 0.0, 1.0);
        const double l = std::exp ((std::clamp (bass, 0.0, 1.0) - 1.0) * 3.4); // log-taper pot

        const double b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
        const double b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
                        - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                        + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                        + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
        const double b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                        - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                        + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                        + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
                        + t * l * C1 * C2 * C3 * R1 * R2 * R4;
        const double a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
        const double a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                        - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                        + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                        + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3
                           + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
        const double a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                        - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                        + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
                        + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;

        const double bs[4] = { 0.0, b1, b2, b3 };
        const double as[4] = { 1.0, a1, a2, a3 };
        filt.setAnalog (bs, as, sr);
    }

    inline float process (float x) noexcept { return filt.process (x); }
};

} // namespace pg
