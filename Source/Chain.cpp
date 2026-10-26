// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Chain.h"
#include "modules/ModuleFactory.h"

namespace pg
{
Chain::Chain()
{
    live = std::make_unique<Snapshot>();
    current.store (live.get());
}

Chain::~Chain()
{
    current.store (nullptr);
}

std::vector<ModulePtr> Chain::getModules() const
{
    const std::lock_guard<std::mutex> lock (editMutex);
    return live->modules;
}

void Chain::publish (std::vector<ModulePtr> modules)
{
    // Prepare modules that are new to the chain before the audio thread can see them.
    for (auto& m : modules)
    {
        const bool known = std::any_of (live->modules.begin(), live->modules.end(),
                                        [&] (const ModulePtr& o) { return o == m; });
        if (! known)
            m->prepare (sr, maxBlock);
    }

    auto next = std::make_unique<Snapshot>();
    next->modules = std::move (modules);
    current.store (next.get(), std::memory_order_release);

    retired.emplace_back (std::move (live), blocksDone.load());
    live = std::move (next);
}

void Chain::setModules (std::vector<ModulePtr> modules)
{
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        publish (std::move (modules));
    }
    sendChangeMessage();
}

void Chain::insert (ModulePtr module, int index)
{
    if (module == nullptr) return;
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        auto mods = live->modules;
        if (index < 0 || index > (int) mods.size()) index = (int) mods.size();
        mods.insert (mods.begin() + index, std::move (module));
        publish (std::move (mods));
    }
    sendChangeMessage();
}

void Chain::remove (int index)
{
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        auto mods = live->modules;
        if (index < 0 || index >= (int) mods.size()) return;
        mods.erase (mods.begin() + index);
        publish (std::move (mods));
    }
    sendChangeMessage();
}

void Chain::move (int from, int to)
{
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        auto mods = live->modules;
        const int n = (int) mods.size();
        if (from < 0 || from >= n || to < 0 || to >= n || from == to) return;
        auto m = mods[(size_t) from];
        mods.erase (mods.begin() + from);
        mods.insert (mods.begin() + to, m);
        publish (std::move (mods));
    }
    sendChangeMessage();
}

void Chain::collectGarbage()
{
    // Modules are shared_ptrs: dropping a snapshot only destroys modules that were
    // removed from the chain, and that happens here on the message thread.
    std::vector<std::unique_ptr<Snapshot>> toFree;
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        const auto done = blocksDone.load();
        const bool running = audioRunning.load();
        for (auto it = retired.begin(); it != retired.end();)
        {
            if (! running || done >= it->second + 2)
            {
                toFree.push_back (std::move (it->first));
                it = retired.erase (it);
            }
            else ++it;
        }
    }
    toFree.clear(); // destructors run outside the lock (NAM models can be big)
}

void Chain::prepare (double sampleRate, int maxBlockSize)
{
    const std::lock_guard<std::mutex> lock (editMutex);
    sr = sampleRate;
    maxBlock = std::max (16, maxBlockSize);
    keyBuf.assign ((size_t) maxBlock, 0.0f);
    for (auto& m : live->modules)
        m->prepare (sr, maxBlock);
    audioRunning.store (true);
}

void Chain::release()
{
    audioRunning.store (false);
}

void Chain::process (float* L, float* R, int numSamples) noexcept
{
    auto* snap = current.load (std::memory_order_acquire);
    if (snap != nullptr)
    {
        for (int offset = 0; offset < numSamples; offset += maxBlock)
        {
            const int n = std::min (maxBlock, numSamples - offset);
            float* l = L + offset;
            float* r = R + offset;
            std::copy (l, l + n, keyBuf.data());
            bool stereo = false;
            for (auto& m : snap->modules)
            {
                m->setKey (keyBuf.data());
                m->process (l, r, n, stereo);
            }
            if (! stereo)
                std::copy (l, l + n, r);
        }
    }
    else
    {
        std::copy (L, L + numSamples, R);
    }
    blocksDone.fetch_add (1, std::memory_order_release);
}

juce::ValueTree Chain::toValueTree() const
{
    juce::ValueTree v ("Chain");
    for (auto& m : getModules())
        v.appendChild (m->toValueTree(), nullptr);
    return v;
}

void Chain::fromValueTree (const juce::ValueTree& v)
{
    std::vector<ModulePtr> mods;
    for (auto child : v)
    {
        if (! child.hasType ("Module")) continue;
        if (auto m = ModuleFactory::create (child.getProperty ("type").toString()))
        {
            m->fromValueTree (child);
            mods.push_back (std::move (m));
        }
    }
    setModules (std::move (mods));
}

} // namespace pg
