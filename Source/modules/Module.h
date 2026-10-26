// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>
#include <memory>
#include <vector>
#include "dsp/DspHelpers.h"

namespace pg
{
//==============================================================================
// Tuning hook: in the ToneicalRender tool (built with TONEICAL_TUNING) circuit
// constants can be overridden from the environment for fitting against real gear:
//   TONEICAL_TUNE="ds1.boostGainDb=33,ds1.clipV=0.6;klon.gMaxDb=38"
// In the app it simply returns the default.
#ifdef TONEICAL_TUNING
double tuneOverride (const char* type, const char* key, double defaultValue);
#else
inline double tuneOverride (const char*, const char*, double defaultValue) { return defaultValue; }
#endif

//==============================================================================
// Description of one knob / switch on a pedal.
struct ParamSpec
{
    enum class Kind { knob, choice, toggle };

    juce::String id, name;
    float minValue = 0, maxValue = 1, defaultValue = 0;
    juce::String suffix;
    Kind kind = Kind::knob;
    juce::StringArray choices; // for Kind::choice
    float skewCentre = 0;      // if > 0, knob uses this as its mid point

    static ParamSpec knob (juce::String id, juce::String name, float lo, float hi, float def,
                           juce::String suffix = {}, float centre = 0)
    {
        ParamSpec p; p.id = id; p.name = name; p.minValue = lo; p.maxValue = hi;
        p.defaultValue = def; p.suffix = suffix; p.skewCentre = centre; return p;
    }
    static ParamSpec choice (juce::String id, juce::String name, juce::StringArray items, int def)
    {
        ParamSpec p; p.id = id; p.name = name; p.kind = Kind::choice; p.choices = items;
        p.minValue = 0; p.maxValue = (float) (items.size() - 1); p.defaultValue = (float) def; return p;
    }
    static ParamSpec toggle (juce::String id, juce::String name, bool def)
    {
        ParamSpec p; p.id = id; p.name = name; p.kind = Kind::toggle;
        p.minValue = 0; p.maxValue = 1; p.defaultValue = def ? 1.0f : 0.0f; return p;
    }
};

//==============================================================================
// One pedal / amp / cab in the chain.
//
// Threading: the GUI writes parameters (atomics) from the message thread, the
// audio thread reads them in process(). prepare() is only called while the
// module is NOT being processed (before it is published to the audio thread,
// or from prepareToPlay while audio is stopped).
class Module
{
public:
    explicit Module (std::vector<ParamSpec> specs);
    virtual ~Module() = default;

    // --- identity --------------------------------------------------------------
    virtual juce::String typeId() const = 0;      // stable id used in presets
    virtual juce::String displayName() const = 0;
    virtual juce::String category() const = 0;    // "Dynamics", "Drive", "Amp", ...
    virtual juce::uint32 colour() const = 0;      // pedal enclosure colour (ARGB)
    virtual juce::String credit() const { return {}; } // what the design is based on

    // --- parameters -------------------------------------------------------------
    const std::vector<ParamSpec>& getSpecs() const noexcept { return specs; }
    int   numParams() const noexcept { return (int) specs.size(); }
    float get (int index) const noexcept { return values[(size_t) index].load (std::memory_order_relaxed); }
    void  set (int index, float v) noexcept;
    int   indexOf (const juce::String& paramId) const;

    std::atomic<bool> bypassed { false };

    // peak of the signal arriving at this module since the GUI last asked (GUI: exchange (0))
    std::atomic<float> meter { 0.0f };
    // 0..1 "how hard is it being played" for lights, decaying smoothly; call ~30x per second
    float takeGlow (float& smoothed)
    {
        const float pk = meter.exchange (0.0f, std::memory_order_relaxed);
        const float db = 20.0f * std::log10 (std::max (pk, 1.0e-6f));
        const float target = std::clamp ((db + 48.0f) / 42.0f, 0.0f, 1.0f);   // -48 dBFS .. -6 dBFS
        smoothed = target > smoothed ? target : smoothed * 0.86f + target * 0.14f;
        return smoothed;
    }

    // --- optional file slot (NAM model, impulse response) -----------------------
    virtual bool hasFileSlot() const { return false; }
    virtual juce::String fileWildcard() const { return {}; }
    virtual juce::String fileSlotLabel() const { return {}; }
    virtual bool loadFile (const juce::File&, juce::String& error) { error = "not supported"; return false; }
    virtual void clearFile() {}
    juce::File getLoadedFile() const;
    virtual juce::String getStatusText() const { return {}; } // shown under the file name

    // --- processing -------------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);

    // Processes in place. 'stereo' says whether R carries an independent signal;
    // stereo-creating effects (chorus, delay, reverb) set it to true.
    void process (float* L, float* R, int numSamples, bool& stereo) noexcept;

    // The clean guitar signal (before any pedal) for this block, set by the Chain.
    // Lets e.g. the noise gate detect playing on the DI even when it sits after the amp.
    void setKey (const float* cleanInput) noexcept { key = cleanInput; }

    // Optional one-line live status shown on the pedal (e.g. the gate's noise floor)
    virtual bool showsStatus() const { return hasFileSlot(); }

    // --- state ------------------------------------------------------------------
    juce::ValueTree toValueTree() const;
    void fromValueTree (const juce::ValueTree&);

    // Unique per-instance id so the GUI can track pedals across chain rebuilds
    const juce::Uuid uid;

protected:
    // Called after a parameter changed (message thread / preset load) - never on the audio thread.
    virtual void parameterChanged (int /*index*/) {}

    virtual void prepareDsp (double sampleRate, int maxBlockSize) = 0;
    virtual void processDsp (float* L, float* R, int numSamples, bool& stereo) noexcept = 0;
    virtual void resetDsp() noexcept {}

    // Helper for mono-in/mono-out effects: runs fn(channel, samples, n) on L (and R if stereo)
    template <typename Fn>
    void forEachActiveChannel (float* L, float* R, int n, bool stereo, Fn&& fn) noexcept
    {
        fn (0, L, n);
        if (stereo) fn (1, R, n);
    }

    void setLoadedFile (const juce::File& f);

    double sr = 48000.0;
    int maxBlock = 512;
    const float* key = nullptr; // clean DI for the current block (may be null)

private:
    std::vector<ParamSpec> specs;
    std::unique_ptr<std::atomic<float>[]> values;

    // click-free bypass
    Smoother wetMix;
    std::vector<float> dryL, dryR;
    bool lastBypassed = false;

    mutable juce::CriticalSection fileLock;
    juce::File loadedFile;
};

using ModulePtr = std::shared_ptr<Module>;

} // namespace pg
