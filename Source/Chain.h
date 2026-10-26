// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "modules/Module.h"
#include <juce_events/juce_events.h>
#include <mutex>

namespace pg
{
//==============================================================================
// The pedalboard: an ordered list of modules.
//
// Edits happen on the message thread and are published to the audio thread as an
// immutable "snapshot" via an atomic pointer swap - the audio thread never waits.
// Old snapshots are freed later (collectGarbage) once the audio thread has
// provably finished at least one full block after the swap.
class Chain : public juce::ChangeBroadcaster
{
public:
    Chain();
    ~Chain() override;

    // ---- message thread ---------------------------------------------------------
    std::vector<ModulePtr> getModules() const;
    void setModules (std::vector<ModulePtr> modules);
    void insert (ModulePtr module, int index = -1);   // -1 = append
    void remove (int index);
    void move (int from, int to);
    void clear() { setModules ({}); }
    void collectGarbage();

    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    // ---- called from prepareToPlay / releaseResources ------------------------------
    void prepare (double sampleRate, int maxBlockSize);
    void release();

    // ---- audio thread -----------------------------------------------------------------
    // L/R in place. Produces a stereo result (R == L if nothing in the chain is stereo).
    void process (float* L, float* R, int numSamples) noexcept;

private:
    struct Snapshot { std::vector<ModulePtr> modules; };

    void publish (std::vector<ModulePtr> modules); // requires editMutex

    mutable std::mutex editMutex;
    std::unique_ptr<Snapshot> live;
    std::atomic<Snapshot*> current { nullptr };
    std::vector<std::pair<std::unique_ptr<Snapshot>, juce::uint64>> retired;

    std::atomic<juce::uint64> blocksDone { 0 };
    std::atomic<bool> audioRunning { false };
    double sr = 48000.0;
    int maxBlock = 512;
    std::vector<float> keyBuf = std::vector<float> (512, 0.0f); // clean DI copy for the gate
};

} // namespace pg
