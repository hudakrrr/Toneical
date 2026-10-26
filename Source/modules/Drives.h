// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"
#include <juce_dsp/juce_dsp.h>

namespace pg
{
//==============================================================================
// Base for nonlinear pedals: runs processOversampled() at 4x the host rate so
// the clipping doesn't alias (fold high harmonics back down as harsh fizz).
class OversampledModule : public Module
{
public:
    using Module::Module;

protected:
    // 2^2 = 4x by default; hard-clipping, very bright stages override this with 3 (8x)
    virtual int oversamplingLog2() const { return 2; }

    void prepareDsp (double sampleRate, int maxBlockSize) override
    {
        const int f = oversamplingLog2();
        os = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) f, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) maxBlockSize);
        osRate = sampleRate * (1 << f);
        prepareOversampled (osRate);
    }

    void resetDsp() noexcept override
    {
        if (os) os->reset();
        resetOversampled();
    }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        float* chans[2] = { L, R };
        juce::dsp::AudioBlock<float> block (chans, stereo ? 2u : 1u, (size_t) n);
        auto up = os->processSamplesUp (block);
        updateCoefficients();
        for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
            processOversampled ((int) ch, up.getChannelPointer (ch), (int) up.getNumSamples());
        os->processSamplesDown (block);
    }

    virtual void prepareOversampled (double rate) = 0;
    virtual void resetOversampled() noexcept = 0;
    virtual void updateCoefficients() noexcept = 0;           // once per block, before channels
    virtual void processOversampled (int ch, float* x, int n) noexcept = 0;

    double osRate = 192000.0;

private:
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
};

// classic 0..10 pedal knob
inline ParamSpec pedalKnob (juce::String id, juce::String name, float def)
{
    return ParamSpec::knob (id, name, 0, 10, def);
}

} // namespace pg
