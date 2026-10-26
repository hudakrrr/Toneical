// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"
#include <juce_events/juce_events.h>

namespace nam { class DSP; }

namespace pg
{
//==============================================================================
// Neural Amp Modeler slot – loads any .nam capture (amp head, full rig or pedal)
// using the MIT-licensed NeuralAmpModelerCore. Thousands of free captures are on
// tone3000.com. Use several of these in a chain (e.g. a Klon capture into a
// Plexi capture) and put a cab IR after amp-head captures.
//
// Models are loaded and pre-warmed on a background thread, then swapped in
// under a spin lock that the audio thread only ever *tries* to take.
class NamModule final : public Module
{
public:
    NamModule();
    ~NamModule() override;

    juce::String typeId() const override      { return "nam"; }
    juce::String displayName() const override { return "NAM Capture"; }
    juce::String category() const override    { return "Amp"; }
    juce::uint32 colour() const override      { return 0xff1f2a36; }
    juce::String credit() const override      { return "NeuralAmpModelerCore (MIT) by Steven Atkinson"; }

    bool hasFileSlot() const override           { return true; }
    juce::String fileWildcard() const override  { return "*.nam"; }
    juce::String fileSlotLabel() const override { return "Load .nam..."; }

    bool loadFile (const juce::File&, juce::String& error) override;
    void clearFile() override;
    juce::String getStatusText() const override;

protected:
    void prepareDsp (double sampleRate, int maxBlockSize) override;
    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override;

private:
    void loadOnWorker (juce::File file);

    juce::SpinLock modelLock;
    std::unique_ptr<nam::DSP> model;        // guarded by modelLock
    std::vector<float> inBuf, outBuf;
    float lastOutGain = 1.0f;               // audio thread only

    std::atomic<float> modelRate { -1.0f }, normGainDb { 0.0f };
    std::atomic<bool> loading { false }, hasLoudness { false };
    juce::String lastError;                 // message thread only
    mutable juce::CriticalSection statusLock;

    std::atomic<double> preparedRate { 48000.0 };
    std::atomic<int> preparedBlock { 512 };

    juce::ThreadPool loader { juce::ThreadPoolOptions{}.withNumberOfThreads (1).withThreadName ("NAM loader") };
};

} // namespace pg
