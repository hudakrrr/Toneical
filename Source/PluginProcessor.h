// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Chain.h"
#include "Tuner.h"
#include "recorder/Session.h"

class ToneicalProcessor final : public juce::AudioProcessor,
                               private juce::Timer
{
public:
    ToneicalProcessor();
    ~ToneicalProcessor() override;

    // --- AudioProcessor -----------------------------------------------------------------
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Toneical"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // --- app ---------------------------------------------------------------------------
    pg::Chain& getChain() noexcept { return chain; }
    pg::Tuner& getTuner() noexcept { return tuner; }
    pg::rec::Session& getSession() noexcept { return session; }
    void toggleRecord();     // record button: start (on the armed track, with the current rig) / punch out
    juce::AudioProcessorValueTreeState& getParams() noexcept { return params; }

    double getCurrentSampleRate() const noexcept { return currentRate.load(); }
    float takeInputPeak() noexcept  { return inPeak.exchange (0.0f); }
    float takeOutputPeak() noexcept { return outPeak.exchange (0.0f); }
    int getNumInputChannelsActive() const noexcept { return activeInputs.load(); }

    juce::String currentPresetName { "Classic Crunch" };

    // --- input calibration: play hard for a few seconds, input gain is set so peaks hit kCalibTargetDb
    static constexpr float kCalibTargetDb = -6.0f;
    void startInputCalibration() noexcept { calibPeak.store (0.0f); calibrating.store (true); }
    float finishInputCalibration() noexcept  // returns the raw peak (linear) seen while calibrating
    {
        calibrating.store (false);
        return calibPeak.load();
    }
    bool isCalibrating() const noexcept { return calibrating.load(); }
    float getLimiterReductionDb() const noexcept { return limiterGrDb.load(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    void timerCallback() override { chain.collectGarbage(); session.pump(); }

    juce::AudioProcessorValueTreeState params;
    std::atomic<float>* inputSelect = nullptr;
    std::atomic<float>* inputGainDb = nullptr;
    std::atomic<float>* outputGainDb = nullptr;
    std::atomic<float>* tunerMute = nullptr;

    pg::Chain chain;
    pg::Tuner tuner;
    pg::rec::Session session;

    std::vector<float> workL, workR, workDi, workClick;
    int maxBlock = 512;
    float lastInGain = 1, lastOutGain = 1;

    std::atomic<double> currentRate { 48000.0 };
    std::atomic<float> inPeak { 0 }, outPeak { 0 };
    std::atomic<int> activeInputs { 0 };
    std::atomic<bool> calibrating { false };
    std::atomic<float> calibPeak { 0 }, limiterGrDb { 0 };

    // output limiter (ceiling -1 dBFS, no latency): fast peak follower + soft clip for the rest
    float limEnv = 0, limGain = 1, limRel = 0.999f, limAtt = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneicalProcessor)
};
