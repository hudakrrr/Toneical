// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"
#include "dsp/DspHelpers.h"

namespace nam { class DSP; }

namespace pg
{
//==============================================================================
// "Toneical" (the Real Amp) - real amplifiers, captured.
//
// How the good commercial amp sims sound real: they either simulate the whole
// circuit component by component (grid current, coupling caps charging up,
// cathode bypass caps, the output transformer driving a speaker's impedance), or
// they *learn* the amp from recordings of it (Neural Amp Modeler, ToneX, Neural DSP).
// A handful of static saturation curves - what the Valve Head does - can match how
// loud and how distorted an amp gets, but not the way its distortion moves with
// your picking, and that's what makes a distorted amp sound alive.
//
// So this head plays neural captures of real amps (from the community collection
// pelennor2170/NAM_models, GPL v3; files in assets/nam): the captured amp does the
// distortion exactly as the real one did. Around it:
//   Gain   - drives the capture harder or softer (like a hotter or weaker pickup;
//            5 = as captured), with automatic level compensation
//   Bass / Middle / Treble / Presence - an EQ after the capture (5 = flat)
//   Master - output level
class CapturedAmp final : public Module
{
public:
    CapturedAmp();
    ~CapturedAmp() override;

    juce::String typeId() const override      { return "realamp"; }
    juce::String displayName() const override { return "Toneical"; }
    juce::String category() const override    { return "Amp"; }
    juce::uint32 colour() const override      { return 0xff6b2d22; }
    juce::String credit() const override
    {
        return "Neural captures of real amps (NAM community collection, GPL v3) - played with NeuralAmpModelerCore";
    }
    bool showsStatus() const override { return true; }
    juce::String getStatusText() const override;

    static constexpr int numChannels = 6;
    static juce::StringArray channelNames();

protected:
    void parameterChanged (int index) override;
    void prepareDsp (double sampleRate, int maxBlockSize) override;
    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override;
    void resetDsp() noexcept override;

private:
    void loadChannel (int ch);
    void updateEq();

    juce::SpinLock modelLock;
    std::unique_ptr<nam::DSP> model;       // guarded by modelLock
    int modelChannel = -1;                 // message thread
    std::atomic<int> activeChannel { -1 };
    bool prepared = false;

    std::vector<float> inBuf, outBuf;
    Biquad bass, mid, treble, presence, dcHp;
    float lastPre = 1.0f, lastOut = 1.0f;
    std::atomic<int> eqVersion { 0 };
    int eqSeen = -1;
};

} // namespace pg
