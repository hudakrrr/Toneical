// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "modules/ModuleFactory.h"
#include "gui/LookAndFeel.h"
#include "gui/PedalArt.h"

namespace pg
{
//==============================================================================
// Right-hand gear browser (AmpliTube style): category tabs, a search box and a grid
// of gear tiles. Drag a tile onto the chain strip to insert it there; double-click
// a tile to add it after the selected pedal.
class GearBrowser final : public juce::Component
{
public:
    std::function<void (const juce::String& typeId)> onDoubleClick;

    GearBrowser();
    ~GearBrowser() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class Tile;
    void rebuild();

    juce::TextEditor search;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::String category { "All" };
    juce::Viewport viewport;
    juce::Component grid;
    std::vector<std::unique_ptr<Tile>> tiles;
};

} // namespace pg
