// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Presets.h"
#include "modules/ModuleFactory.h"
#include "BinaryData.h"

namespace pg::Presets
{
namespace
{
    ModulePtr makeModule (const juce::var& m, const juce::String& presetName)
    {
        const auto type = m["type"].toString();
        auto mod = ModuleFactory::create (type);
        if (mod == nullptr)
        {
            std::fprintf (stderr, "PRESET ERROR: '%s' uses unknown pedal '%s'\n", presetName.toRawUTF8(), type.toRawUTF8());
            return nullptr;
        }
        if (auto* params = m["params"].getDynamicObject())
            for (auto& p : params->getProperties())
            {
                const int idx = mod->indexOf (p.name.toString());
                if (idx < 0)
                    std::fprintf (stderr, "PRESET ERROR: '%s': %s has no parameter '%s'\n",
                                  presetName.toRawUTF8(), type.toRawUTF8(), p.name.toString().toRawUTF8());
                else
                    mod->set (idx, (float) (double) p.value);
            }
        mod->bypassed.store ((bool) m.getProperty ("bypassed", false));
        return mod;
    }

    struct Library
    {
        juce::StringArray categories;
        std::vector<FactoryPreset> presets;
        juce::String source;
    };

    bool parse (const juce::String& text, const juce::String& source, Library& lib)
    {
        const auto root = juce::JSON::parse (text);
        if (root["format"].toString() != "toneical-factory" || ! root["presets"].isArray())
            return false;
        Library l;
        l.source = source;
        if (auto* cats = root["categories"].getArray())
            for (auto& c : *cats) l.categories.add (c.toString());
        for (auto& p : *root["presets"].getArray())
        {
            FactoryPreset fp;
            fp.name        = p["name"].toString();
            fp.category    = p["category"].toString();
            fp.description = p["description"].toString();
            fp.chain       = p["chain"];
            if (fp.name.isEmpty()) continue;
            l.categories.addIfNotAlreadyThere (fp.category);
            l.presets.push_back (std::move (fp));
        }
        if (l.presets.empty()) return false;
        lib = std::move (l);
        return true;
    }

    const Library& library()
    {
        static const Library lib = []
        {
            Library l;
            // 1) explicit override (tools/tests), 2) the installed data file - updated
            // without recompiling, 3) the copy compiled into the binary.
            juce::Array<juce::File> candidates;
            const auto env = juce::SystemStats::getEnvironmentVariable ("TONEICAL_FACTORY", {});
            if (env.isNotEmpty()) candidates.add (juce::File (env));
            candidates.add (homeFolder().getChildFile ("Factory").getChildFile ("factory.json"));
            for (auto& f : candidates)
                if (f.existsAsFile() && parse (f.loadFileAsString(), f.getFullPathName(), l))
                    return l;
            parse (juce::String::fromUTF8 (BinaryData::factory_json, BinaryData::factory_jsonSize), "built-in", l);
            return l;
        }();
        return lib;
    }
}

std::vector<ModulePtr> FactoryPreset::build() const
{
    std::vector<ModulePtr> mods;
    if (auto* arr = chain.getArray())
        for (auto& m : *arr)
            if (auto mod = makeModule (m, name))
                mods.push_back (std::move (mod));
    return mods;
}

const juce::StringArray& categories() { return library().categories; }
const std::vector<FactoryPreset>& factory() { return library().presets; }
juce::String factorySource() { return library().source; }

juce::File homeFolder()
{
    // The installer puts everything in ~/Toneical; TONEICAL_HOME overrides it.
    const auto env = juce::SystemStats::getEnvironmentVariable ("TONEICAL_HOME", {});
    if (env.isNotEmpty() && juce::File::isAbsolutePath (env))
        return juce::File (env);
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Toneical");
}

juce::File userFolder()
{
    auto dir = homeFolder().getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

bool save (const Chain& chain, const juce::File& file, juce::String& error)
{
    juce::ValueTree root ("ToneicalPreset");
    root.setProperty ("version", 1, nullptr);
    root.appendChild (chain.toValueTree(), nullptr);

    if (auto xml = root.createXml(); xml != nullptr && xml->writeTo (file))
        return true;

    error = "Could not write " + file.getFullPathName();
    return false;
}

bool load (Chain& chain, const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr) { error = "Not a valid preset file"; return false; }

    auto root = juce::ValueTree::fromXml (*xml);
    auto c = root.getChildWithName ("Chain");
    if (! root.hasType ("ToneicalPreset") || ! c.isValid()) { error = "Not a Toneical preset"; return false; }

    chain.fromValueTree (c);
    return true;
}

} // namespace pg::Presets
