// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "GearBrowser.h"

namespace pg
{
//==============================================================================
class GearBrowser::Tile final : public juce::Component,
                                public juce::SettableTooltipClient
{
public:
    Tile (GearBrowser& o, ModuleInfo i) : owner (o), info (std::move (i))
    {
        setTooltip (info.name + (info.credit.isNotEmpty() ? "\n" + info.credit : juce::String())
                    + "\nDrag onto the pedalboard, or double-click to add it after the selected pedal");
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (isMouseOver() ? Colours::panel.brighter (0.12f) : Colours::panel);
        g.fillRoundedRectangle (r, 7.0f);
        if (isMouseOver()) { g.setColour (Colours::accent.withAlpha (0.7f)); g.drawRoundedRectangle (r, 7.0f, 1.2f); }

        // the gear itself, drawn the way it looks on the board
        const auto icon = iconArea();
        PedalArt::drawMini (g, icon, PedalArt::styleFor (info.typeId, juce::Colour (info.colour)), info.name,
                            PedalArt::knobCount (info.typeId), true, false, 0.3f, 0.0f, isMouseOver() ? 3.0f : 0.0f);

        g.setColour (Colours::text);
        g.setFont (uiFont (11.5f, true));
        g.drawFittedText (info.name, r.withTrimmedTop (r.getHeight() - 22.0f).toNearestInt(), juce::Justification::centred, 1, 0.75f);
    }

    juce::Rectangle<float> iconArea() const
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        return r.removeFromTop (r.getHeight() - 22.0f).reduced (r.getWidth() * 0.18f, 6.0f);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (started || e.getDistanceFromDragStart() < 5) return;
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            started = true;
            dc->startDragging ("gear:" + info.typeId, this,
                               PedalArt::dragImage (getLocalBounds(), iconArea(), PedalArt::styleFor (info.typeId, juce::Colour (info.colour)),
                                                    info.name, PedalArt::knobCount (info.typeId), true), false);
        }
    }
    void mouseUp (const juce::MouseEvent&) override { started = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override { if (owner.onDoubleClick) owner.onDoubleClick (info.typeId); }

    GearBrowser& owner;
    ModuleInfo info;
    bool started = false;
};

//==============================================================================
GearBrowser::GearBrowser()
{
    search.setTextToShowWhenEmpty ("Search gear (name, \"Boss\", \"fuzz\"...)", Colours::dim);
    search.setColour (juce::TextEditor::backgroundColourId, Colours::bg);
    search.setColour (juce::TextEditor::outlineColourId, Colours::panelEdge);
    search.setColour (juce::TextEditor::focusedOutlineColourId, Colours::accent);
    search.setColour (juce::TextEditor::textColourId, Colours::text);
    search.setFont (uiFont (13.0f));
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible (search);

    juce::StringArray cats { "All" };
    for (auto& i : ModuleFactory::all()) cats.addIfNotAlreadyThere (i.category);
    for (auto& c : cats)
    {
        auto* b = tabs.add (new juce::TextButton (c));
        b->setClickingTogglesState (false);
        b->setToggleState (c == category, juce::dontSendNotification);
        b->onClick = [this, b, c]
        {
            category = c;
            for (auto* t : tabs) t->setToggleState (t == b, juce::dontSendNotification);
            rebuild();
        };
        addAndMakeVisible (b);
    }

    viewport.setViewedComponent (&grid, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    rebuild();
}

GearBrowser::~GearBrowser() = default;

void GearBrowser::rebuild()
{
    tiles.clear();
    const auto q = search.getText().trim();
    for (auto& info : ModuleFactory::all())
    {
        if (category != "All" && info.category != category) continue;
        if (q.isNotEmpty() && ! (info.name.containsIgnoreCase (q) || info.category.containsIgnoreCase (q) || info.credit.containsIgnoreCase (q)))
            continue;
        auto t = std::make_unique<Tile> (*this, info);
        grid.addAndMakeVisible (*t);
        tiles.push_back (std::move (t));
    }
    resized();
}

void GearBrowser::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel.darker (0.25f));
    g.setColour (Colours::panelEdge);
    g.fillRect (0, 0, 1, getHeight());
    g.setColour (Colours::accent);
    g.setFont (uiFont (13.0f, true));
    g.drawText ("GEAR", 12, 6, 100, 20, juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (uiFont (10.5f));
    g.drawText ("drag onto the board", getWidth() - 132, 6, 120, 20, juce::Justification::centredRight);
}

void GearBrowser::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (22);
    search.setBounds (r.removeFromTop (28));
    r.removeFromTop (6);

    // tabs: wrap into rows of 3
    const int perRow = 3, tabH = 24;
    const int rows = (tabs.size() + perRow - 1) / perRow;
    auto tabArea = r.removeFromTop (rows * (tabH + 4));
    for (int i = 0; i < tabs.size(); ++i)
    {
        const int row = i / perRow, col = i % perRow, w = tabArea.getWidth() / perRow;
        tabs[i]->setBounds (tabArea.getX() + col * w + 2, tabArea.getY() + row * (tabH + 4), w - 4, tabH);
    }
    r.removeFromTop (4);
    viewport.setBounds (r);

    const int cols = 2, tileW = (r.getWidth() - 10) / cols, tileH = 110;
    for (size_t i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds ((int) (i % cols) * tileW, (int) (i / cols) * tileH, tileW, tileH);
    grid.setSize (r.getWidth() - 10, (int) ((tiles.size() + cols - 1) / cols) * tileH + 4);
}

} // namespace pg
