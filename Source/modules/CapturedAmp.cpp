// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CapturedAmp.h"
#include "BinaryData.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/activations.h"
#include <mutex>

namespace pg
{
namespace
{
    struct Capture
    {
        const char* name;          // shown on the amp
        const char* source;        // capture author / original file (attribution, GPL)
        const char* data; int size;
        float trimDb;              // brings every channel to the same loudness (Gain 5, Master 0)
        float compPerDb;           // output change per dB of Gain drive, compensated automatically
    };

    // trim/compensation measured with a real DI guitar through the British 4x12 cab (tools/level_realamp.py)
    const Capture captures[CapturedAmp::numChannels] = {
        { "Clean (US Twin)",              "Tim R - Fender Twin Reverb, Normal, Bright",  BinaryData::clean_twin_nam,       BinaryData::clean_twin_namSize,       34.7f, 0.50f },
        { "Crunch (British 2000)",        "Tim R - Marshall JCM2000 Crunch",             BinaryData::crunch_jcm2000_nam,   BinaryData::crunch_jcm2000_namSize,   7.2f, 0.37f },
        { "Lead (British 2000)",          "Tim R - Marshall JCM2000 Lead 2, gain 6",     BinaryData::lead_jcm2000_g6_nam,  BinaryData::lead_jcm2000_g6_namSize,  4.3f, 0.09f },
        { "Lead+ (British 2000 hot)",     "Tim R - Marshall JCM2000 Lead 2, gain 8",     BinaryData::lead_jcm2000_g8_nam,  BinaryData::lead_jcm2000_g8_namSize,  4.1f, 0.05f },
        { "Chime (AC Top Boost)",         "Phillipe P - Vox AC15 Top Boost",             BinaryData::chime_ac15_nam,       BinaryData::chime_ac15_namSize,       12.5f, 0.15f },
        { "High Gain (US Block Letter)",  "Helga B - Peavey 5150 Block Letter, no boost", BinaryData::highgain_5150_nam,   BinaryData::highgain_5150_namSize,    15.9f, 0.02f },
    };

    // parsed once per process, shared by every Real Amp instance
    const nlohmann::json& parsed (int ch)
    {
        static std::mutex m;
        static std::unique_ptr<nlohmann::json> cache[CapturedAmp::numChannels];
        std::lock_guard<std::mutex> lock (m);
        if (cache[ch] == nullptr)
        {
            const auto& c = captures[ch];
            cache[ch] = std::make_unique<nlohmann::json> (nlohmann::json::parse (c.data, c.data + c.size));
        }
        return *cache[ch];
    }

    // Gain knob -> drive into the capture (dB). 5 = exactly as captured.
    float preDb (float knob) { return knob >= 5.0f ? (knob - 5.0f) * 2.4f : (knob - 5.0f) * 3.6f; }
}

juce::StringArray CapturedAmp::channelNames()
{
    juce::StringArray s;
    for (auto& c : captures) s.add (c.name);
    return s;
}

CapturedAmp::CapturedAmp()
    : Module ({ ParamSpec::choice ("channel", "Amp", channelNames(), 1),
                ParamSpec::knob ("gain", "Gain", 0, 10, 5),
                ParamSpec::knob ("bass", "Bass", 0, 10, 5),
                ParamSpec::knob ("mid", "Middle", 0, 10, 5),
                ParamSpec::knob ("treble", "Treble", 0, 10, 5),
                ParamSpec::knob ("presence", "Presence", 0, 10, 5),
                ParamSpec::knob ("master", "Master", -30, 12, 0, " dB") })
{
    // the fast tanh the official NAM plugin uses: halves the CPU cost, no audible difference
    nam::activations::Activation::enable_fast_tanh();
}

CapturedAmp::~CapturedAmp() = default;

void CapturedAmp::loadChannel (int ch)
{
    ch = juce::jlimit (0, numChannels - 1, ch);
    if (ch == modelChannel && model != nullptr) return;
    std::unique_ptr<nam::DSP> dsp;
    try
    {
        dsp = nam::get_dsp (parsed (ch));
        if (dsp != nullptr) dsp->Reset (sr, maxBlock);   // Reset() pre-warms the model
    }
    catch (...) { dsp.reset(); }

    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (model, dsp);
    }
    modelChannel = ch;
    activeChannel.store (model != nullptr ? ch : -1);
    // 'dsp' now holds the previous model; it's freed here, on the message thread
}

void CapturedAmp::parameterChanged (int index)
{
    if (index == 0 && prepared) loadChannel ((int) get (0));
}

void CapturedAmp::prepareDsp (double sampleRate, int maxBlockSize)
{
    inBuf.assign ((size_t) maxBlockSize, 0.0f);
    outBuf.assign ((size_t) maxBlockSize, 0.0f);
    dcHp.highPass (sampleRate, 12.0, 0.7);
    eqSeen = -1;
    prepared = true;
    const int ch = juce::jlimit (0, numChannels - 1, (int) get (0));
    if (model != nullptr && ch == modelChannel)
    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        model->Reset (sampleRate, maxBlockSize);
    }
    else
        loadChannel (ch);
}

void CapturedAmp::resetDsp() noexcept
{
    bass.reset(); mid.reset(); treble.reset(); presence.reset(); dcHp.reset();
}

void CapturedAmp::updateEq()
{
    auto db = [] (float knob, float range) { return (double) ((knob - 5.0f) / 5.0f * range); };
    bass.lowShelf (sr, 110.0, db (get (2), 9.0f), 0.8);
    mid.peak (sr, 650.0, 0.75, db (get (3), 8.0f));
    treble.highShelf (sr, 2600.0, db (get (4), 9.0f), 0.8);
    presence.highShelf (sr, 5500.0, db (get (5), 6.0f), 0.8);
}

void CapturedAmp::processDsp (float* L, float* R, int n, bool& stereo) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (modelLock);
    if (! sl.isLocked() || model == nullptr)
        return;   // switching captures right now: pass through for one block

    // EQ: recompute only when a tone knob moved (cheap check)
    const int v = (int) (get (2) * 7 + get (3) * 131 + get (4) * 1931 + get (5) * 28657);
    if (v != eqSeen) { updateEq(); eqSeen = v; }

    const int ch = activeChannel.load (std::memory_order_relaxed);
    const auto& cap = captures[juce::jlimit (0, numChannels - 1, ch)];
    const float pre = preDb (get (1));
    const float preG = dbToGain (pre);
    const float outG = dbToGain (cap.trimDb - cap.compPerDb * pre);
    const float master = dbToGain (get (6));

    const float p0 = lastPre, pStep = (preG - p0) / (float) n;
    for (int i = 0; i < n; ++i)
        inBuf[(size_t) i] = (stereo ? 0.5f * (L[i] + R[i]) : L[i]) * (p0 + pStep * (float) (i + 1));
    lastPre = preG;

    float* in[1]  = { inBuf.data() };
    float* out[1] = { outBuf.data() };
    model->process (in, out, n);

    const float o0 = lastOut, oStep = (outG - o0) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        float y = outBuf[(size_t) i];
        if (! std::isfinite (y)) y = 0.0f;
        y = presence.process (treble.process (mid.process (bass.process (dcHp.process (y)))));
        // soft ceiling (about 0.5 dB at +6 dBFS): a hot pickup into the clean amp can't make +30 dBFS peaks
        const float z = y * (o0 + oStep * (float) (i + 1));
        L[i] = 4.8f * std::tanh (z * (1.0f / 4.8f)) * master;
    }
    lastOut = outG;
    stereo = false;
}

juce::String CapturedAmp::getStatusText() const
{
    const int ch = activeChannel.load();
    if (ch < 0) return "Loading capture...";
    juce::String s ("Capture: ");
    s << captures[ch].source;
    if (std::abs (sr - 48000.0) > 1.0)
        s << "  -  WARNING: captured at 48 kHz, set your device to 48 kHz";
    return s;
}

} // namespace pg
