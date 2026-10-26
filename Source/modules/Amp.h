// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Drives.h"

namespace pg
{
//==============================================================================
// "Valve Head" – a small physically-inspired tube amp:
//   input coupling cap -> 2-4 cascaded triode gain stages (asymmetric curves,
//   coupling-cap high-pass + Miller low-pass between stages) -> passive
//   Bass/Mid/Treble tone stack (Yeh & Smith model of the Fender/Marshall/Vox
//   circuit) -> push-pull power amp with sag and a presence control.
//
// The gain structure of each channel was fitted to Neural Amp Modeler captures of
// real amps (Fender Twin Reverb, Marshall JCM2000 crunch / lead, Vox AC15) by
// matching how output level and harmonic distortion change with picking strength.
// Put a cabinet (Cab Sim or IR Loader) after it!
class TubeAmp final : public OversampledModule
{
public:
    TubeAmp() : OversampledModule ({ ParamSpec::choice ("channel", "Channel", { "Clean (Blackface)", "Crunch (Plexi)", "Lead (Hot Rod)", "Chime (AC)" }, 1),
                                     pedalKnob ("gain", "Gain", 5),
                                     pedalKnob ("bass", "Bass", 5),
                                     pedalKnob ("mid", "Middle", 5),
                                     pedalKnob ("treble", "Treble", 6),
                                     pedalKnob ("presence", "Presence", 5),
                                     ParamSpec::knob ("master", "Master", -30, 12, 0, " dB") })
    {
        for (int ch = 0; ch < 4; ++ch) voices[ch] = defaultVoice (ch);
       #ifdef TONEICAL_TUNING
        applyTuningOverrides();
       #endif
    }

    juce::String typeId() const override      { return "tubeamp"; }
    juce::String displayName() const override { return "Valve Head"; }
    juce::String category() const override    { return "Amp"; }
    juce::uint32 colour() const override      { return 0xff8a5a2b; }
    juce::String credit() const override      { return "Tone stack: Yeh & Smith, DAFx 2006"; }

    static constexpr int maxStages = 4;

    struct Voice
    {
        int    stages;
        double gain0Db, gain10Db;   // total preamp small-signal gain with the Gain knob at 0 / 10 (dB-linear pot)
        double firstShare;          // fraction of that gain (in dB) given to the first stage
        double hp[maxStages];       // coupling-cap high-pass per stage (Hz) - higher = tighter bass
        double lp[maxStages];       // Miller-effect low-pass per stage (Hz) - lower = less fizz
        double brightDb;            // bright cap (treble boost before the first stage)
        double bias;                // triode asymmetry (even harmonics)
        int    stack;               // 0 Fender, 1 Marshall, 2 Vox
        double stackMakeupDb;       // recovers the tone stack's insertion loss
        double powerDb;             // drive into the power amp
        double sag;                 // supply sag amount
        double postLp;              // output transformer / speaker-load roll-off (Hz)
        double trimDb;              // output trim so all channels are equally loud
        double powerBias = 0.0;     // push-pull imbalance -> even harmonics from the power amp
    };

    // Fitted with tools/fit_amp.py against NAM captures of real amps (see README, "How the amp was tuned").
    static Voice defaultVoice (int ch)
    {
        switch (ch)
        {
            case 0: return { 2, -28.89, -4.89, 0.461, { 85, 114, 114, 114 }, { 22000, 22000, 22000, 22000 }, 9.00, 0.053, 0, 14.0, -34.38, 0.10, 20000, 36.0, 0.0 };
            case 1: return { 3, 20.69, 52.69, 0.100, { 41.6, 149.8, 233, 233 }, { 22000, 22000, 22000, 22000 }, 2.00, 0.490, 1, 14.0, -11.76, 0.11, 20000, -1.55, -0.103 };
            case 2: return { 4, 54.76, 86.76, 0.100, { 35.8, 161.0, 232.5, 286.2 }, { 22000, 22000, 22000, 22000 }, 2.00, 0.755, 1, 14.0, -9.36, 0.64, 9000, 2.43, 0.600 };
            default:  return { 2, 26.64, 48.64, 0.625, { 123, 286, 368, 368 }, { 12144, 8674, 7807, 7807 }, 9.00, 0.303, 2, 16.0, 4.93, 0.50, 3989, 5.69, 0.033 };
        }
    }

protected:
    int oversamplingLog2() const override { return 3; } // 8x: keeps aliasing inaudible on high notes
    void prepareOversampled (double) override { lastKey = -1; }

    void resetOversampled() noexcept override
    {
        for (int c = 0; c < 2; ++c)
        {
            inHp[c].reset(); stack[c].reset(); presence[c].reset(); dc[c].reset(); sagEnv[c] = 0;
            for (int s = 0; s < maxStages; ++s) { stLp[c][s].reset(); stHp[c][s].reset(); }
            bright[c].reset(); postLp[c].reset();
        }
    }

    void updateCoefficients() noexcept override
    {
        const int ch = juce::jlimit (0, 3, (int) get (0));
        const auto& v = voices[ch];
        const float gain = get (1) / 10.0f, bass = get (2) / 10.0f, mid = get (3) / 10.0f,
                    treb = get (4) / 10.0f, pres = get (5) / 10.0f;

        // only recompute filters when something changed (cheap hash of the knobs)
        const double knobKey = ch * 1e10 + std::round (bass * 1000) * 1e6 + std::round (mid * 1000) * 1e3
                             + std::round (treb * 1000) + pres * 0.1;
        if (differs (knobKey, lastKey))
        {
            const ToneStack::Values* vals[] = { &ToneStack::fender, &ToneStack::marshall, &ToneStack::vox };
            for (auto& st : stack) st.set (*vals[juce::jlimit (0, 2, v.stack)], bass, mid, treb, osRate);
            for (int c = 0; c < 2; ++c)
            {
                inHp[c].setCutoff (osRate, 20.0);
                for (int s = 0; s < maxStages; ++s)
                {
                    stHp[c][s].setCutoff (osRate, v.hp[s]);
                    stLp[c][s].setCutoff (osRate, v.lp[s]);
                }
                presence[c].highShelf (osRate, 3000.0, -4.0 + 10.0 * pres, 0.7);
                bright[c].highShelf (osRate, 1800.0, v.brightDb, 0.7);
                postLp[c].lowPass (osRate, v.postLp, 0.6);
                dc[c].prepare (osRate);
            }
            lastKey = knobKey;
        }

        // Gain knob: dB-linear (like an audio-taper pot) between gain0Db and gain10Db.
        // Stage 1 gets 'firstShare' of the total, the rest is split evenly, so soft
        // picking only saturates the last stage and hard picking drives every stage.
        const double totalDb = v.gain0Db + (v.gain10Db - v.gain0Db) * gain;
        numStages = v.stages;
        const double firstDb = totalDb * v.firstShare;
        const double restDb  = numStages > 1 ? (totalDb - firstDb) / (numStages - 1) : 0.0;
        for (int s = 0; s < maxStages; ++s)
            stageGain[s] = (float) std::pow (10.0, (s == 0 ? firstDb : restDb) / 20.0);
        bias      = (float) v.bias;
        makeUp    = (float) std::pow (10.0, v.stackMakeupDb / 20.0);
        drive     = (float) std::pow (10.0, v.powerDb / 20.0);
        sagAmount = (float) v.sag;
        powerBias = (float) v.powerBias;
        outGain   = dbToGain ((float) v.trimDb);
        masterGain = dbToGain (get (6));   // after the output stage: Master changes volume, not saturation
        sagAtt    = 1.0f - std::exp (-1.0f / (0.004f * (float) osRate));
        sagRel    = 1.0f - std::exp (-1.0f / (0.120f * (float) osRate));
    }

    void processOversampled (int c, float* x, int n) noexcept override
    {
        const float b = bias, tb = fastTanh (bias);
        for (int i = 0; i < n; ++i)
        {
            float v = bright[c].process (inHp[c].hp (x[i]));

            // preamp: inverting triode stages, so the asymmetry alternates
            for (int s = 0; s < numStages; ++s)
            {
                v = stHp[c][s].hp (v) * stageGain[s];
                v = (s & 1) ? -(fastTanh (-v + b) - tb) : (fastTanh (v + b) - tb);
                v = stLp[c][s].lp (v);
            }

            // passive tone stack (lossy) + make-up
            v = stack[c].process (v) * makeUp;

            // power amp: sag lowers the drive as the "supply" is loaded
            const float a = std::abs (v);
            sagEnv[c] += (a - sagEnv[c]) * (a > sagEnv[c] ? sagAtt : sagRel);
            const float sag = 1.0f / (1.0f + sagAmount * sagEnv[c]);
            v = presence[c].process (v);
            v = (fastTanh (v * drive * sag + powerBias) - fastTanh (powerBias)) / drive;   // unity small-signal gain
            // output stage (speaker / transformer limits): acts on the amp's own output level, BEFORE
            // the channel's level trim - so a clean channel stays clean and keeps its pick attack,
            // while absurd overloads are still rounded off. The app's limiter guards the final output.
            const float y = dc[c].process (postLp[c].process (v)) * 0.5f;
            // a soft ceiling after the trim (about 0.5 dB at +6 dBFS peaks) so a hot pickup into the
            // clean channel can't produce +30 dBFS peaks
            const float z = 4.0f * std::tanh (y * 0.25f) * outGain;
            x[i] = 4.8f * std::tanh (z * (1.0f / 4.8f)) * masterGain;
        }
    }

private:
   #ifdef TONEICAL_TUNING
    // TONEICAL_AMP="1:gain0Db=10,powerDb=-3;2:stages=3"  (render/tuning builds only)
    void applyTuningOverrides()
    {
        const auto spec = juce::SystemStats::getEnvironmentVariable ("TONEICAL_AMP", {});
        for (auto chunk : juce::StringArray::fromTokens (spec, ";", ""))
        {
            const int ch = chunk.upToFirstOccurrenceOf (":", false, false).getIntValue();
            if (ch < 0 || ch > 3) continue;
            auto& v = voices[ch];
            for (auto kv : juce::StringArray::fromTokens (chunk.fromFirstOccurrenceOf (":", false, false), ",", ""))
            {
                const auto k = kv.upToFirstOccurrenceOf ("=", false, false).trim();
                const double val = kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
                if (k == "stages") v.stages = juce::jlimit (1, maxStages, (int) val);
                else if (k == "gain0Db") v.gain0Db = val;
                else if (k == "gain10Db") v.gain10Db = val;
                else if (k == "firstShare") v.firstShare = val;
                else if (k == "brightDb") v.brightDb = val;
                else if (k == "bias") v.bias = val;
                else if (k == "stack") v.stack = (int) val;
                else if (k == "stackMakeupDb") v.stackMakeupDb = val;
                else if (k == "powerDb") v.powerDb = val;
                else if (k == "sag") v.sag = val;
                else if (k == "postLp") v.postLp = val;
                else if (k == "trimDb") v.trimDb = val;
                else if (k == "powerBias") v.powerBias = val;
                else if (k.startsWith ("hp")) v.hp[juce::jlimit (0, 3, k.getTrailingIntValue())] = val;
                else if (k.startsWith ("lp")) v.lp[juce::jlimit (0, 3, k.getTrailingIntValue())] = val;
            }
        }
    }
   #endif

    Voice voices[4];
    OnePole inHp[2], stLp[2][maxStages], stHp[2][maxStages];
    Biquad presence[2], bright[2], postLp[2];
    ToneStack stack[2];
    DcBlocker dc[2];
    float sagEnv[2] {};
    int numStages = 3;
    float stageGain[maxStages] { 1, 1, 1, 1 };
    float bias = 0.2f, makeUp = 5, drive = 1, sagAmount = 0.3f, powerBias = 0, outGain = 1, masterGain = 1, sagAtt = 0, sagRel = 0;
    double lastKey = -1;
};

//==============================================================================
// Speaker cabinet with built-in real cabinet impulse responses (no files needed).
// IRs: BestPlugins Mega Pack 2 (c) David Fau Casquel, GPL v2+ (via Guitarix), see assets/irs.
class CabSim final : public Module
{
public:
    CabSim() : Module ({ ParamSpec::choice ("cab", "Cabinet", { "4x12 Modern", "4x12 British", "4x12 Tight", "Boutique 2x12", "Small Combo" }, 0),
                         ParamSpec::knob ("bright", "Bright", -6, 6, 0, " dB"),
                         ParamSpec::knob ("thump", "Thump", -6, 6, 0, " dB"),
                         ParamSpec::knob ("level", "Level", -24, 12, 0, " dB") })
    {
        loadCab (0);
    }

    juce::String typeId() const override      { return "cabsim"; }
    juce::String displayName() const override { return "Cab Sim"; }
    juce::String category() const override    { return "Cab"; }
    juce::uint32 colour() const override      { return 0xff4b3a2f; }
    juce::String credit() const override      { return "Real cabinet IRs: BestPlugins Mega Pack 2 (David Fau Casquel, GPL)"; }

    static void getIr (int index, const void*& data, size_t& size);   // defined in ModuleFactory.cpp (BinaryData)

protected:
    void parameterChanged (int index) override { if (index == 0) loadCab ((int) get (0)); }

    void loadCab (int index)
    {
        const void* data = nullptr; size_t size = 0;
        getIr (juce::jlimit (0, 4, index), data, size);
        if (data != nullptr)
            conv.loadImpulseResponse (data, size, juce::dsp::Convolution::Stereo::no,
                                      juce::dsp::Convolution::Trim::no, 0,
                                      juce::dsp::Convolution::Normalise::yes);
    }

    void prepareDsp (double sampleRate, int maxBlockSize) override
    {
        conv.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        lastB = lastT = -100.0f;
    }
    void resetDsp() noexcept override { conv.reset(); for (auto& ch : f) for (auto& b : ch) b.reset(); }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const float br = get (1), th = get (2);
        if (differs (br, lastB) || differs (th, lastT))
        {
            for (auto& ch : f) { ch[0].highShelf (sr, 3000.0, br, 0.7); ch[1].peak (sr, 110.0, 1.0, th); }
            lastB = br; lastT = th;
        }
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, stereo ? 2u : 1u, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));

        // normalised IRs are quiet (+6 dB), plus a per-cabinet trim so all cabinets are equally loud
        static const float cabTrimDb[5] = { 0.0f, 0.7f, -1.0f, 0.9f, -0.9f };
        const float g = dbToGain (get (3) + cabTrimDb[juce::jlimit (0, 4, (int) get (0))]) * 2.0f;
        forEachActiveChannel (L, R, n, stereo, [&] (int c, float* x, int len)
        {
            for (int i = 0; i < len; ++i)
                x[i] = f[c][1].process (f[c][0].process (x[i])) * g;
        });
    }

private:
    juce::dsp::Convolution conv;
    Biquad f[2][2];
    float lastB = -100, lastT = -100;
};

//==============================================================================
// Impulse-response loader – load any cab (or room) IR .wav, e.g. from TONE3000.
class IrLoader final : public Module
{
public:
    IrLoader() : Module ({ ParamSpec::knob ("mix", "Mix", 0, 100, 100, " %"),
                           ParamSpec::knob ("lowcut", "Low Cut", 20, 400, 70, " Hz", 90),
                           ParamSpec::knob ("highcut", "High Cut", 2000, 20000, 9000, " Hz", 7000),
                           ParamSpec::knob ("level", "Level", -24, 12, 0, " dB") }) {}

    juce::String typeId() const override      { return "ir"; }
    juce::String displayName() const override { return "IR Loader"; }
    juce::String category() const override    { return "Cab"; }
    juce::uint32 colour() const override      { return 0xff5a4636; }

    bool hasFileSlot() const override              { return true; }
    juce::String fileWildcard() const override     { return "*.wav;*.aif;*.aiff;*.flac"; }
    juce::String fileSlotLabel() const override    { return "Load IR..."; }

    bool loadFile (const juce::File& file, juce::String& error) override
    {
        if (! file.existsAsFile()) { error = "File not found"; return false; }
        conv.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                  juce::dsp::Convolution::Trim::yes, 0,
                                  juce::dsp::Convolution::Normalise::yes);
        hasIr.store (true);
        setLoadedFile (file);
        return true;
    }

    void clearFile() override { hasIr.store (false); setLoadedFile ({}); }

    juce::String getStatusText() const override
    {
        if (! hasIr.load()) return "No IR loaded (passing through)";
        const auto len = conv.getCurrentIRSize();
        return len > 0 ? juce::String (len) + " samples (" + juce::String (juce::roundToInt (1000.0 * (double) len / sr)) + " ms)"
                       : "Loading...";
    }

protected:
    void prepareDsp (double sampleRate, int maxBlockSize) override
    {
        conv.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        wet.assign ((size_t) maxBlockSize * 2, 0.0f);
        lastLo = lastHi = -1;
    }
    void resetDsp() noexcept override { conv.reset(); for (auto& ch : f) for (auto& b : ch) b.reset(); }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        if (! hasIr.load (std::memory_order_relaxed)) return;

        const float lo = get (1), hi = get (2);
        if (differs (lo, lastLo) || differs (hi, lastHi))
        {
            for (auto& ch : f) { ch[0].highPass (sr, lo, 0.7); ch[1].lowPass (sr, hi, 0.7); }
            lastLo = lo; lastHi = hi;
        }
        const float mix = get (0) * 0.01f, g = dbToGain (get (3));
        const int nch = stereo ? 2 : 1;

        float* w[2] = { wet.data(), wet.data() + maxBlock };
        std::copy (L, L + n, w[0]);
        if (stereo) std::copy (R, R + n, w[1]);

        juce::dsp::AudioBlock<float> block (w, (size_t) nch, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (block));

        float* io[2] = { L, R };
        for (int c = 0; c < nch; ++c)
            for (int i = 0; i < n; ++i)
            {
                const float y = f[c][1].process (f[c][0].process (w[c][i]));
                io[c][i] = (io[c][i] + mix * (y - io[c][i])) * g;
            }
    }

private:
    juce::dsp::Convolution conv;
    std::vector<float> wet;
    Biquad f[2][2];
    std::atomic<bool> hasIr { false };
    float lastLo = -1, lastHi = -1;
};

} // namespace pg
