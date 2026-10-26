// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Module.h"

namespace pg
{
#ifdef TONEICAL_TUNING
double tuneOverride (const char* type, const char* key, double defaultValue)
{
    const auto spec = juce::SystemStats::getEnvironmentVariable ("TONEICAL_TUNE", {});
    const auto wanted = juce::String (type) + "." + key;
    for (auto kv : juce::StringArray::fromTokens (spec, ",;", ""))
        if (kv.upToFirstOccurrenceOf ("=", false, false).trim() == wanted)
            return kv.fromFirstOccurrenceOf ("=", false, false).getDoubleValue();
    return defaultValue;
}
#endif

Module::Module (std::vector<ParamSpec> s)
    : specs (std::move (s)),
      values (new std::atomic<float>[specs.size()])
{
    for (size_t i = 0; i < specs.size(); ++i)
        values[i].store (specs[i].defaultValue);
}

void Module::set (int index, float v) noexcept
{
    if (index < 0 || index >= numParams()) return;
    const auto& s = specs[(size_t) index];
    v = juce::jlimit (s.minValue, s.maxValue, v);
    if (s.kind != ParamSpec::Kind::knob) v = std::round (v);
    const float old = values[(size_t) index].exchange (v, std::memory_order_relaxed);
    if (differs (old, v)) parameterChanged (index);
}

int Module::indexOf (const juce::String& paramId) const
{
    for (size_t i = 0; i < specs.size(); ++i)
        if (specs[i].id == paramId) return (int) i;
    return -1;
}

juce::File Module::getLoadedFile() const
{
    const juce::ScopedLock sl (fileLock);
    return loadedFile;
}

void Module::setLoadedFile (const juce::File& f)
{
    const juce::ScopedLock sl (fileLock);
    loadedFile = f;
}

void Module::prepare (double sampleRate, int maxBlockSize)
{
    sr = sampleRate;
    maxBlock = std::max (1, maxBlockSize);
    dryL.assign ((size_t) maxBlock, 0.0f);
    dryR.assign ((size_t) maxBlock, 0.0f);
    wetMix.prepare (sr, 0.015);
    lastBypassed = bypassed.load();
    wetMix.snap (lastBypassed ? 0.0f : 1.0f);
    prepareDsp (sr, maxBlock);
    resetDsp();
}

void Module::process (float* L, float* R, int n, bool& stereo) noexcept
{
    // signal meter for the GUI (LEDs and pilot lights glow with your playing)
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i) pk = std::max (pk, std::abs (L[i]));
        if (pk > meter.load (std::memory_order_relaxed)) meter.store (pk, std::memory_order_relaxed);
    }
    const bool byp = bypassed.load (std::memory_order_relaxed);
    if (byp != lastBypassed)
    {
        if (! byp && ! wetMix.isSmoothing() && wetMix.current <= 0.0f)
            resetDsp(); // coming back from full bypass – clear stale tails
        wetMix.set (byp ? 0.0f : 1.0f);
        lastBypassed = byp;
    }

    if (! wetMix.isSmoothing())
    {
        if (wetMix.current <= 0.0f) return;       // fully bypassed
        processDsp (L, R, n, stereo);              // fully engaged
        return;
    }

    // crossfading between dry and wet
    n = std::min (n, maxBlock);
    const bool stereoIn = stereo;
    std::copy (L, L + n, dryL.data());
    std::copy (stereoIn ? R : L, (stereoIn ? R : L) + n, dryR.data());

    processDsp (L, R, n, stereo);
    if (stereo && ! stereoIn)
    {
        // effect just produced stereo – nothing else to do, dryR is the mono dry
    }

    for (int i = 0; i < n; ++i)
    {
        const float w = wetMix.next();
        L[i] = dryL[(size_t) i] + w * (L[i] - dryL[(size_t) i]);
        if (stereo)
            R[i] = dryR[(size_t) i] + w * (R[i] - dryR[(size_t) i]);
    }
}

juce::ValueTree Module::toValueTree() const
{
    juce::ValueTree v ("Module");
    v.setProperty ("type", typeId(), nullptr);
    v.setProperty ("bypassed", bypassed.load(), nullptr);
    for (size_t i = 0; i < specs.size(); ++i)
        v.setProperty (juce::Identifier (specs[i].id), get ((int) i), nullptr);

    const auto f = getLoadedFile();
    if (f != juce::File())
        v.setProperty ("file", f.getFullPathName(), nullptr);
    return v;
}

void Module::fromValueTree (const juce::ValueTree& v)
{
    bypassed.store ((bool) v.getProperty ("bypassed", false));
    for (size_t i = 0; i < specs.size(); ++i)
        set ((int) i, (float) v.getProperty (juce::Identifier (specs[i].id), specs[i].defaultValue));

    if (hasFileSlot() && v.hasProperty ("file"))
    {
        const juce::File f (v.getProperty ("file").toString());
        juce::String err;
        if (f.existsAsFile())
            loadFile (f, err);
    }
}

} // namespace pg
