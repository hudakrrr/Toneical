// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Chain.h"

namespace pg::Presets
{
// Factory presets live in presets/factory.json. The installer copies it to
// ~/Toneical/Factory/factory.json, which is loaded at startup (so preset updates need no
// rebuild); a copy compiled into the binary is the fallback.
struct FactoryPreset
{
    juce::String name;
    juce::String category;      // "Clean", "Rhythm", "Lead", "Ambient & FX", "Utility"
    juce::String description;   // shown when the preset is picked
    juce::var chain;            // [{ "type", "bypassed", "params": { id: value } }]
    std::vector<ModulePtr> build() const;
};

const std::vector<FactoryPreset>& factory();
const juce::StringArray& categories();         // menu order
juce::String factorySource();                   // where the factory presets were loaded from

juce::File homeFolder();                        // ~/Toneical, Windows: Documents\Toneical (or $TONEICAL_HOME)
juce::File userFolder();                        // ~/Toneical/Presets (created on demand)
constexpr const char* fileExtension = ".toneical";

bool save (const Chain& chain, const juce::File& file, juce::String& error);
bool load (Chain& chain, const juce::File& file, juce::String& error);

} // namespace pg::Presets
