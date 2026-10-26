// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"
#include <juce_dsp/juce_dsp.h>

namespace pg
{
inline float lfoValue (float phase, int shape) noexcept // phase 0..1 -> -1..1
{
    switch (shape)
    {
        case 1:  return 4.0f * std::abs (phase - 0.5f) - 1.0f;                      // triangle
        case 2:  return std::tanh (6.0f * std::sin (2.0f * kPi * phase)) / std::tanh (6.0f); // soft square
        default: return std::sin (2.0f * kPi * phase);                               // sine
    }
}

//==============================================================================
class EqModule final : public Module
{
public:
    EqModule() : Module ({ ParamSpec::knob ("low", "Bass", -12, 12, 0, " dB"),
                           ParamSpec::knob ("lomid", "Lo-Mid", -12, 12, 0, " dB"),
                           ParamSpec::knob ("himid", "Hi-Mid", -12, 12, 0, " dB"),
                           ParamSpec::knob ("high", "Treble", -12, 12, 0, " dB"),
                           ParamSpec::knob ("hpf", "Low Cut", 20, 600, 20, " Hz", 100),
                           ParamSpec::knob ("lpf", "High Cut", 2000, 20000, 20000, " Hz", 8000),
                           ParamSpec::knob ("level", "Level", -18, 18, 0, " dB") }) {}

    juce::String typeId() const override      { return "eq"; }
    juce::String displayName() const override { return "Studio EQ"; }
    juce::String category() const override    { return "EQ"; }
    juce::uint32 colour() const override      { return 0xff4f5d75; }

protected:
    void prepareDsp (double, int) override { std::fill (std::begin (last), std::end (last), -999.0f); }
    void resetDsp() noexcept override { for (auto& ch : f) for (auto& b : ch) b.reset(); }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        bool changed = false;
        for (int i = 0; i < 6; ++i) if (differs (get (i), last[i])) { last[i] = get (i); changed = true; }
        if (changed)
            for (auto& ch : f)
            {
                ch[0].lowShelf (sr, 110.0, last[0], 0.8);
                ch[1].peak (sr, 400.0, 0.9, last[1]);
                ch[2].peak (sr, 1600.0, 0.9, last[2]);
                ch[3].highShelf (sr, 4500.0, last[3], 0.8);
                ch[4].highPass (sr, last[4], 0.7);
                ch[5].lowPass (sr, std::min ((double) last[5], sr * 0.45), 0.7);
            }
        const float g = dbToGain (get (6));
        forEachActiveChannel (L, R, n, stereo, [&] (int c, float* x, int len)
        {
            for (int i = 0; i < len; ++i)
            {
                float y = x[i];
                for (auto& b : f[c]) y = b.process (y);
                x[i] = y * g;
            }
        });
    }

private:
    Biquad f[2][6];
    float last[6] {};
};

//==============================================================================
// Stereo chorus: two modulated delay taps, LFOs 90° apart.
class Chorus final : public Module
{
public:
    Chorus() : Module ({ ParamSpec::knob ("rate", "Rate", 0.05f, 6, 0.8f, " Hz", 0.8f),
                         pedalDepth(),
                         ParamSpec::knob ("mix", "Mix", 0, 100, 50, " %") }) {}

    juce::String typeId() const override      { return "chorus"; }
    juce::String displayName() const override { return "Stereo Chorus"; }
    juce::String category() const override    { return "Modulation"; }
    juce::uint32 colour() const override      { return 0xff3fa7c9; }
    juce::String credit() const override      { return "Bucket-brigade chorus (Boss CE-2 style)"; }

    static ParamSpec pedalDepth() { return ParamSpec::knob ("depth", "Depth", 0, 10, 5); }

protected:
    void prepareDsp (double sampleRate, int) override
    {
        for (auto& d : dl) d.prepare ((int) (0.05 * sampleRate));
        for (auto& t : tone) t.setCutoff (sampleRate, 7000.0); // BBD-ish darkening of the wet path
    }
    void resetDsp() noexcept override { for (auto& d : dl) d.reset(); for (auto& t : tone) t.reset(); }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const float inc = get (0) / (float) sr;
        const float depth = get (1) / 10.0f * 0.004f * (float) sr;
        const float base  = 0.007f * (float) sr;
        const float mix   = get (2) * 0.01f;
        const bool stereoIn = stereo;

        for (int i = 0; i < n; ++i)
        {
            const float inL = L[i], inR = stereoIn ? R[i] : L[i];
            dl[0].push (inL); dl[1].push (inR);
            const float mL = lfoValue (phase, 1), mR = lfoValue (std::fmod (phase + 0.25f, 1.0f), 1);
            const float wL = tone[0].lp (dl[0].read (base + depth * (0.5f + 0.5f * mL)));
            const float wR = tone[1].lp (dl[1].read (base + depth * (0.5f + 0.5f * mR)));
            L[i] = inL * (1.0f - 0.4f * mix) + wL * mix * 0.8f;
            R[i] = inR * (1.0f - 0.4f * mix) + wR * mix * 0.8f;
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
        stereo = true;
    }

private:
    DelayLine dl[2];
    OnePole tone[2];
    float phase = 0;
};

//==============================================================================
// Phaser: chain of first-order all-pass filters swept by an LFO (MXR Phase 90 idea).
class Phaser final : public Module
{
public:
    Phaser() : Module ({ ParamSpec::knob ("rate", "Speed", 0.05f, 8, 0.5f, " Hz", 0.6f),
                         ParamSpec::knob ("depth", "Depth", 0, 10, 7),
                         ParamSpec::knob ("fb", "Feedback", 0, 90, 35, " %"),
                         ParamSpec::choice ("stages", "Stages", { "4", "6", "8" }, 0),
                         ParamSpec::knob ("mix", "Mix", 0, 100, 50, " %") }) {}

    juce::String typeId() const override      { return "phaser"; }
    juce::String displayName() const override { return "Phase Shifter"; }
    juce::String category() const override    { return "Modulation"; }
    juce::uint32 colour() const override      { return 0xffe07a1f; }
    juce::String credit() const override      { return "All-pass phaser (MXR Phase 90 style)"; }

protected:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override { for (auto& c : z) for (auto& s : c) s = 0; fbState[0] = fbState[1] = 0; }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const float inc = get (0) / (float) sr;
        const float depth = get (1) / 10.0f;
        const float fb = get (2) * 0.01f;
        const int stages = 4 + 2 * (int) get (3);
        const float mix = get (4) * 0.01f;
        const bool stereoIn = stereo;
        constexpr float fMin = 200.0f, fMax = 3200.0f;

        for (int i = 0; i < n; ++i)
        {
            float in[2] = { L[i], stereoIn ? R[i] : L[i] };
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                const float lfo = 0.5f + 0.5f * lfoValue (std::fmod (phase + 0.25f * (float) c, 1.0f), 0);
                const float fc = fMin * std::pow (fMax / fMin, lfo * depth);
                const float t = std::tan (kPi * std::min (fc, (float) sr * 0.45f) / (float) sr);
                const float a = (t - 1.0f) / (t + 1.0f);

                float y = in[c] + fb * fbState[c];
                for (int s = 0; s < stages; ++s)
                {
                    const float ap = a * y + z[c][s];
                    z[c][s] = y - a * ap;
                    y = ap;
                }
                fbState[c] = y;
                out[c] = in[c] * (1.0f - 0.5f * mix) + y * 0.5f * mix; // mix 100% = classic 50/50 notches
            }
            L[i] = out[0]; R[i] = out[1];
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
        stereo = true;
    }

private:
    float z[2][8] {};
    float fbState[2] {};
    float phase = 0;
};

//==============================================================================
class Tremolo final : public Module
{
public:
    Tremolo() : Module ({ ParamSpec::knob ("rate", "Speed", 0.5f, 15, 5, " Hz", 4),
                          ParamSpec::knob ("depth", "Intensity", 0, 100, 60, " %"),
                          ParamSpec::choice ("shape", "Shape", { "Sine", "Triangle", "Square" }, 0),
                          ParamSpec::toggle ("pan", "Stereo Pan", false) }) {}

    juce::String typeId() const override      { return "trem"; }
    juce::String displayName() const override { return "Tremolo"; }
    juce::String category() const override    { return "Modulation"; }
    juce::uint32 colour() const override      { return 0xff9c2f2f; }

protected:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override { phase = 0; }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const float inc = get (0) / (float) sr, depth = get (1) * 0.01f;
        const int shape = (int) get (2);
        const bool pan = get (3) > 0.5f;
        const bool stereoIn = stereo;

        for (int i = 0; i < n; ++i)
        {
            const float m = lfoValue (phase, shape);
            const float gA = 1.0f - depth * (0.5f + 0.5f * m);
            const float gB = 1.0f - depth * (0.5f - 0.5f * m);
            const float inR = stereoIn ? R[i] : L[i];
            if (pan) { R[i] = inR * gB; L[i] *= gA; }
            else     { L[i] *= gA; if (stereoIn) R[i] *= gA; }
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;
        }
        if (pan) stereo = true;
    }

private:
    float phase = 0;
};

//==============================================================================
// Analog-voiced delay: darkening feedback path, optional tape wow, ping-pong.
class Delay final : public Module
{
public:
    Delay() : Module ({ ParamSpec::knob ("time", "Time", 20, 1500, 380, " ms", 350),
                        ParamSpec::knob ("fb", "Repeats", 0, 95, 35, " %"),
                        ParamSpec::knob ("mix", "Mix", 0, 100, 30, " %"),
                        ParamSpec::knob ("tone", "Tone", 800, 12000, 3500, " Hz", 3500),
                        ParamSpec::knob ("mod", "Wow", 0, 10, 2),
                        ParamSpec::toggle ("pingpong", "Ping-Pong", false) }) {}

    juce::String typeId() const override      { return "delay"; }
    juce::String displayName() const override { return "Echo"; }
    juce::String category() const override    { return "Time"; }
    juce::uint32 colour() const override      { return 0xff5b8c5a; }

protected:
    void prepareDsp (double sampleRate, int) override
    {
        for (auto& d : dl) d.prepare ((int) (1.6 * sampleRate) + 1024);
        timeSm = (float) (get (0) * 0.001 * sampleRate);
        lastTone = -1;
    }
    void resetDsp() noexcept override
    {
        for (auto& d : dl) d.reset();
        for (auto& t : tone) t.reset();
        for (auto& h : hp) h.reset();
        fbL = fbR = 0;
    }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        if (differs (get (3), lastTone))
        {
            lastTone = get (3);
            for (auto& t : tone) t.setCutoff (sr, lastTone);
            for (auto& h : hp) h.setCutoff (sr, 90.0);
        }
        const float target = get (0) * 0.001f * (float) sr;
        const float glide  = 1.0f - std::exp (-1.0f / (0.08f * (float) sr));
        const float fb = get (1) * 0.01f, mix = get (2) * 0.01f;
        const float wow = get (4) / 10.0f * 0.0025f * (float) sr;
        const bool pingpong = get (5) > 0.5f;
        const float inc = 0.6f / (float) sr;
        const bool stereoIn = stereo;

        for (int i = 0; i < n; ++i)
        {
            timeSm += (target - timeSm) * glide;
            const float mod = wow * (0.5f + 0.5f * std::sin (2.0f * kPi * phase));
            phase += inc; if (phase >= 1.0f) phase -= 1.0f;

            const float inL = L[i], inR = stereoIn ? R[i] : L[i];
            const float d = timeSm + mod;
            const float wL = dl[0].read (d), wR = dl[1].read (d);

            if (pingpong)
            {
                dl[0].push (0.5f * (inL + inR) + std::tanh (fb * fbR));
                dl[1].push (std::tanh (fb * fbL));
            }
            else
            {
                dl[0].push (inL + std::tanh (fb * fbL));
                dl[1].push (inR + std::tanh (fb * fbR));
            }
            fbL = hp[0].hp (tone[0].lp (wL));
            fbR = hp[1].hp (tone[1].lp (wR));

            L[i] = inL + mix * fbL;
            R[i] = inR + mix * fbR;
        }
        stereo = true;
    }

private:
    DelayLine dl[2];
    OnePole tone[2], hp[2];
    float timeSm = 0, fbL = 0, fbR = 0, phase = 0, lastTone = -1;
};

//==============================================================================
// Reverb built on JUCE's Freeverb implementation (public-domain algorithm by Jezar).
class Reverb final : public Module
{
public:
    Reverb() : Module ({ ParamSpec::knob ("size", "Size", 0, 100, 55, " %"),
                         ParamSpec::knob ("damp", "Damping", 0, 100, 45, " %"),
                         ParamSpec::knob ("predelay", "Pre-delay", 0, 200, 20, " ms", 40),
                         ParamSpec::knob ("width", "Width", 0, 100, 100, " %"),
                         ParamSpec::knob ("mix", "Mix", 0, 100, 25, " %") }) {}

    juce::String typeId() const override      { return "reverb"; }
    juce::String displayName() const override { return "Hall Reverb"; }
    juce::String category() const override    { return "Time"; }
    juce::uint32 colour() const override      { return 0xff6a4fa3; }
    juce::String credit() const override      { return "Freeverb (Jezar at Dreampoint, public domain)"; }

protected:
    void prepareDsp (double sampleRate, int maxBlockSize) override
    {
        verb.setSampleRate (sampleRate);
        pre[0].prepare ((int) (0.21 * sampleRate)); pre[1].prepare ((int) (0.21 * sampleRate));
        wL.assign ((size_t) maxBlockSize, 0.0f); wR.assign ((size_t) maxBlockSize, 0.0f);
        for (auto& h : hp) h.setCutoff (sampleRate, 150.0);
    }
    void resetDsp() noexcept override { verb.reset(); pre[0].reset(); pre[1].reset(); hp[0].reset(); hp[1].reset(); }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        juce::Reverb::Parameters p;
        p.roomSize = 0.3f + 0.69f * get (0) * 0.01f;
        p.damping  = get (1) * 0.01f;
        p.width    = get (3) * 0.01f;
        p.wetLevel = 1.0f; p.dryLevel = 0.0f; p.freezeMode = 0.0f;
        verb.setParameters (p);

        const float d = std::max (2.0f, get (2) * 0.001f * (float) sr);
        const float mix = get (4) * 0.01f;
        const bool stereoIn = stereo;

        for (int i = 0; i < n; ++i)
        {
            pre[0].push (hp[0].hp (L[i])); pre[1].push (hp[1].hp (stereoIn ? R[i] : L[i]));
            wL[(size_t) i] = pre[0].read (d); wR[(size_t) i] = pre[1].read (d);
        }
        verb.processStereo (wL.data(), wR.data(), n);
        for (int i = 0; i < n; ++i)
        {
            const float inR = stereoIn ? R[i] : L[i];
            L[i] = L[i] + mix * 0.5f * wL[(size_t) i];
            R[i] = inR  + mix * 0.5f * wR[(size_t) i];
        }
        stereo = true;
    }

private:
    juce::Reverb verb;
    DelayLine pre[2];
    OnePole hp[2];
    std::vector<float> wL, wR;
};

} // namespace pg
