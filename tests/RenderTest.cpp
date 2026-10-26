// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// Offline sanity test: renders a synthetic plucked guitar through every module and
// every factory preset, checks for NaN/Inf/silence/clipping, measures CPU cost,
// optionally loads .nam models, and hammers the chain with edits from another
// thread while "audio" runs to catch threading bugs.
//
// usage: ToneicalRenderTest [outputDir] [model.nam ...]

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "Chain.h"
#include "Presets.h"
#include "modules/ModuleFactory.h"
#include <random>
#include <thread>

using namespace pg;

static constexpr double kRate = 48000.0;
static constexpr int kBlock = 128;

// Karplus-Strong pluck sequence: E2, A2, D3 chord-ish riff, ~4 s
static std::vector<float> makeGuitar (double seconds)
{
    std::vector<float> out ((size_t) (seconds * kRate), 0.0f);
    std::mt19937 rng (42);
    std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
    const float notes[] = { 82.41f, 110.0f, 146.83f, 196.0f, 82.41f, 123.47f, 164.81f, 246.94f };
    const double step = seconds / 8.0;
    for (int k = 0; k < 8; ++k)
    {
        const int start = (int) (k * step * kRate);
        const int period = (int) (kRate / notes[k]);
        std::vector<float> ring ((size_t) period);
        for (auto& v : ring) v = noise (rng) * 0.35f;
        float prev = 0;
        for (int i = start, j = 0; i < (int) out.size(); ++i, ++j)
        {
            const size_t idx = (size_t) (j % period);
            const float cur = ring[idx];
            ring[idx] = 0.996f * 0.5f * (cur + prev);
            prev = cur;
            out[(size_t) i] += cur;
        }
    }
    return out;
}

struct Result { float peak = 0, rms = 0; bool finite = true; double rtFactor = 0; };

static Result render (Chain& chain, const std::vector<float>& in, std::vector<float>& L, std::vector<float>& R)
{
    L = in; R.assign (in.size(), 0.0f);
    Result res;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (size_t pos = 0; pos < in.size(); pos += kBlock)
    {
        const int n = (int) std::min<size_t> (kBlock, in.size() - pos);
        chain.process (L.data() + pos, R.data() + pos, n);
    }
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    res.rtFactor = (1000.0 * (double) in.size() / kRate) / std::max (ms, 1e-3);
    double sum = 0;
    for (size_t i = 0; i < in.size(); ++i)
    {
        for (float v : { L[i], R[i] })
        {
            if (! std::isfinite (v)) res.finite = false;
            res.peak = std::max (res.peak, std::abs (v));
            sum += (double) v * v;
        }
    }
    res.rms = (float) std::sqrt (sum / (2.0 * (double) in.size()));
    return res;
}

static void writeWav (const juce::File& f, const std::vector<float>& L, const std::vector<float>& R)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    auto stream = f.createOutputStream();
    if (stream == nullptr) return;
    auto opts = juce::AudioFormatWriterOptions().withSampleRate (kRate).withNumChannels (2).withBitsPerSample (24);
    std::unique_ptr<juce::OutputStream> os (stream.release());
    if (auto w = wav.createWriterFor (os, opts))
    {
        const float* chans[2] = { L.data(), R.data() };
        w->writeFromFloatArrays (chans, 2, (int) L.size());
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory().getChildFile ("render");
    outDir.createDirectory();

    const auto guitar = makeGuitar (4.0);
    std::vector<float> L, R;
    int failures = 0;

    // peakLimit: single modules must stay below +13 dBFS; whole presets (which run into the
    // app's master volume and limiter) below +18 dBFS
    auto check = [&] (const juce::String& name, const Result& r, bool expectSound = true, float peakLimit = 4.5f)
    {
        const bool ok = r.finite && (! expectSound || r.rms > 1e-4f) && r.peak < peakLimit;
        if (! ok) ++failures;
        printf ("%-34s %s  peak %6.1f dBFS  rms %6.1f dBFS  %7.0fx realtime\n", name.toRawUTF8(), ok ? "OK  " : "FAIL",
                juce::Decibels::gainToDecibels (r.peak), juce::Decibels::gainToDecibels (r.rms), r.rtFactor);
    };

    printf ("== single modules ==\n");
    for (auto& info : ModuleFactory::all())
    {
        Chain chain;
        chain.prepare (kRate, kBlock);
        chain.setModules ({ ModuleFactory::create (info.typeId) });
        // also exercise every knob at both extremes
        auto m = chain.getModules().front();
        auto res = render (chain, guitar, L, R);
        check (info.name, res);
        for (int p = 0; p < m->numParams(); ++p)
            for (float v : { m->getSpecs()[(size_t) p].minValue, m->getSpecs()[(size_t) p].maxValue })
            {
                const float old = m->get (p);
                m->set (p, v);
                auto r2 = render (chain, guitar, L, R);
                if (! r2.finite || r2.peak > 8.0f)
                {
                    ++failures;
                    printf ("   FAIL %s %s=%g peak %g finite %d\n", info.name.toRawUTF8(),
                            m->getSpecs()[(size_t) p].id.toRawUTF8(), v, r2.peak, (int) r2.finite);
                }
                m->set (p, old);
            }
        chain.release();
        chain.collectGarbage();
    }

    printf ("\n== factory presets (WAVs in %s) ==\n", outDir.getFullPathName().toRawUTF8());
    writeWav (outDir.getChildFile ("00_dry_input.wav"), guitar, guitar);
    int k = 1;
    for (auto& preset : Presets::factory())
    {
        Chain chain;
        chain.prepare (kRate, kBlock);
        chain.setModules (preset.build());
        auto res = render (chain, guitar, L, R);
        check (preset.name, res, true, 8.0f);
        writeWav (outDir.getChildFile (juce::String (k++).paddedLeft ('0', 2) + "_"
                                       + preset.name.replaceCharacters (" &()/", "_____") + ".wav"), L, R);
    }

    // IR loader with a synthetic "cabinet" impulse response (decaying, low-passed noise)
    {
        std::vector<float> ir (2400);
        std::mt19937 rng (3);
        std::uniform_real_distribution<float> noise (-1.0f, 1.0f);
        float lp = 0;
        for (size_t i = 0; i < ir.size(); ++i)
        {
            lp += 0.25f * (noise (rng) - lp);
            ir[i] = lp * std::exp (-(float) i / 300.0f);
        }
        const auto irFile = outDir.getChildFile ("test_cab_ir.wav");
        writeWav (irFile, ir, ir);
        Chain chain;
        chain.prepare (kRate, kBlock);
        auto irMod = ModuleFactory::create ("ir");
        juce::String err;
        const bool ok = irMod->loadFile (irFile, err);
        chain.setModules ({ ModuleFactory::create ("tubeamp"), irMod });
        for (int w = 0; w < 200 && ! irMod->getStatusText().contains ("samples"); ++w)
        {
            render (chain, guitar, L, R); // the IR is swapped in on the audio thread
            juce::Thread::sleep (5);
        }
        printf ("IR status: %s\n", irMod->getStatusText().toRawUTF8());
        auto res = render (chain, guitar, L, R);
        if (! ok || ! irMod->getStatusText().contains ("samples")) { ++failures; printf ("IR load FAILED %s\n", err.toRawUTF8()); }
        check ("Valve Head -> IR Loader", res);
        writeWav (outDir.getChildFile ("ir_test.wav"), L, R);
    }

    // NAM models given on the command line
    for (int a = 2; a < argc; ++a)
    {
        const juce::File f (argv[a]);
        Chain chain;
        chain.prepare (kRate, kBlock);
        auto nam = ModuleFactory::create ("nam");
        juce::String err;
        if (! nam->loadFile (f, err)) { printf ("NAM %s: %s\n", f.getFileName().toRawUTF8(), err.toRawUTF8()); ++failures; continue; }
        chain.setModules ({ nam });
        for (int w = 0; w < 400 && nam->getStatusText().startsWith ("Loading"); ++w)
            juce::Thread::sleep (10);
        printf ("NAM status: %s\n", nam->getStatusText().toRawUTF8());
        auto res = render (chain, guitar, L, R);
        check ("NAM " + f.getFileName(), res, false);
        writeWav (outDir.getChildFile ("nam_" + f.getFileNameWithoutExtension() + ".wav"), L, R);
    }

    // threading stress test: edit the chain while an "audio thread" runs
    printf ("\n== threading stress test ==\n");
    {
        Chain chain;
        chain.prepare (kRate, kBlock);
        chain.setModules (Presets::factory()[2].build());
        std::atomic<bool> run { true };
        std::atomic<long> blocks { 0 };
        std::thread audio ([&]
        {
            std::vector<float> l (kBlock), r (kBlock);
            size_t pos = 0;
            while (run.load())
            {
                for (int i = 0; i < kBlock; ++i) l[(size_t) i] = guitar[(pos + (size_t) i) % guitar.size()];
                pos += kBlock;
                chain.process (l.data(), r.data(), kBlock);
                blocks.fetch_add (1);
            }
        });
        std::mt19937 rng (7);
        const auto& all = ModuleFactory::all();
        for (int it = 0; it < 3000; ++it)
        {
            const int n = (int) chain.getModules().size();
            switch (rng() % 4)
            {
                case 0: if (n < 12) chain.insert (ModuleFactory::create (all[rng() % all.size()].typeId), (int) (rng() % (unsigned long) (n + 1))); break;
                case 1: if (n > 0) chain.remove ((int) (rng() % (unsigned long) n)); break;
                case 2: if (n > 1) chain.move ((int) (rng() % (unsigned long) n), (int) (rng() % (unsigned long) n)); break;
                default:
                    for (auto& m : chain.getModules())
                    {
                        m->bypassed.store (rng() % 3 == 0);
                        if (m->numParams() > 0) m->set ((int) (rng() % (unsigned long) m->numParams()), (float) (rng() % 100));
                    }
            }
            if (it % 10 == 0) chain.collectGarbage();
            if (it % 50 == 0) std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        run.store (false);
        audio.join();
        chain.release();
        chain.collectGarbage();
        printf ("3000 edits while processing %ld blocks: OK (no crash)\n", blocks.load());
    }

    printf ("\n%s (%d failures)\n", failures == 0 ? "ALL PASSED" : "FAILURES", failures);
    return failures == 0 ? 0 : 1;
}
