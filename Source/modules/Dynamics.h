// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"

namespace pg
{
//==============================================================================
// Noise gate, "Decimator" style: it listens to the clean guitar signal (the DI,
// before any pedal) but mutes wherever it sits in the chain. Put it AFTER the amp
// and cab: it then removes the amp's hiss and hum between notes, and it never
// mistakes amplified noise for playing.
//
// Auto mode measures the noise floor continuously (the quietest 20 ms peak level
// over the last 6 s) and opens 'Auto Offset' dB above it (closing 6 dB lower), so it
// adapts to your guitar, pickups and interface gain on its own.
class NoiseGate final : public Module
{
public:
    NoiseGate() : Module ({ ParamSpec::toggle ("auto", "Auto Threshold", true),
                            ParamSpec::knob ("thresh", "Threshold", -90, -10, -60, " dB"),
                            ParamSpec::knob ("margin", "Auto Offset", 6, 30, 12, " dB"),
                            ParamSpec::knob ("release", "Release", 5, 1000, 120, " ms", 120) }) {}

    juce::String typeId() const override      { return "gate"; }
    juce::String displayName() const override { return "Noise Gate"; }
    juce::String category() const override    { return "Dynamics"; }
    juce::uint32 colour() const override      { return 0xff3d4a57; }
    juce::String credit() const override      { return "Detects on the clean guitar signal - place it after amp & cab"; }

    bool showsStatus() const override { return true; }
    juce::String getStatusText() const override
    {
        const float fl = floorDb.load(), th = threshDb.load();
        juce::String s;
        if (get (0) > 0.5f)
        {
            if (fl > 0.0f) return "Auto: listening for the noise floor...";
            s << "noise floor " << juce::String (juce::roundToInt (fl)) << " dB  ->  ";
        }
        s << "threshold " << juce::String (juce::roundToInt (th)) << " dB";
        return s;
    }

protected:
    void prepareDsp (double, int) override
    {
        blockLen = std::max (1, (int) (0.02 * sr));       // 20 ms level blocks
        std::fill (std::begin (history), std::end (history), 0.0f);
        histPos = 0; histFilled = 0; blockAcc = 0; blockCount = 0;
        floorDb.store (1.0f);
    }

    void resetDsp() noexcept override { env = 0; gain = 0; holdLeft = 0; open = false; }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const bool autoMode = get (0) > 0.5f;
        const float* det = key != nullptr ? key : L;
        const float envRel = std::exp (-1.0f / (0.030f * (float) sr));
        const float gAtt   = 1.0f - std::exp (-1.0f / (0.0005f * (float) sr));
        const float gRel   = 1.0f - std::exp (-1.0f / (get (3) * 0.001f * (float) sr));
        const int   holdLen = (int) (0.06 * sr);

        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (det[i]);

            // --- noise-floor tracker (minimum of 20 ms RMS blocks over ~6 s) ---
            blockAcc = std::max (blockAcc, a);       // peak of this 20 ms block (hum/hiss peaks, not average)
            if (++blockCount >= blockLen)
            {
                if (blockAcc > 1.0e-5f) // ignore digital silence (muted input, start-up) - it's not the noise floor
                {
                    history[(size_t) histPos] = blockAcc;
                    histPos = (histPos + 1) % kHistory;
                    histFilled = std::min (histFilled + 1, kHistory);
                }
                blockAcc = 0; blockCount = 0;
                float mn = 1.0f;
                for (int k = 0; k < histFilled; ++k) mn = std::min (mn, history[(size_t) k]);
                // A real noise floor is never louder than about -57 dBFS peak: anything above that
                // is quiet playing (e.g. you started playing the moment the app opened).
                floorDb.store (juce::jlimit (-100.0f, -57.0f, gainToDb (mn)));
            }
            if (autoMode && histFilled == 0) { L[i] = 0.0f; if (stereo) R[i] = 0.0f; continue; } // nothing measured yet
            const float th = autoMode ? std::min (-20.0f, floorDb.load (std::memory_order_relaxed) + get (2))
                                      : get (1);
            if ((i & 31) == 0) { thrOpen = dbToGain (th); thrClose = thrOpen * 0.5f; threshDb.store (th, std::memory_order_relaxed); }

            // --- gate ---
            env = a > env ? a : env * envRel;
            if (env > thrOpen)        { open = true; holdLeft = holdLen; }
            else if (env < thrClose)  { if (holdLeft > 0) --holdLeft; else open = false; }

            const float target = open ? 1.0f : 0.0f;
            gain += (target - gain) * (target > gain ? gAtt : gRel);
            L[i] *= gain;
            if (stereo) R[i] *= gain;
        }
    }

private:
    static constexpr int kHistory = 300; // 300 x 20 ms = 6 s
    float history[kHistory] {};
    int histPos = 0, histFilled = 0, blockLen = 960, blockCount = 0;
    float blockAcc = 0;
    std::atomic<float> floorDb { 1.0f }, threshDb { -60.0f };   // floorDb > 0: not measured yet
    float thrOpen = 0.001f, thrClose = 0.0005f;
    float env = 0, gain = 0; int holdLeft = 0; bool open = false;
};

//==============================================================================
// Feed-forward compressor with soft knee (classic "sustainer" before drives).
class Compressor final : public Module
{
public:
    Compressor() : Module ({ ParamSpec::knob ("thresh", "Sustain", -50, 0, -24, " dB"),
                             ParamSpec::knob ("ratio", "Ratio", 1.5f, 20, 4, ":1", 4),
                             ParamSpec::knob ("attack", "Attack", 0.3f, 50, 5, " ms", 5),
                             ParamSpec::knob ("release", "Release", 20, 800, 150, " ms", 150),
                             ParamSpec::knob ("level", "Level", -12, 18, 0, " dB"),
                             ParamSpec::knob ("blend", "Blend", 0, 100, 100, " %") }) {}

    juce::String typeId() const override      { return "comp"; }
    juce::String displayName() const override { return "Squeeze Comp"; }
    juce::String category() const override    { return "Dynamics"; }
    juce::uint32 colour() const override      { return 0xff2f6fb0; }

protected:
    void prepareDsp (double, int) override {}
    void resetDsp() noexcept override { envDb = -120.0f; }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const float thr = get (0), ratio = get (1);
        const float att = std::exp (-1.0f / (get (2) * 0.001f * (float) sr));
        const float rel = std::exp (-1.0f / (get (3) * 0.001f * (float) sr));
        const float makeup = dbToGain (get (4) + 3.0f);   // Level 0 dB ~ unity loudness at default settings
        const float blend = get (5) * 0.01f;
        constexpr float knee = 6.0f;

        for (int i = 0; i < n; ++i)
        {
            const float a = stereo ? std::max (std::abs (L[i]), std::abs (R[i])) : std::abs (L[i]);
            const float inDb = gainToDb (a);
            envDb = inDb > envDb ? att * envDb + (1 - att) * inDb : rel * envDb + (1 - rel) * inDb;

            // soft-knee gain computer
            const float over = envDb - thr;
            float grDb = 0.0f;
            if (over > knee * 0.5f)        grDb = over * (1.0f / ratio - 1.0f);
            else if (over > -knee * 0.5f)  grDb = (1.0f / ratio - 1.0f) * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.0f * knee);

            const float g = dbToGain (grDb) * makeup;
            const float mix = blend * g + (1.0f - blend);
            L[i] *= mix;
            if (stereo) R[i] *= mix;
        }
    }

private:
    float envDb = -120.0f;
};

//==============================================================================
// Clean boost with optional "bright" switch – pushes the amp/pedal after it.
class CleanBoost final : public Module
{
public:
    CleanBoost() : Module ({ ParamSpec::knob ("gain", "Boost", 0, 24, 8, " dB"),
                             ParamSpec::knob ("lowcut", "Low Cut", 20, 600, 40, " Hz", 120),
                             ParamSpec::toggle ("bright", "Bright", false) }) {}

    juce::String typeId() const override      { return "boost"; }
    juce::String displayName() const override { return "Clean Boost"; }
    juce::String category() const override    { return "Drive"; }
    juce::uint32 colour() const override      { return 0xffc9c4b8; }
    juce::String credit() const override      { return "Inspired by MXR Micro Amp / EP Booster"; }

protected:
    void prepareDsp (double, int) override { lastCut = -1; lastBright = -1; }
    void resetDsp() noexcept override { for (auto& b : hp) b.reset(); for (auto& b : shelf) b.reset(); }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const float cut = get (1); const int bright = (int) get (2);
        if (differs (cut, lastCut) || bright != lastBright)
        {
            for (auto& b : hp) b.highPass (sr, cut);
            for (auto& b : shelf) b.highShelf (sr, 2500.0, bright ? 5.0 : 0.0);
            lastCut = cut; lastBright = bright;
        }
        const float g0 = gainPrev, g1 = dbToGain (get (0));
        gainPrev = g1;

        forEachActiveChannel (L, R, n, stereo, [&] (int ch, float* x, int len)
        {
            const float step = (g1 - g0) / (float) len;
            for (int i = 0; i < len; ++i)
                x[i] = shelf[ch].process (hp[ch].process (x[i])) * (g0 + step * (float) (i + 1));
        });
    }

private:
    Biquad hp[2], shelf[2];
    float lastCut = -1; int lastBright = -1;
    float gainPrev = 1.0f;
};

} // namespace pg
