// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pg
{
namespace Colours
{
    const juce::Colour bg        { 0xff14161b };
    const juce::Colour panel     { 0xff1d2027 };
    const juce::Colour panelEdge { 0xff2a2e37 };
    const juce::Colour text      { 0xffe8e6e1 };
    const juce::Colour dim       { 0xff8b909c };
    const juce::Colour accent    { 0xfff2a33a }; // warm amber, like a lit valve
    const juce::Colour good      { 0xff4cd07d };
    const juce::Colour warn      { 0xfff25c4b };
}

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class PedalLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PedalLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, Colours::bg);
        setColour (juce::Label::textColourId, Colours::text);
        setColour (juce::ComboBox::backgroundColourId, Colours::panel);
        setColour (juce::ComboBox::outlineColourId, Colours::panelEdge);
        setColour (juce::ComboBox::textColourId, Colours::text);
        setColour (juce::ComboBox::arrowColourId, Colours::dim);
        setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
        setColour (juce::PopupMenu::textColourId, Colours::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::accent.withAlpha (0.25f));
        setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
        setColour (juce::PopupMenu::headerTextColourId, Colours::accent);
        setColour (juce::TextButton::buttonColourId, Colours::panel);
        setColour (juce::TextButton::buttonOnColourId, Colours::accent.withAlpha (0.8f));
        setColour (juce::TextButton::textColourOffId, Colours::text);
        setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        setColour (juce::Slider::textBoxTextColourId, Colours::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::TooltipWindow::backgroundColourId, Colours::panel);
        setColour (juce::TooltipWindow::textColourId, Colours::text);
        setColour (juce::ScrollBar::thumbColourId, Colours::dim.withAlpha (0.5f));
    }

    // Knob caps: the style comes from the slider's "knob" property (PedalArt::KnobStyle):
    // 0 dome, 1 chicken-head, 2 skirted, 3 small ribbed, 4 knurled aluminium.
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
        const float r  = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c   = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto surface = s.findColour (juce::Slider::rotarySliderOutlineColourId);
        const auto mark = surface.getPerceivedBrightness() > 0.55f ? juce::Colours::black : juce::Colours::white;
        const int style = (int) s.getProperties().getWithDefault ("knob", 0);

        // printed scale: tick marks, and the current position as a brighter tick
        for (int i = 0; i <= 10; ++i)
        {
            const float a = startAngle + (endAngle - startAngle) * (float) i / 10.0f;
            const auto p1 = c.getPointOnCircumference (r * 0.93f, a), p2 = c.getPointOnCircumference (r * (i % 5 == 0 ? 0.78f : 0.84f), a);
            g.setColour (mark.withAlpha (i % 5 == 0 ? 0.55f : 0.3f));
            g.drawLine ({ p1, p2 }, 1.3f);
        }
        juce::Path value;
        value.addCentredArc (c.x, c.y, r * 0.97f, r * 0.97f, 0, startAngle, angle, true);
        g.setColour (Colours::accent.withAlpha (0.75f));
        g.strokePath (value, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        auto shadow = [&] (float cr)
        {
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillEllipse (c.x - cr + 1.5f, c.y - cr + 3.0f, cr * 2, cr * 2);
        };
        auto pointer = [&] (float from, float to, float width, juce::Colour col)
        {
            juce::Path p;
            p.addRoundedRectangle (-width * 0.5f, -to, width, to - from, width * 0.5f);
            g.setColour (col);
            g.fillPath (p, juce::AffineTransform::rotation (angle).translated (c));
        };

        switch (style)
        {
            case 1: // chicken-head
            {
                const float cr = r * 0.5f;
                shadow (cr);
                juce::Path head;
                head.addEllipse (-cr, -cr, cr * 2, cr * 2);
                juce::Path wing;
                wing.startNewSubPath (-cr * 0.55f, cr * 0.3f);
                wing.lineTo (-cr * 0.22f, -r * 0.8f);
                wing.quadraticTo (0, -r * 0.88f, cr * 0.22f, -r * 0.8f);
                wing.lineTo (cr * 0.55f, cr * 0.3f);
                wing.quadraticTo (0, cr * 1.5f, -cr * 0.55f, cr * 0.3f);
                head.addPath (wing);
                const auto t = juce::AffineTransform::rotation (angle).translated (c);
                g.setColour (juce::Colours::black.withAlpha (0.4f));
                g.fillPath (head, t.translated (1.5f, 3.0f));
                juce::ColourGradient gr (juce::Colour (0xff3c3c40), c.x, c.y - r, juce::Colour (0xff0e0e10), c.x, c.y + r, false);
                g.setGradientFill (gr);
                g.fillPath (head, t);
                pointer (cr * 0.2f, r * 0.76f, 2.4f, juce::Colours::white.withAlpha (0.92f));
                break;
            }
            case 2: // skirted: black cap on a numbered metal skirt
            {
                const float sr = r * 0.74f, cr = r * 0.5f;
                shadow (sr);
                juce::ColourGradient sk (juce::Colour (0xffe2e2e2), c.x - sr, c.y - sr, juce::Colour (0xff6e6e6e), c.x + sr, c.y + sr, false);
                g.setGradientFill (sk);
                g.fillEllipse (c.x - sr, c.y - sr, sr * 2, sr * 2);
                pointer (cr, sr * 0.96f, 2.4f, juce::Colour (0xff111111));
                juce::ColourGradient gr (juce::Colour (0xff3a3d44), c.x, c.y - cr, juce::Colour (0xff101113), c.x, c.y + cr, false);
                g.setGradientFill (gr);
                g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
                pointer (cr * 0.25f, cr * 0.9f, 2.0f, juce::Colours::white.withAlpha (0.85f));
                break;
            }
            case 3: // small ribbed knob
            {
                const float cr = r * 0.62f;
                shadow (cr);
                g.setColour (juce::Colour (0xff141416));
                g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
                for (int i = 0; i < 24; ++i)
                {
                    const float a = juce::MathConstants<float>::twoPi * (float) i / 24.0f;
                    g.setColour (juce::Colours::white.withAlpha (0.09f));
                    g.drawLine ({ c.getPointOnCircumference (cr * 0.82f, a), c.getPointOnCircumference (cr, a) }, 1.0f);
                }
                juce::ColourGradient top (juce::Colour (0xff35363b), c.x, c.y - cr, juce::Colour (0xff141416), c.x, c.y + cr, false);
                g.setGradientFill (top);
                g.fillEllipse (c.x - cr * 0.78f, c.y - cr * 0.78f, cr * 1.56f, cr * 1.56f);
                pointer (cr * 0.1f, cr * 0.95f, 2.6f, juce::Colours::white.withAlpha (0.95f));
                break;
            }
            case 4: // knurled aluminium
            {
                const float cr = r * 0.66f;
                shadow (cr);
                juce::ColourGradient al (juce::Colour (0xfff2f2f2), c.x - cr, c.y - cr, juce::Colour (0xff8a8a8a), c.x + cr, c.y + cr, false);
                g.setGradientFill (al);
                g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
                for (int i = 0; i < 36; ++i)
                {
                    const float a = juce::MathConstants<float>::twoPi * (float) i / 36.0f;
                    g.setColour (juce::Colours::black.withAlpha (0.18f));
                    g.drawLine ({ c.getPointOnCircumference (cr * 0.86f, a), c.getPointOnCircumference (cr, a) }, 0.8f);
                }
                juce::ColourGradient top (juce::Colour (0xffdedede), c.x, c.y - cr, juce::Colour (0xffa9a9a9), c.x, c.y + cr, false);
                g.setGradientFill (top);
                g.fillEllipse (c.x - cr * 0.8f, c.y - cr * 0.8f, cr * 1.6f, cr * 1.6f);
                pointer (cr * 0.15f, cr * 0.95f, 2.2f, juce::Colour (0xff1a1a1a));
                break;
            }
            default: // dome
            {
                const float cr = r * 0.68f;
                shadow (cr);
                juce::ColourGradient grad (juce::Colour (0xff3a3d44), c.x, c.y - cr, juce::Colour (0xff121316), c.x, c.y + cr, false);
                g.setGradientFill (grad);
                g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.drawEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2, 1.0f);
                pointer (cr * 0.35f, cr * 0.95f, 3.0f, juce::Colours::white.withAlpha (0.92f));
                break;
            }
        }
    }

    // EQ faders
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle st, juce::Slider& s) override
    {
        if (st != juce::Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, st, s); return; }
        const float cx = (float) x + (float) w * 0.5f;
        for (int i = 0; i <= 10; ++i)
        {
            const float ty = (float) y + 6.0f + ((float) h - 12.0f) * (float) i / 10.0f;
            g.setColour (juce::Colours::white.withAlpha (i == 5 ? 0.5f : 0.2f));
            g.drawLine (cx - (i == 5 ? 12.0f : 9.0f), ty, cx - 5.0f, ty, 1.0f);
            g.drawLine (cx + 5.0f, ty, cx + (i == 5 ? 12.0f : 9.0f), ty, 1.0f);
        }
        g.setColour (juce::Colours::black);
        g.fillRoundedRectangle (cx - 2.5f, (float) y + 4.0f, 5.0f, (float) h - 8.0f, 2.0f);
        const juce::Rectangle<float> cap (cx - 9.0f, pos - 7.0f, 18.0f, 14.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (cap.translated (0, 2), 2.0f);
        juce::ColourGradient m (juce::Colour (0xfff0f0f0), cap.getX(), cap.getY(), juce::Colour (0xff8a8a8a), cap.getX(), cap.getBottom(), false);
        g.setGradientFill (m);
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (juce::Colour (0xff222222));
        g.fillRect (cap.getX() + 2, cap.getCentreY() - 0.75f, cap.getWidth() - 4, 1.5f);
    }

    // toggles on the gear: a small bat-handle toggle switch with its name
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat();
        const auto sw = r.removeFromLeft (18.0f).withSizeKeepingCentre (18.0f, juce::jmin (r.getHeight(), 22.0f));
        const auto c = sw.getCentre();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (sw.withSizeKeepingCentre (12.0f, sw.getHeight()), 3.0f);
        const auto tip = c.translated (0, b.getToggleState() ? -sw.getHeight() * 0.32f : sw.getHeight() * 0.32f);
        g.setColour (juce::Colour (0xffb0b0b0));
        g.drawLine ({ c, tip }, 3.0f);
        g.setColour (over ? juce::Colours::white : juce::Colour (0xffe0e0e0));
        g.fillEllipse (tip.x - 3.5f, tip.y - 3.5f, 7.0f, 7.0f);
        g.setColour (b.findColour (juce::ToggleButton::textColourId));
        g.setFont (uiFont (11.0f, true));
        g.drawFittedText (b.getButtonText(), r.reduced (4, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }

    juce::Font getLabelFont (juce::Label& l) override { return uiFont (l.getFont().getHeight()); }
    juce::Font getComboBoxFont (juce::ComboBox&) override { return uiFont (13.0f); }
    juce::Font getTextButtonFont (juce::TextButton&, int h) override { return uiFont (juce::jmin (14.0f, (float) h * 0.55f)); }
    juce::Font getPopupMenuFont() override { return uiFont (14.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc,
                               bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgc;
        if (down) col = col.brighter (0.15f); else if (over) col = col.brighter (0.07f);
        g.setColour (col);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (Colours::panelEdge.brighter (0.2f));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }
};

} // namespace pg
