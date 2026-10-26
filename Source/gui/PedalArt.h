// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace pg::PedalArt
{
//==============================================================================
// How a piece of gear looks. Every pedal type gets its own enclosure shape, finish and
// knob style that fits what it is (compact drives with a treadle, a round fuzz, an amp
// head, a speaker cab, a digital box with a screen...). All designs are original.
enum class Shape { stomp, slim, compact, wide, wedge, round, head, cab, digital };
enum class KnobStyle { dome, chicken, skirted, small, silver, fader };
enum class Finish { matte, brushed, sparkle, hammered, tolex };

struct Style
{
    Shape shape = Shape::stomp;
    KnobStyle knob = KnobStyle::dome;
    Finish finish = Finish::matte;
    juce::Colour body, ink, panel;   // panel: treadle / control plate / band colour
    bool light = false;               // light body -> dark lettering
    bool nameBand = false;            // name printed on a coloured band (stomp boxes)
};

Style styleFor (const juce::String& typeId, juce::Colour colour);
juce::Typeface::Ptr logoTypeface();                    // the app's script logo font
void drawLogoBadge (juce::Graphics&, juce::Rectangle<float> plate, const juce::String& text, float corner);
int knobCount (const juce::String& typeId);          // knob/fader controls of that gear (cached)

// big enclosure
juce::Path bodyPath (juce::Rectangle<float> body, Shape);
void drawBody (juce::Graphics&, juce::Rectangle<float> body, const Style&);
void drawLed (juce::Graphics&, juce::Rectangle<float>, bool on, float glow = 0.4f);   // glow: 0..1 signal level
void drawStompSwitch (juce::Graphics&, juce::Rectangle<float>, float press = 0.0f);  // press: 0..1 animation
void drawBatToggle (juce::Graphics&, juce::Rectangle<float>, bool up, const char* upLabel = nullptr,
                    const char* downLabel = nullptr, juce::Colour ink = juce::Colours::white);
void drawRocker (juce::Graphics&, juce::Rectangle<float>, bool on, float glow = 0.4f);
void drawJewel (juce::Graphics&, juce::Rectangle<float>, bool on, float glow, juce::Colour colour);
void drawSoftSwitch (juce::Graphics&, juce::Rectangle<float>);
void drawGrille (juce::Graphics&, juce::Rectangle<float>, juce::Colour cloth, bool speakers);

// small drawings: chain strip pedals and gear browser tiles. Bodies sit on the cell's floor.
struct Mini
{
    juce::Rectangle<float> body, footswitch;
    float jackY = 0;                  // height of the input/output jacks on the sides
};
Mini miniGeometry (juce::Rectangle<float> cell, const Style&);
Mini drawMini (juce::Graphics&, juce::Rectangle<float> cell, const Style&, const juce::String& name,
               int knobs, bool on, bool withName, float glow = 0.3f, float press = 0.0f, float lift = 0.0f);

// A drag image: just the pedal on a transparent background, same size as the component it
// is dragged from (so JUCE places it exactly where the pedal was), rendered at 2x for sharpness.
juce::ScaledImage dragImage (juce::Rectangle<int> componentBounds, juce::Rectangle<float> cell, const Style&,
                             const juce::String& name, int knobs, bool on);

// patch cables: a right-angle "pancake" plug in each jack, a short black cable between
void drawPlug (juce::Graphics&, juce::Point<float> jack, bool bodyToTheRight);
void drawCable (juce::Graphics&, juce::Point<float> fromJack, bool fromRight,
                juce::Point<float> toJack, bool toRight, float floorY);
void drawJackPlate (juce::Graphics&, juce::Rectangle<float>, const juce::String& label);

} // namespace pg::PedalArt
