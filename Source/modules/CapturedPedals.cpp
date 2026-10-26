// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "CapturedPedals.h"
#include "BinaryData.h"
#include "dsp/DspHelpers.h"
#include "dsp/LstmModel.h"
#include "dsp/RateBridge.h"
#include "NAM/dsp.h"
#include "NAM/get_dsp.h"
#include "NAM/activations.h"
#include <map>
#include <mutex>
#include <utility>

namespace pg
{
namespace
{
    //==========================================================================
    // capture files (embedded) - parsed once per process
    const nlohmann::json& parsed (const char* data, int size)
    {
        static std::mutex m;
        static std::map<const char*, std::unique_ptr<nlohmann::json>> cache;
        std::lock_guard<std::mutex> lock (m);
        auto& slot = cache[data];
        if (slot == nullptr) slot = std::make_unique<nlohmann::json> (nlohmann::json::parse (data, data + size));
        return *slot;
    }

    //==========================================================================
    struct Engine
    {
        virtual ~Engine() = default;
        virtual void reset (double sampleRate, int maxBlock) = 0;
        virtual void process (float* x, int n, float cond) noexcept = 0;
        virtual bool conditioned() const { return false; }
    };

    struct NamEngine final : Engine
    {
        explicit NamEngine (const nlohmann::json& j) : dsp (nam::get_dsp (j)) {}
        void reset (double sr, int maxBlock) override
        {
            out.assign ((size_t) maxBlock, 0.0f);
            if (dsp) dsp->Reset (sr, maxBlock);   // pre-warms
        }
        void process (float* x, int n, float) noexcept override
        {
            if (! dsp) return;
            float* in[1] = { x };
            float* o[1] = { out.data() };
            dsp->process (in, o, n);
            for (int i = 0; i < n; ++i) x[i] = std::isfinite (out[(size_t) i]) ? out[(size_t) i] : 0.0f;
        }
        std::unique_ptr<nam::DSP> dsp;
        std::vector<float> out;
    };

    struct LstmEngine final : Engine
    {
        explicit LstmEngine (const nlohmann::json& j)
        {
            const auto& md = j.at ("model_data");
            const auto& sd = j.at ("state_dict");
            std::vector<float> lw = sd.at ("lin.weight").at (0).get<std::vector<float>>();
            ok = net.load (md.at ("input_size").get<int>(), md.at ("hidden_size").get<int>(), md.value ("skip", 1) != 0,
                           sd.at ("rec.weight_ih_l0").get<std::vector<std::vector<float>>>(),
                           sd.at ("rec.weight_hh_l0").get<std::vector<std::vector<float>>>(),
                           sd.at ("rec.bias_ih_l0").get<std::vector<float>>(),
                           sd.at ("rec.bias_hh_l0").get<std::vector<float>>(),
                           lw, sd.at ("lin.bias").at (0).get<float>());
        }
        void reset (double sr, int maxBlock) override
        {
            bridge.prepare (sr, 44100.0, maxBlock);   // GuitarML captures are trained at 44.1 kHz
            net.reset();
        }
        void process (float* x, int n, float cond) noexcept override
        {
            if (! ok) return;
            bridge.process (x, x, n, [this, cond] (float* m, int k) { net.process (m, k, cond); });
        }
        bool conditioned() const override { return net.inputSize() == 2; }
        LstmModel net;
        RateBridge bridge;
        bool ok = false;
    };

    std::unique_ptr<Engine> makeEngine (const char* data, int size)
    {
        const auto& j = parsed (data, size);
        if (j.contains ("state_dict")) return std::make_unique<LstmEngine> (j);
        return std::make_unique<NamEngine> (j);
    }

    //==========================================================================
    enum class Tone { none, tilt, treble, filter, bright };

    struct Snap    { const char* data; int size; float knob; };   // knob = Drive position it was captured at
    struct Variant { const char* name; std::vector<Snap> snaps; float trimDb = 0.0f; };   // trim: Level 0 dB = as loud as bypass

    struct Def
    {
        const char* typeId; const char* name; const char* credit; juce::uint32 colour;
        const char* driveId; const char* driveName; float driveDefault;
        const char* toneId;  const char* toneName;  Tone tone;
        const char* levelName;
        const char* variantName; std::vector<Variant> variants;
        float inGainDb;      // fixed gain into the capture (GuitarML's convention for its models)
        float dbPerStep;     // Drive: dB of extra input per knob step (captures without a Drive input)
        float unusedTrim;    // (per-variant trims live in Variant)
        float compPerDb;     // output compensation per dB of extra drive
    };

    #define BD(n) BinaryData::n, BinaryData::n##Size

    // trimDb / compPerDb measured with tools/level_pedals.py (real DI guitar)
    const std::vector<Def>& defs()
    {
        static const std::vector<Def> d = {
            { "ts808", "Green Screamer", "Neural capture of an Ibanez TS9 Tube Screamer, including its Drive knob (GuitarML Proteus library)", 0xff2e9b4f,
              "drive", "Drive", 3.0f, "tone", "Tone", Tone::tilt, "Level", nullptr,
              { { "", { { BD (ts9_drive_json), -1 }  }, -11.3f } }, 3.5f, 0.0f, 0.0f, 0.00f },
            { "klon", "Clone Kentaur", "Neural captures of a Klon Centaur clone at gain 6 and full gain (Keith B, NAM)", 0xffc8a23c,
              "gain", "Gain", 5.0f, "treble", "Treble", Tone::treble, "Output", nullptr,
              { { "", { { BD (klon_g6_nam), 6.0f }, { BD (klon_max_nam), 10.0f }  }, -10.3f } }, 0.0f, 1.5f, 0.0f, 0.39f },
            { "sd1", "Baby SD-1", "Neural capture of a Boss SD-1 Super Overdrive (Phillipe P, NAM)", 0xffe8d42a,
              "drive", "Drive", 5.0f, "tone", "Tone", Tone::tilt, "Level", nullptr,
              { { "", { { BD (sd1_nam), 5.0f }  }, -0.5f } }, 0.0f, 1.5f, 0.0f, 0.49f },
            { "ds1", "Baby DS-1", "Neural captures of a Boss DS-1 Distortion at dist 6 and max (Keith B, NAM)", 0xffe8761c,
              "dist", "Dist", 6.0f, "tone", "Tone", Tone::tilt, "Level", nullptr,
              { { "", { { BD (ds1_g6_nam), 6.0f }, { BD (ds1_max_nam), 10.0f }  }, -13.6f } }, 0.0f, 1.5f, 0.0f, 0.26f },
            { "os2", "Baby OS-2", "Neural capture of a Boss OS-2 OverDrive/Distortion (Helga B, NAM)", 0xffef9a1e,
              "drive", "Drive", 5.0f, "tone", "Tone", Tone::tilt, "Level", nullptr,
              { { "", { { BD (os2_nam), 5.0f }  }, -11.7f } }, 0.0f, 1.5f, 0.0f, 0.12f },
            { "mt2", "Baby MT-2", "Neural capture of a Boss MT-2 Metal Zone at high gain (GuitarML Proteus library)", 0xff2c2d31,
              "dist", "Dist", 5.0f, "tone", "Tone", Tone::tilt, "Level", nullptr,
              { { "", { { BD (mt2_json), 5.0f }  }, -16.4f } }, 1.9f, 1.2f, 0.0f, 0.22f },
            { "hm2", "Baby HM-2", "Neural captures of a Boss HM-2 Heavy Metal at four settings (Peter N, NAM)", 0xff2b2b2e,
              "dist", "Dist", 5.0f, nullptr, nullptr, Tone::none, "Level", "Setting",
              { { "Swede: everything on 10",     { { BD (hm2_swede_nam),   5.0f } }, -16.8f },
                { "Low 10, High 10, Dist 0",     { { BD (hm2_l10h10_nam),  5.0f } }, -6.4f },
                { "Low 10, High 8, Dist 2",      { { BD (hm2_l10h8d2_nam), 5.0f } }, -10.5f },
                { "Low 8, High 4, Dist 0",       { { BD (hm2_l8h4_nam),    5.0f } }, -4.5f } }, 0.0f, 1.5f, 0.0f, 0.05f },
            { "rat", "Rodent", "Neural capture of a ProCo RAT at high gain (GuitarML Proteus library)", 0xff26272b,
              "dist", "Distortion", 5.0f, "filter", "Filter", Tone::filter, "Volume", nullptr,
              { { "", { { BD (rat_json), 5.0f }  }, -21.2f } }, 1.9f, 1.2f, 0.0f, 0.10f },
            { "muff", "Little Big Fluff", "Neural capture of an Electro-Harmonix Little Big Muff (GuitarML Proteus library)", 0xffb8b8b8,
              "sustain", "Sustain", 5.0f, "tone", "Tone", Tone::tilt, "Volume", nullptr,
              { { "", { { BD (bigmuff_json), 5.0f }  }, -18.3f } }, 1.9f, 1.2f, 0.0f, 0.06f },
            { "mxr78", "XD '78 Distortion", "Neural capture of an MXR '78 Custom Badass Distortion, including its Distortion knob (GuitarML Proteus library)", 0xffe9c11a,
              "dist", "Distortion", 5.0f, nullptr, nullptr, Tone::none, "Output", nullptr,
              { { "", { { BD (mxr78_json), -1 }  }, -13.1f } }, 3.5f, 0.0f, 0.0f, 0.00f },
            { "m77", "MXD Bad Mod 77", "Neural captures of an MXR M77 Custom Badass Modified O.D., bump off/on (Tudor N, NAM)", 0xff1f2a44,
              "gain", "Gain", 2.0f, "tone", "Tone", Tone::tilt, "Output", "Bump",
              { { "Bump off", { { BD (m77_nam),      0.0f } }, -0.2f },
                { "Bump on",  { { BD (m77_bump_nam), 0.0f } }, -3.2f } }, 0.0f, 1.2f, 0.0f, 0.40f },
            { "bb", "Exotic BB Preamp", "Neural capture of a Xotic BB Preamp, including its Gain knob (GuitarML Proteus library)", 0xffc9ccd1,
              "gain", "Gain", 5.0f, "tone", "Tone", Tone::tilt, "Volume", nullptr,
              { { "", { { BD (xoticbb_json), -1 }  }, -14.0f } }, 3.5f, 0.0f, 0.0f, 0.00f },
            { "protein", "Proteen Dual Drive", "Neural captures of both sides of a Browne Protein dual overdrive, including the Drive knobs (GuitarML Proteus library)", 0xff3b6ea8,
              "drive", "Drive", 5.0f, "tone", "Tone", Tone::tilt, "Volume", "Side",
              { { "Blue (lower gain)",  { { BD (protein_blue_json),  -1 } }, -10.6f },
                { "Green (more gain)",  { { BD (protein_green_json), -1 } }, -13.3f } }, 3.5f, 0.0f, 0.0f, 0.00f },
            { "precision", "Precise Drive", "Neural capture of a Horizon Devices Precision Drive, drive at 0 (Phillipe P, NAM)", 0xff17181b,
              "drive", "Drive", 0.0f, "bright", "Bright", Tone::bright, "Volume", nullptr,
              { { "", { { BD (precision_nam), 0.0f }  }, -5.5f } }, 0.0f, 1.2f, 0.0f, 0.29f },
            { "grind", "Fortune Grind", "Neural capture of a Fortin Grind boost at 12 o'clock (Tudor N, NAM)", 0xff7d1d1d,
              "grind", "Grind", 5.0f, nullptr, nullptr, Tone::none, "Level", nullptr,
              { { "", { { BD (grind_nam), 5.0f }  }, -0.7f } }, 0.0f, 1.2f, 0.0f, 0.68f },
            { "sd805", "Seemore 805", "Neural capture of a Seymour Duncan 805 Overdrive (Tim R, NAM)", 0xff1f7a6a,
              "gain", "Gain", 5.0f, "tone", "Tone", Tone::tilt, "Volume", nullptr,
              { { "", { { BD (sd805_nam), 5.0f }  }, -1.5f } }, 0.0f, 1.5f, 0.0f, 0.69f },
        };
        return d;
    }
    #undef BD

    //==========================================================================
    std::vector<ParamSpec> specsFor (const Def& d)
    {
        std::vector<ParamSpec> s;
        if (d.variantName != nullptr)
        {
            juce::StringArray names;
            for (auto& v : d.variants) names.add (v.name);
            s.push_back (ParamSpec::choice ("setting", d.variantName, names, 0));
        }
        if (d.driveId) s.push_back (ParamSpec::knob (d.driveId, d.driveName, 0, 10, d.driveDefault));
        if (d.toneId)  s.push_back (ParamSpec::knob (d.toneId, d.toneName, 0, 10, 5));
        s.push_back (ParamSpec::knob ("level", d.levelName, -24, 18, 0, " dB"));
        return s;
    }
}

//==============================================================================
class CapturedPedal final : public Module
{
public:
    explicit CapturedPedal (const Def& def) : Module (specsFor (def)), d (def)
    {
        int i = 0;
        if (d.variantName) pVariant = i++;
        if (d.driveId) pDrive = i++;
        if (d.toneId)  pTone = i++;
        pLevel = i;
        nam::activations::Activation::enable_fast_tanh();
    }

    juce::String typeId() const override      { return d.typeId; }
    juce::String displayName() const override { return d.name; }
    juce::String category() const override    { return "Drive"; }
    juce::uint32 colour() const override      { return d.colour; }
    juce::String credit() const override      { return juce::String (d.credit) + " - GPL v3"; }

protected:
    float driveKnob() const { return pDrive >= 0 ? get (pDrive) : 5.0f; }

    // which capture plays now: the variant (choice) and, within it, the snapshot nearest the Drive knob
    int wantedEngine() const
    {
        const int v = pVariant >= 0 ? juce::jlimit (0, (int) d.variants.size() - 1, (int) get (pVariant)) : 0;
        const auto& snaps = d.variants[(size_t) v].snaps;
        int best = 0; float bestDist = 1e9f;
        for (size_t s = 0; s < snaps.size(); ++s)
        {
            const float dist = snaps[s].knob < 0 ? 0.0f : std::abs (snaps[s].knob - driveKnob());
            if (dist < bestDist) { bestDist = dist; best = (int) s; }
        }
        return firstIndex[(size_t) v] + best;
    }

    void ensureEngines()
    {
        if (! engines.empty()) return;
        for (auto& v : d.variants)
        {
            firstIndex.push_back ((int) engines.size());
            for (auto& s : v.snaps)
            {
                engines.push_back (makeEngine (s.data, s.size));
                snapOf.push_back (&s);
                trimOf.push_back (v.trimDb);
            }
        }
    }

    void switchTo (int e)
    {
        if (e == active.load()) return;
        engines[(size_t) e]->reset (sr, maxBlock);           // warm up the new one first...
        const juce::SpinLock::ScopedLockType sl (lock);       // ...then hand it to the audio thread
        active.store (e);
    }

    void parameterChanged (int index) override
    {
        if (! prepared || (index != pVariant && index != pDrive)) return;
        switchTo (wantedEngine());
    }

    void prepareDsp (double sampleRate, int maxBlockSize) override
    {
        ensureEngines();
        const juce::SpinLock::ScopedLockType sl (lock);
        const int e = wantedEngine();
        engines[(size_t) e]->reset (sampleRate, maxBlockSize);
        active.store (e);
        dcHp.highPass (sampleRate, 15.0, 0.7);
        toneKey = -1;
        prepared = true;
    }

    void resetDsp() noexcept override { dcHp.reset(); tone1.reset(); tone2.reset(); }

    void updateTone()
    {
        const float t = pTone >= 0 ? get (pTone) : 5.0f;
        const double u = (t - 5.0) / 5.0;        // -1 .. +1
        switch (d.tone)
        {
            case Tone::tilt:   tone1.highShelf (sr, 1600.0, 7.0 * u, 0.8); tone2.lowShelf (sr, 250.0, -2.5 * u, 0.8); break;
            case Tone::treble: tone1.highShelf (sr, 2200.0, 6.0 * u, 0.8); tone2 = Biquad(); break;
            case Tone::bright: tone1.highShelf (sr, 3200.0, 6.0 * u, 0.8); tone2 = Biquad(); break;
            case Tone::filter:
            {
                // like the RAT's Filter: turning it up makes it darker
                const double f = std::exp (std::log (18000.0) + (std::log (900.0) - std::log (18000.0)) * (t / 10.0));
                tone1.lowPass (sr, f, 0.6); tone2 = Biquad(); break;
            }
            case Tone::none:   tone1 = Biquad(); tone2 = Biquad(); break;
        }
    }

    void processDsp (float* L, float* R, int n, bool& stereo) noexcept override
    {
        const juce::SpinLock::ScopedTryLockType sl (lock);
        if (! sl.isLocked()) return;
        const int e = active.load (std::memory_order_relaxed);
        if (e < 0) return;
        auto& eng = *engines[(size_t) e];
        const Snap& snap = *snapOf[(size_t) e];
        const float trim = trimOf[(size_t) e];

        const int tk = (int) ((pTone >= 0 ? get (pTone) : 5.0f) * 1000.0f);
        if (tk != toneKey) { updateTone(); toneKey = tk; }

        // drive: the learned knob when the capture has one, otherwise how hard we hit the capture
        const float drv = driveKnob();
        float extraDb = 0.0f, cond = 0.0f;
        if (eng.conditioned()) cond = drv / 10.0f;
        else                   extraDb = (drv - snap.knob) * d.dbPerStep;
        const float inG  = dbToGain (d.inGainDb + extraDb);
        const float outG = dbToGain (trim - d.compPerDb * extraDb + get (pLevel));

        const float i0 = lastIn, iStep = (inG - i0) / (float) n;
        for (int i = 0; i < n; ++i)
            L[i] = (stereo ? 0.5f * (L[i] + R[i]) : L[i]) * (i0 + iStep * (float) (i + 1));
        lastIn = inG;

        eng.process (L, n, cond);

        const float o0 = lastOut, oStep = (outG - o0) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float y = tone2.process (tone1.process (dcHp.process (L[i])));
            L[i] = (std::isfinite (y) ? y : 0.0f) * (o0 + oStep * (float) (i + 1));
        }
        lastOut = outG;
        stereo = false;
    }

private:
    const Def& d;
    int pVariant = -1, pDrive = -1, pTone = -1, pLevel = 0;
    std::vector<std::unique_ptr<Engine>> engines;
    std::vector<const Snap*> snapOf;
    std::vector<float> trimOf;
    std::vector<int> firstIndex;
    std::atomic<int> active { -1 };
    juce::SpinLock lock;
    bool prepared = false;
    Biquad dcHp, tone1, tone2;
    int toneKey = -1;
    float lastIn = 1.0f, lastOut = 1.0f;
};

namespace
{
    template <size_t I> ModulePtr makePedal() { return std::make_shared<CapturedPedal> (defs()[I]); }

    template <size_t... I>
    std::vector<ModulePtr (*)()> factories (std::index_sequence<I...>) { return { &makePedal<I>... }; }
}

std::vector<ModulePtr (*)()> capturedPedalFactories()
{
    constexpr size_t count = 16;
    jassert (defs().size() == count);
    return factories (std::make_index_sequence<count>());
}

} // namespace pg
