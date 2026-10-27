// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>

namespace pg
{
//==============================================================================
// Runs a batch of independent jobs (the live rig, each track's tone...) on helper
// threads, called from the audio thread.
//
// The calling thread works on the batch too and claims jobs one at a time, so it never
// waits for a helper that hasn't woken up yet - it simply does those jobs itself. It only
// waits for jobs a helper has already started. No locks, no allocation on the caller's side.
class WorkerPool
{
public:
    using Fn = void (*) (void* context, int index);

    // numHelpers < 0: one less than the number of CPU cores (at most 7)
    explicit WorkerPool (int numHelpers = -1)
    {
        if (numHelpers < 0) numHelpers = juce::jlimit (0, 7, juce::SystemStats::getNumCpus() - 1);
        for (int i = 0; i < numHelpers; ++i)
        {
            helpers.push_back (std::make_unique<Helper> (*this, i + 1));
            auto& h = *helpers.back();
            if (! h.startRealtimeThread (juce::Thread::RealtimeOptions{}.withPriority (8)))
                h.startThread (juce::Thread::Priority::highest);
        }
    }

    ~WorkerPool()
    {
        for (auto& h : helpers) { h->signalThreadShouldExit(); h->wake.signal(); }
        for (auto& h : helpers) h->stopThread (2000);
    }

    int getNumHelpers() const noexcept { return (int) helpers.size(); }

    // Runs fn (context, i) for every i in [0, count) and returns when all are done.
    // Only one thread may call run() at a time.
    void run (int count, Fn fn, void* context) noexcept
    {
        if (count <= 0) return;
        if (count == 1 || helpers.empty() || count > 0xffff)
        {
            for (int i = 0; i < count; ++i) fn (context, i);
            return;
        }
        const auto batch = ++batchCounter;
        jobFn.store (fn, std::memory_order_relaxed);
        jobContext.store (context, std::memory_order_relaxed);
        done.store (0, std::memory_order_relaxed);
        state.store (pack (batch, count, 0), std::memory_order_release);

        const int toWake = std::min (count - 1, (int) helpers.size());
        for (int i = 0; i < toWake; ++i) helpers[(size_t) i]->wake.signal();

        work (batch);
        for (int spins = 0; done.load (std::memory_order_acquire) < count; ++spins)
            if (spins > 64) std::this_thread::yield();   // a helper is finishing its job
    }

private:
    // state = batch id (32 bits) | job count (16) | next job index (16). All three live in one
    // atomic: a helper that wakes up late can never take a job of a newer batch, nor compare
    // an old batch's index against a new batch's job count.
    static juce::uint64 pack (juce::uint32 batch, int count, int index) noexcept
    {
        return ((juce::uint64) batch << 32) | ((juce::uint64) count << 16) | (juce::uint64) index;
    }

    // Claims and runs jobs of 'batch' until none are left.
    void work (juce::uint32 batch) noexcept
    {
        auto s = state.load (std::memory_order_acquire);
        for (;;)
        {
            if ((juce::uint32) (s >> 32) != batch) return;
            const int index = (int) (s & 0xffff), count = (int) ((s >> 16) & 0xffff);
            if (index >= count) return;
            if (state.compare_exchange_weak (s, s + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                // the batch can't end (and the job be replaced) while this job is unfinished
                jobFn.load (std::memory_order_relaxed) (jobContext.load (std::memory_order_relaxed), index);
                done.fetch_add (1, std::memory_order_release);
                s = state.load (std::memory_order_acquire);
            }
        }
    }

    struct Helper final : juce::Thread
    {
        Helper (WorkerPool& p, int n) : juce::Thread ("Toneical DSP " + juce::String (n)), pool (p) {}
        void run() override
        {
            while (! threadShouldExit())
            {
                wake.wait (200);
                if (threadShouldExit()) break;
                pool.work ((juce::uint32) (pool.state.load (std::memory_order_acquire) >> 32));
            }
        }
        WorkerPool& pool;
        juce::WaitableEvent wake;
    };

    std::vector<std::unique_ptr<Helper>> helpers;
    std::atomic<juce::uint64> state { 0 };
    std::atomic<Fn> jobFn { nullptr };
    std::atomic<void*> jobContext { nullptr };
    std::atomic<int> done { 0 };
    juce::uint32 batchCounter = 0;   // caller only

    JUCE_DECLARE_NON_COPYABLE (WorkerPool)
};

} // namespace pg
