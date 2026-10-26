// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ModuleFactory.h"
#include "Dynamics.h"
#include "Drives.h"
#include "CapturedPedals.h"
#include "Amp.h"
#include "Effects.h"
#include "NamModule.h"
#include "CapturedAmp.h"
#include "BinaryData.h"

namespace pg
{
void CabSim::getIr (int index, const void*& data, size_t& size)
{
    switch (index)
    {
        case 0:  data = BinaryData::cab_modern_4x12_wav;  size = BinaryData::cab_modern_4x12_wavSize;  break;
        case 1:  data = BinaryData::cab_british_4x12_wav; size = BinaryData::cab_british_4x12_wavSize; break;
        case 2:  data = BinaryData::cab_tight_4x12_wav;   size = BinaryData::cab_tight_4x12_wavSize;   break;
        case 3:  data = BinaryData::cab_boutique_wav;     size = BinaryData::cab_boutique_wavSize;     break;
        default: data = BinaryData::cab_small_combo_wav;  size = BinaryData::cab_small_combo_wavSize;  break;
    }
}

namespace
{
    template <typename T> ModulePtr make() { return std::make_shared<T>(); }

    struct Entry { ModuleInfo info; ModulePtr (*create)(); };

    const std::vector<Entry>& entries()
    {
        static const std::vector<Entry> list = [] {
            std::vector<Entry> e;
            auto add = [&] (ModulePtr (*fn)())
            {
                auto m = fn();
                e.push_back ({ { m->typeId(), m->displayName(), m->category(), m->credit(), m->colour() }, fn });
            };
            add (&make<NoiseGate>);
            add (&make<Compressor>);
            add (&make<CleanBoost>);
            for (auto fn : capturedPedalFactories()) add (fn);
            add (&make<CapturedAmp>);
            add (&make<TubeAmp>);
            add (&make<NamModule>);
            add (&make<CabSim>);
            add (&make<IrLoader>);
            add (&make<EqModule>);
            add (&make<Chorus>);
            add (&make<Phaser>);
            add (&make<Tremolo>);
            add (&make<Delay>);
            add (&make<Reverb>);
            return e;
        }();
        return list;
    }
}

const std::vector<ModuleInfo>& ModuleFactory::all()
{
    static const std::vector<ModuleInfo> infos = [] {
        std::vector<ModuleInfo> v;
        for (auto& e : entries()) v.push_back (e.info);
        return v;
    }();
    return infos;
}

ModulePtr ModuleFactory::create (const juce::String& typeId)
{
    for (auto& e : entries())
        if (e.info.typeId == typeId)
            return e.create();
    return nullptr;
}

} // namespace pg
