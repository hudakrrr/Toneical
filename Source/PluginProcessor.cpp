// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

juce::AudioProcessorValueTreeState::ParameterLayout ToneicalProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "input", 1 }, "Input",
                                                        StringArray { "Input 1", "Input 2", "Inputs 1+2" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "ingain", 1 }, "Input Gain",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "outgain", 1 }, "Output Volume",
                                                       NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.0f), -6.0f,
                                                       AudioParameterFloatAttributes().withLabel ("dB")));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "tunermute", 1 }, "Mute (tuner)", false));
    return layout;
}

ToneicalProcessor::ToneicalProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "Params", createLayout())
{
    inputSelect  = params.getRawParameterValue ("input");
    inputGainDb  = params.getRawParameterValue ("ingain");
    outputGainDb = params.getRawParameterValue ("outgain");
    tunerMute    = params.getRawParameterValue ("tunermute");

    chain.setModules (pg::Presets::factory().front().build());
    startTimerHz (10);
}

ToneicalProcessor::~ToneicalProcessor()
{
    stopTimer();
}

bool ToneicalProcessor::isBusesLayoutSupported (const BusesLayout& layout) const
{
    const auto out = layout.getMainOutputChannelSet();
    const auto in  = layout.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void ToneicalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = std::max (32, samplesPerBlock);
    workL.assign ((size_t) maxBlock, 0.0f);
    workR.assign ((size_t) maxBlock, 0.0f);
    currentRate.store (sampleRate);
    tuner.prepare (sampleRate);
    chain.prepare (sampleRate, maxBlock);
    lastInGain  = juce::Decibels::decibelsToGain (inputGainDb->load());
    lastOutGain = juce::Decibels::decibelsToGain (outputGainDb->load(), -59.9f);
    limEnv = 0; limGain = 1;
    limRel = std::exp (-1.0f / (0.120f * (float) sampleRate));   // 120 ms release
    limAtt = 1.0f - std::exp (-1.0f / (0.0005f * (float) sampleRate));
}

void ToneicalProcessor::releaseResources()
{
    chain.release();
}

void ToneicalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total  = buffer.getNumSamples();
    activeInputs.store (numIn, std::memory_order_relaxed);

    const int sel = (int) inputSelect->load();
    const float inTarget  = juce::Decibels::decibelsToGain (inputGainDb->load());
    const float outTarget = tunerMute->load() > 0.5f ? 0.0f
                                                     : juce::Decibels::decibelsToGain (outputGainDb->load(), -59.9f);

    for (int offset = 0; offset < total; offset += maxBlock)
    {
        const int n = std::min (maxBlock, total - offset);
        float* L = workL.data();
        float* R = workR.data();

        // ---- pick the guitar input -----------------------------------------------------------
        const float* in0 = numIn > 0 ? buffer.getReadPointer (0, offset) : nullptr;
        const float* in1 = numIn > 1 ? buffer.getReadPointer (1, offset) : nullptr;
        float rawPeak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float s = 0.0f;
            if (sel == 0 && in0)                  s = in0[i];
            else if (sel == 1 && in1)             s = in1[i];
            else if (sel == 1 && in0)             s = in0[i];              // mono device: fall back
            else if (sel == 2)                    s = (in0 ? in0[i] : 0.0f) + (in1 ? in1[i] : 0.0f);
            L[i] = s;
            rawPeak = std::max (rawPeak, std::abs (s));
        }
        if (calibrating.load (std::memory_order_relaxed) && rawPeak > calibPeak.load (std::memory_order_relaxed))
            calibPeak.store (rawPeak, std::memory_order_relaxed);

        // ---- input gain + metering -------------------------------------------------------------
        const float ig0 = lastInGain, igStep = (inTarget - ig0) / (float) n;
        float peak = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            L[i] *= ig0 + igStep * (float) (i + 1);
            peak = std::max (peak, std::abs (L[i]));
        }
        lastInGain = inTarget;
        if (peak > inPeak.load (std::memory_order_relaxed)) inPeak.store (peak, std::memory_order_relaxed);
        tuner.push (L, n);

        // ---- the pedalboard ------------------------------------------------------------------
        chain.process (L, R, n);

        // ---- master volume + limiter ---------------------------------------------------------
        // Ceiling -1 dBFS: a fast peak limiter does the work, a soft clipper catches what's left,
        // so loud patches never hit the interface's harsh digital clipping.
        constexpr float ceiling = 0.891f; // -1 dBFS
        const float og0 = lastOutGain, ogStep = (outTarget - og0) / (float) n;
        float opeak = 0.0f, minGain = 1.0f;
        for (int i = 0; i < n; ++i)
        {
            const float g = og0 + ogStep * (float) (i + 1);
            float l = L[i] * g, r = R[i] * g;
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;
            const float pk = std::max (std::abs (l), std::abs (r));
            limEnv = pk > limEnv ? pk : limEnv * limRel + pk * (1.0f - limRel);
            const float target = limEnv > ceiling ? ceiling / limEnv : 1.0f;
            limGain += (target - limGain) * (target < limGain ? limAtt : 1.0f - limRel);
            minGain = std::min (minGain, limGain);
            l *= limGain; r *= limGain;
            auto soft = [] (float x) { // linear up to 0.8, then smoothly approaches 1.0
                const float a = std::abs (x);
                return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
            };
            L[i] = soft (l); R[i] = soft (r);
            opeak = std::max (opeak, std::max (std::abs (L[i]), std::abs (R[i])));
        }
        limiterGrDb.store (juce::Decibels::gainToDecibels (minGain), std::memory_order_relaxed);
        lastOutGain = outTarget;
        if (opeak > outPeak.load (std::memory_order_relaxed)) outPeak.store (opeak, std::memory_order_relaxed);

        if (numOut >= 2)
        {
            buffer.copyFrom (0, offset, L, n);
            buffer.copyFrom (1, offset, R, n);
        }
        else if (numOut == 1)
        {
            auto* o = buffer.getWritePointer (0, offset);
            for (int i = 0; i < n; ++i) o[i] = 0.5f * (L[i] + R[i]);
        }
    }

    for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, total);
}

juce::AudioProcessorEditor* ToneicalProcessor::createEditor()
{
    return new ToneicalEditor (*this);
}

void ToneicalProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("ToneicalState");
    root.setProperty ("preset", currentPresetName, nullptr);
    root.appendChild (params.copyState(), nullptr);
    root.appendChild (chain.toValueTree(), nullptr);
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, dest);
}

void ToneicalProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("ToneicalState")) return;

    currentPresetName = root.getProperty ("preset", "Custom").toString();
    auto p = root.getChildWithName (params.state.getType());
    if (p.isValid()) params.replaceState (p);
    auto c = root.getChildWithName ("Chain");
    if (c.isValid()) chain.fromValueTree (c);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ToneicalProcessor();
}
