// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "modules/Module.h"
#include "gui/LookAndFeel.h"
#include "gui/PedalArt.h"

namespace pg
{
//==============================================================================
// The bottom strip: the whole signal chain as small pedals, left (input) to right
// (output). Click a pedal to edit it in the big view, click its little footswitch to
// bypass, drag to reorder, drop gear from the browser to insert it.
class ChainStrip final : public juce::Component,
                         public juce::DragAndDropTarget,
                         private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void (const ModulePtr&)> onSelect;
        std::function<void (int from, int to)> onMove;
        std::function<void (const juce::String& typeId, int index)> onInsert;
        std::function<void (int index)> onRemove;
        std::function<void (int index)> onAddMenu;   // show the "add pedal" menu, inserting at index
    };

    explicit ChainStrip (Callbacks cb);
    ~ChainStrip() override;

    void setModules (std::vector<ModulePtr> mods);
    void setSelected (const ModulePtr& m);
    int preferredWidth() const;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

    bool isInterestedInDragSource (const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped (const SourceDetails&) override;

    static constexpr int pedalW = 108, pedalH = 132, gap = 34, margin = 74, outPlateW = 46;

private:
    class MiniPedal;
    void timerCallback() override;
    int dropIndexFor (int x, const juce::Component* dragged) const;
    int indexOf (const MiniPedal&) const;
    juce::Rectangle<int> addTile() const;
    juce::Rectangle<int> inPlate() const;
    juce::Rectangle<int> outPlate() const;

    Callbacks cb;
    std::vector<ModulePtr> modules;
    std::vector<std::unique_ptr<MiniPedal>> minis;
    ModulePtr selected;
    int markerX = -1;
};

} // namespace pg
