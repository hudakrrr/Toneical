// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ChainStrip.h"

namespace pg
{
//==============================================================================
class ChainStrip::MiniPedal final : public juce::Component,
                                    public juce::SettableTooltipClient
{
public:
    MiniPedal (ChainStrip& o, ModulePtr m) : module (std::move (m)), owner (o)
    {
        setTooltip (module->displayName() + (module->credit().isNotEmpty() ? "  -  " + module->credit() : juce::String())
                    + "\nClick: edit   |   footswitch: on/off   |   drag: move   |   right-click: more");
        setRepaintsOnMouseActivity (false);
    }

    ModulePtr module;
    bool selected = false;
    bool lastBypassed = false;
    float glow = 0.0f, glowShown = -1.0f, press = 0.0f, hover = 0.0f;

    PedalArt::Style style() const { return PedalArt::styleFor (module->typeId(), juce::Colour (module->colour())); }
    PedalArt::Mini geometry() const { return PedalArt::miniGeometry (getLocalBounds().toFloat().reduced (2.0f), style()); }
    juce::Rectangle<float> footswitch() const { return geometry().footswitch; }

    void paint (juce::Graphics& g) override
    {
        const auto st = style();
        const auto geo = geometry();
        // selection / hover: a soft amber light on the board under the pedal + a thin outline
        if (selected || hover > 0.01f)
        {
            const float a = selected ? 0.55f : 0.25f * hover;
            const auto spot = geo.body.expanded (10.0f, 6.0f).withTrimmedTop (geo.body.getHeight() * 0.55f);
            juce::ColourGradient lightG (Colours::accent.withAlpha (a), spot.getCentreX(), spot.getBottom() - 4,
                                         Colours::accent.withAlpha (0.0f), spot.getCentreX(), spot.getY(), true);
            g.setGradientFill (lightG);
            g.fillEllipse (spot);
        }
        PedalArt::drawMini (g, getLocalBounds().toFloat().reduced (2.0f), st, module->displayName(),
                            PedalArt::knobCount (module->typeId()), ! module->bypassed.load(), true, glow, press);
        if (selected || hover > 0.01f)
        {
            g.setColour (Colours::accent.withAlpha (selected ? 0.95f : 0.45f * hover));
            g.strokePath (PedalArt::bodyPath (geo.body.expanded (2.5f), st.shape), juce::PathStrokeType (selected ? 1.8f : 1.2f));
        }
    }

    void mouseEnter (const juce::MouseEvent&) override { hoverTarget = 1.0f; }
    void mouseExit (const juce::MouseEvent&) override  { hoverTarget = 0.0f; }

    // ~30 times a second from the strip: lights follow the signal, switches spring back
    void animate()
    {
        bool dirty = false;
        module->takeGlow (glow);
        if (std::abs (glow - glowShown) > 0.03f) { glowShown = glow; dirty = true; }
        if (press > 0.0f) { press = std::max (0.0f, press - 0.2f); dirty = true; }
        if (std::abs (hover - hoverTarget) > 0.01f) { hover += (hoverTarget - hover) * 0.35f; dirty = true; }
        if (dirty) repaint();
    }
    float hoverTarget = 0.0f;

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (footswitch().expanded (4).contains (e.position))
        {
            module->bypassed.store (! module->bypassed.load());
            press = 1.0f;
            repaint();
            return;
        }
        if (owner.cb.onSelect) owner.cb.onSelect (module);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || footswitch().expanded (4).contains (e.mouseDownPosition)) return;
        if (auto* dc = juce::DragAndDropContainer::findParentDragContainerFor (this))
        {
            dragging = true;
            dc->startDragging ("pedal", this,
                               PedalArt::dragImage (getLocalBounds(), getLocalBounds().toFloat().reduced (2.0f), style(),
                                                    module->displayName(), PedalArt::knobCount (module->typeId()),
                                                    ! module->bypassed.load()), false);   // false: drawn inside our window, so transparency works everywhere
        }
    }

private:
    void showMenu()
    {
        const int idx = owner.indexOf (*this);
        juce::PopupMenu m;
        m.addSectionHeader (module->displayName());
        m.addItem (1, module->bypassed.load() ? "Switch on" : "Bypass");
        m.addItem (2, "Move left", idx > 0);
        m.addItem (3, "Move right", idx + 1 < (int) owner.modules.size());
        m.addItem (4, "Insert pedal after this...");
        m.addSeparator();
        m.addItem (5, "Remove");
        juce::Component::SafePointer<MiniPedal> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe, idx] (int r)
        {
            if (safe == nullptr) return;
            auto& o = safe->owner;
            switch (r)
            {
                case 1: safe->module->bypassed.store (! safe->module->bypassed.load()); safe->repaint(); break;
                case 2: if (o.cb.onMove) o.cb.onMove (idx, idx - 1); break;
                case 3: if (o.cb.onMove) o.cb.onMove (idx, idx + 1); break;
                case 4: if (o.cb.onAddMenu) o.cb.onAddMenu (idx + 1); break;
                case 5: if (o.cb.onRemove) o.cb.onRemove (idx); break;
                default: break;
            }
        });
    }

    ChainStrip& owner;
    bool dragging = false;
};

//==============================================================================
ChainStrip::ChainStrip (Callbacks c) : cb (std::move (c))
{
    startTimerHz (30);   // LEDs follow the signal, bypass changes made in the big editor show up
}

ChainStrip::~ChainStrip() = default;

int ChainStrip::indexOf (const MiniPedal& p) const
{
    for (size_t i = 0; i < minis.size(); ++i)
        if (minis[i].get() == &p) return (int) i;
    return -1;
}

void ChainStrip::setModules (std::vector<ModulePtr> mods)
{
    modules = std::move (mods);
    minis.clear();
    for (auto& m : modules)
    {
        auto mp = std::make_unique<MiniPedal> (*this, m);
        mp->selected = (m == selected);
        mp->lastBypassed = m->bypassed.load();
        addAndMakeVisible (*mp);
        minis.push_back (std::move (mp));
    }
    setSize (juce::jmax (preferredWidth(), getParentWidth()), getHeight());
    resized();
    repaint();
}

void ChainStrip::setSelected (const ModulePtr& m)
{
    selected = m;
    for (auto& mp : minis) { mp->selected = (mp->module == m); mp->repaint(); }
}

int ChainStrip::preferredWidth() const
{
    return margin * 2 + (int) minis.size() * (pedalW + gap) + outPlateW + gap + pedalW;
}

juce::Rectangle<int> ChainStrip::outPlate() const
{
    const int x = margin + (int) minis.size() * (pedalW + gap);
    return { x, (getHeight() - pedalH) / 2 + pedalH - 56, outPlateW, 50 };
}

juce::Rectangle<int> ChainStrip::inPlate() const
{
    return { 8, (getHeight() - pedalH) / 2 + pedalH - 56, outPlateW, 50 };
}

juce::Rectangle<int> ChainStrip::addTile() const
{
    const int x = outPlate().getRight() + gap;
    return { x, (getHeight() - pedalH) / 2 + 10, pedalW - 30, pedalH - 20 };
}

void ChainStrip::resized()
{
    const int y = (getHeight() - pedalH) / 2;
    for (size_t i = 0; i < minis.size(); ++i)
        minis[i]->setBounds (margin + (int) i * (pedalW + gap), y, pedalW, pedalH);
}

void ChainStrip::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff111316));
    // board rails
    for (int k = 0; k < 3; ++k)
    {
        const int y = 22 + k * (getHeight() - 44) / 2;
        g.setColour (juce::Colour (0xff1a1c21)); g.fillRect (0, y, getWidth(), 12);
        g.setColour (juce::Colour (0xff23262d)); g.fillRect (0, y, getWidth(), 2);
    }

    // patch cables are drawn over the pedals (paintOverChildren); the jack plates here
    PedalArt::drawJackPlate (g, inPlate().toFloat(), "GUITAR");
    PedalArt::drawJackPlate (g, outPlate().toFloat(), "AMP / OUT");

    // add tile
    const auto add = addTile();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (add.toFloat(), 10.0f);
    juce::Path outline;
    outline.addRoundedRectangle (add.toFloat().reduced (1.0f), 10.0f);
    const float dash[] = { 5.0f, 4.0f };
    juce::PathStrokeType (1.4f).createDashedStroke (outline, outline, dash, 2);
    g.setColour (Colours::dim.withAlpha (0.7f));
    g.fillPath (outline);
    g.setFont (uiFont (30.0f));
    g.drawText ("+", add.withTrimmedBottom (26), juce::Justification::centred);
    g.setFont (uiFont (10.5f, true));
    g.drawText ("ADD / DROP", add.withTrimmedTop (50), juce::Justification::centred);
}

void ChainStrip::paintOverChildren (juce::Graphics& g)
{
    // signal flow: guitar plate -> each pedal (in on the left, out on the right) -> output plate
    const float floorY = (float) getHeight() - 10.0f;
    struct Jack { juce::Point<float> p; bool right; };
    std::vector<std::pair<Jack, Jack>> links;
    const auto ip = inPlate().toFloat(), op = outPlate().toFloat();
    Jack prev { { ip.getRight(), ip.getY() + 16.0f }, true };
    for (auto& mp : minis)
    {
        const auto geo = mp->geometry();
        const auto off = mp->getPosition().toFloat();
        links.push_back ({ prev, { { off.x + geo.body.getX(), off.y + geo.jackY }, false } });
        prev = { { off.x + geo.body.getRight(), off.y + geo.jackY }, true };
    }
    links.push_back ({ prev, { { op.getX(), op.getY() + 16.0f }, false } });

    for (auto& [a, b] : links)
        PedalArt::drawCable (g, a.p, a.right, b.p, b.right, floorY);
    for (auto& [a, b] : links)
    {
        PedalArt::drawPlug (g, a.p, a.right);
        PedalArt::drawPlug (g, b.p, b.right);
    }

    if (markerX >= 0)
    {
        g.setColour (Colours::accent);
        g.fillRoundedRectangle ((float) markerX - 2.0f, 8.0f, 4.0f, (float) getHeight() - 16.0f, 2.0f);
    }
}

void ChainStrip::mouseUp (const juce::MouseEvent& e)
{
    if (addTile().contains (e.getPosition()) && cb.onAddMenu)
        cb.onAddMenu ((int) modules.size());
}

void ChainStrip::timerCallback()
{
    for (auto& mp : minis)
    {
        const bool b = mp->module->bypassed.load();
        if (b != mp->lastBypassed) { mp->lastBypassed = b; mp->repaint(); }
        mp->animate();
    }
}

int ChainStrip::dropIndexFor (int x, const juce::Component* dragged) const
{
    int idx = 0;
    for (auto& mp : minis)
        if (mp.get() != dragged && mp->getBounds().getCentreX() < x) ++idx;
    return idx;
}

bool ChainStrip::isInterestedInDragSource (const SourceDetails& d)
{
    const auto s = d.description.toString();
    return s == "pedal" || s.startsWith ("gear:");
}

void ChainStrip::itemDragMove (const SourceDetails& d)
{
    const auto* dragged = d.description.toString() == "pedal" ? d.sourceComponent.get() : nullptr;
    const int idx = dropIndexFor (d.localPosition.x, dragged);
    std::vector<MiniPedal*> others;
    for (auto& mp : minis) if (mp.get() != dragged) others.push_back (mp.get());
    if (others.empty()) markerX = margin - gap / 2 + 4;
    else if (idx >= (int) others.size()) markerX = others.back()->getRight() + gap / 2;
    else markerX = others[(size_t) idx]->getX() - gap / 2;
    repaint();
}

void ChainStrip::itemDragExit (const SourceDetails&) { markerX = -1; repaint(); }

void ChainStrip::itemDropped (const SourceDetails& d)
{
    markerX = -1;
    repaint();
    const auto s = d.description.toString();
    if (s.startsWith ("gear:"))
    {
        if (cb.onInsert) cb.onInsert (s.fromFirstOccurrenceOf ("gear:", false, false), dropIndexFor (d.localPosition.x, nullptr));
        return;
    }
    for (size_t i = 0; i < minis.size(); ++i)
        if (minis[i].get() == d.sourceComponent.get())
        {
            const int to = dropIndexFor (d.localPosition.x, minis[i].get());
            if (to != (int) i && cb.onMove) cb.onMove ((int) i, to);
            return;
        }
}

} // namespace pg
