// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Command-line renderer used for measuring / tuning the DSP.
//
//   ToneicalRender in.wav out.wav [options]
//     --preset NAME|N          run a factory preset (default: none)
//     --module TYPE [k=v ...]  append a module with parameters (repeatable)
//     --nam FILE               append a NAM Capture module with this model
//     --ir FILE                append an IR Loader with this impulse response
//     --gain DB                input gain before the chain
//     --block N                processing block size (default 128)
//
// The input is processed at its own sample rate, mono (first channel).

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "Chain.h"
#include "Presets.h"
#include "modules/ModuleFactory.h"

using namespace pg;

static int fail (const juce::String& msg) { fprintf (stderr, "%s\n", msg.toRawUTF8()); return 1; }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 3) return fail ("usage: ToneicalRender in.wav out.wav [--preset X] [--module type k=v...] [--nam f] [--ir f] [--gain dB]");

    const juce::File inFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File outFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (inFile));
    if (reader == nullptr) return fail ("cannot read " + inFile.getFullPathName());
    const double sr = reader->sampleRate;
    const int len = (int) reader->lengthInSamples;
    juce::AudioBuffer<float> in ((int) reader->numChannels, len);
    reader->read (&in, 0, len, 0, true, true);

    std::vector<ModulePtr> mods;
    float gainDb = 0.0f;
    int block = 128;

    for (int a = 3; a < argc; ++a)
    {
        const juce::String arg (argv[a]);
        auto next = [&] { return a + 1 < argc ? juce::String (argv[++a]) : juce::String(); };
        if (arg == "--gain") gainDb = next().getFloatValue();
        else if (arg == "--block") block = juce::jmax (16, next().getIntValue());
        else if (arg == "--preset")
        {
            const auto want = next();
            bool found = false;
            const auto& f = Presets::factory();
            for (size_t i = 0; i < f.size(); ++i)
                if (f[i].name.equalsIgnoreCase (want) || juce::String ((int) i + 1) == want)
                {
                    for (auto& m : f[i].build()) mods.push_back (m);
                    found = true;
                }
            if (! found) return fail ("unknown preset " + want);
        }
        else if (arg == "--module")
        {
            const auto type = next();
            auto m = ModuleFactory::create (type);
            if (m == nullptr) return fail ("unknown module " + type);
            while (a + 1 < argc && juce::String (argv[a + 1]).containsChar ('=') && ! juce::String (argv[a + 1]).startsWith ("--"))
            {
                const juce::String kv (argv[++a]);
                const int idx = m->indexOf (kv.upToFirstOccurrenceOf ("=", false, false));
                if (idx < 0) return fail ("unknown parameter " + kv + " for " + type);
                m->set (idx, kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());
            }
            mods.push_back (m);
        }
        else if (arg == "--nam" || arg == "--ir")
        {
            auto m = ModuleFactory::create (arg == "--nam" ? "nam" : "ir");
            juce::String err;
            if (! m->loadFile (juce::File::getCurrentWorkingDirectory().getChildFile (next()), err)) return fail (err);
            mods.push_back (m);
        }
        else return fail ("unknown option " + arg);
    }

    Chain chain;
    chain.prepare (sr, block);
    chain.setModules (mods);

    // wait for background loads (NAM models, IRs)
    std::vector<float> L ((size_t) block), R ((size_t) block);
    for (int w = 0; w < 600; ++w)
    {
        bool busy = false;
        for (auto& m : mods)
            if (m->getStatusText().startsWith ("Loading")) busy = true;
        if (! busy) break;
        std::fill (L.begin(), L.end(), 0.0f);
        chain.process (L.data(), R.data(), block); // lets the IR loader swap its new IR in
        juce::Thread::sleep (5);
    }
    for (auto& m : mods)
        if (m->showsStatus()) fprintf (stderr, "%s: %s\n", m->displayName().toRawUTF8(), m->getStatusText().toRawUTF8());
    // warm-up (settles filters, lets the IR loader finish), then reset by re-preparing
    for (int i = 0; i < 150; ++i)
    {
        std::fill (L.begin(), L.end(), 0.0f);
        chain.process (L.data(), R.data(), block);
        if (i % 10 == 0) juce::Thread::sleep (3); // convolution engines load their IRs on a background thread
    }

    juce::AudioBuffer<float> out (2, len);
    const float g = juce::Decibels::decibelsToGain (gainDb);
    const float* src = in.getReadPointer (0);
    for (int pos = 0; pos < len; pos += block)
    {
        const int n = juce::jmin (block, len - pos);
        float* l = out.getWritePointer (0, pos);
        float* r = out.getWritePointer (1, pos);
        for (int i = 0; i < n; ++i) l[i] = src[pos + i] * g;
        chain.process (l, r, n);
    }

    for (auto& m : mods)
        if (m->showsStatus()) fprintf (stderr, "after render - %s: %s\n", m->displayName().toRawUTF8(), m->getStatusText().toRawUTF8());
    outFile.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os (outFile.createOutputStream().release());
    if (os == nullptr) return fail ("cannot write " + outFile.getFullPathName());
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (sr).withNumChannels (2).withBitsPerSample (32);
    auto writer = wav.createWriterFor (os, opts);
    if (writer == nullptr) return fail ("cannot create writer");
    writer->writeFromAudioSampleBuffer (out, 0, len);
    return 0;
}
