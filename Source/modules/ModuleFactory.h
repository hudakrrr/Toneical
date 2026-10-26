// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include "Module.h"

namespace pg
{
struct ModuleInfo
{
    juce::String typeId, name, category, credit;
    juce::uint32 colour = 0xff444444;
};

struct ModuleFactory
{
    // All available module types, in the order they appear in the "Add" menu.
    static const std::vector<ModuleInfo>& all();

    // Returns nullptr for unknown ids.
    static ModulePtr create (const juce::String& typeId);
};

} // namespace pg
