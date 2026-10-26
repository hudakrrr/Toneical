// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "NamModule.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/activations.h"
#include "NAM/wavenet/model.h"
#include "NAM/lstm.h"
#include "NAM/convnet.h"
#include "NAM/linear.h"
#include "NAM/container.h"
#include "NAM/sequential.h"

namespace pg
{
namespace
{
    // NAM registers each architecture (WaveNet, LSTM, A2 containers...) with a static
    // object inside that architecture's .cpp. When linked from a static library the
    // linker drops object files nobody references - and the registrations with them
    // ("No config parser registered for architecture"). Taking the address of each
    // create_config() pulls those object files (and their registrations) in.
    using CreateFn = std::unique_ptr<nam::ModelConfig> (*) (const nlohmann::json&, double);
    [[maybe_unused]] volatile CreateFn keepNamArchitectures[] = {
        &nam::wavenet::create_config, &nam::lstm::create_config, &nam::convnet::create_config,
        &nam::linear::create_config, &nam::container::create_config, &nam::sequential::create_config
    };
}

NamModule::NamModule()
    : Module ({ ParamSpec::knob ("input", "Input", -24, 24, 0, " dB"),
                ParamSpec::knob ("output", "Output", -30, 18, 0, " dB"),
                ParamSpec::toggle ("normalize", "Normalize", true) })
{
    for (auto fn : keepNamArchitectures) (void) fn;
    nam::activations::Activation::enable_fast_tanh();   // as in the official NAM plugin: ~2x less CPU
}

NamModule::~NamModule()
{
    loader.removeAllJobs (true, 10000);
}

bool NamModule::loadFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile())
    {
        error = "File not found: " + file.getFullPathName();
        return false;
    }
    if (! file.hasFileExtension (".nam"))
    {
        error = "Not a .nam file";
        return false;
    }

    {
        const juce::ScopedLock sl (statusLock);
        lastError.clear();
    }
    loading.store (true);
    loader.addJob ([this, file] { loadOnWorker (file); });
    return true;
}

void NamModule::loadOnWorker (juce::File file)
{
    try
    {
        auto dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toStdString()));
        if (dsp == nullptr)
            throw std::runtime_error ("model could not be created");

        const double rate = preparedRate.load();
        const int block   = preparedBlock.load();
        dsp->ResetAndPrewarm (rate, block);

        const bool loud = dsp->HasLoudness();
        // the NAM plugin normalises captures to -18 dB "loudness"
        normGainDb.store (loud ? (float) (-18.0 - dsp->GetLoudness()) : 0.0f);
        hasLoudness.store (loud);
        modelRate.store ((float) dsp->GetExpectedSampleRate());

        {
            const juce::SpinLock::ScopedLockType sl (modelLock);
            std::swap (model, dsp);
            // device changed while we were pre-warming? reset to the current settings
            if (differs (preparedRate.load(), rate) || preparedBlock.load() != block)
                model->Reset (preparedRate.load(), preparedBlock.load());
        }
        dsp.reset(); // old model is destroyed here, on the worker thread
        setLoadedFile (file);
    }
    catch (const std::exception& e)
    {
        const juce::ScopedLock sl (statusLock);
        lastError = "Load failed: " + juce::String (e.what());
    }
    catch (...)
    {
        const juce::ScopedLock sl (statusLock);
        lastError = "Load failed (unknown error)";
    }
    loading.store (false);
}

void NamModule::clearFile()
{
    loader.removeAllJobs (true, 10000);
    std::unique_ptr<nam::DSP> old;
    {
        const juce::SpinLock::ScopedLockType sl (modelLock);
        std::swap (model, old);
    }
    old.reset();
    setLoadedFile ({});
    modelRate.store (-1.0f);
}

juce::String NamModule::getStatusText() const
{
    {
        const juce::ScopedLock sl (statusLock);
        if (lastError.isNotEmpty()) return lastError;
    }
    if (loading.load()) return "Loading model...";
    if (getLoadedFile() == juce::File()) return "No capture loaded (passing through)";

    juce::String s;
    const float mr = modelRate.load();
    const double dr = preparedRate.load();
    if (mr > 0)
    {
        s << juce::String (mr / 1000.0f, 1) << " kHz model";
        if (std::abs (mr - (float) dr) > 1.0f)
            s << "  -  WARNING: device runs at " << juce::String (dr / 1000.0, 1)
              << " kHz, set it to " << juce::String (mr / 1000.0f, 1) << " kHz!";
    }
    else
    {
        s << "Loaded (rate not stored in file - most captures are 48 kHz)";
    }
    if (hasLoudness.load())
        s << ", normalised " << juce::String (normGainDb.load(), 1) << " dB";
    return s;
}

void NamModule::prepareDsp (double sampleRate, int maxBlockSize)
{
    preparedRate.store (sampleRate);
    preparedBlock.store (maxBlockSize);
    inBuf.assign ((size_t) maxBlockSize, 0.0f);
    outBuf.assign ((size_t) maxBlockSize, 0.0f);

    const juce::SpinLock::ScopedLockType sl (modelLock);
    if (model != nullptr)
        model->ResetAndPrewarm (sampleRate, maxBlockSize);
}

void NamModule::processDsp (float* L, float* R, int n, bool& stereo) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (modelLock);
    if (! sl.isLocked() || model == nullptr)
        return; // nothing loaded (or swapping right now): pass through

    const float inG  = dbToGain (get (0));
    const float outG = dbToGain (get (1) + (get (2) > 0.5f ? normGainDb.load (std::memory_order_relaxed) : 0.0f));

    for (int i = 0; i < n; ++i)
        inBuf[(size_t) i] = (stereo ? 0.5f * (L[i] + R[i]) : L[i]) * inG;

    float* in[1]  = { inBuf.data() };
    float* out[1] = { outBuf.data() };
    model->process (in, out, n);

    // ramp the output gain across the block to avoid zipper noise
    const float g0 = lastOutGain, step = (outG - g0) / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const float y = outBuf[(size_t) i];
        L[i] = (std::isfinite (y) ? y : 0.0f) * (g0 + step * (float) (i + 1));
    }
    lastOutGain = outG;
    stereo = false; // captures are mono
}

} // namespace pg
