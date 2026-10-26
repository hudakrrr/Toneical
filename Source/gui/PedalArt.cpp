// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// switches over gear shapes handle the special shapes and send the rest to "default:" on purpose
#if defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

#include "PedalArt.h"
#include "modules/ModuleFactory.h"
#include "BinaryData.h"
#include <map>

namespace pg::PedalArt
{
using juce::Colour;
using juce::Rectangle;
using juce::Point;

juce::Typeface::Ptr logoTypeface()
{
    static const auto face = juce::Typeface::createSystemTypefaceFor (BinaryData::LogoFont_ttf, (size_t) BinaryData::LogoFont_ttfSize);
    return face;
}

// the "Toneical" badge: orange script on a dark, chrome-edged plate (like the app's logo)
void drawLogoBadge (juce::Graphics& g, Rectangle<float> plate, const juce::String& text, float corner)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (plate.translated (0, 1.5f), corner);
    juce::ColourGradient bg (Colour (0xff24262b), plate.getX(), plate.getY(), Colour (0xff0d0e10), plate.getX(), plate.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRoundedRectangle (plate, corner);
    juce::ColourGradient rim (Colour (0xffd8d8d8), plate.getX(), plate.getY(), Colour (0xff5a5a5a), plate.getX(), plate.getBottom(), false);
    g.setGradientFill (rim);
    g.drawRoundedRectangle (plate.reduced (0.6f), corner, juce::jmax (1.0f, plate.getHeight() * 0.035f));
    const auto f = juce::Font (juce::FontOptions (logoTypeface()).withHeight (plate.getHeight() * 1.05f));
    g.setFont (f);
    const auto tr = plate.reduced (plate.getHeight() * 0.25f, 0).translated (0, -plate.getHeight() * 0.04f).toNearestInt();
    g.setColour (Colour (0xfff2a33a).withAlpha (0.28f));        // soft glow
    g.drawFittedText (text, tr.translated (0, 1), juce::Justification::centred, 1, 0.8f);
    g.setColour (Colour (0xfff2a33a));
    g.drawFittedText (text, tr, juce::Justification::centred, 1, 0.8f);
}

//==============================================================================
Style styleFor (const juce::String& t, Colour c)
{
    Style s;
    s.body = c;
    s.light = c.getPerceivedBrightness() > 0.55f;
    s.ink = s.light ? Colour (0xff17171a) : Colour (0xfff1eee8);
    s.panel = c.darker (0.45f);

    auto set = [&s] (Shape sh, KnobStyle k, Finish f) { s.shape = sh; s.knob = k; s.finish = f; };

    if      (t == "gate")     set (Shape::stomp,   KnobStyle::small,   Finish::hammered);
    else if (t == "comp")     set (Shape::stomp,   KnobStyle::skirted, Finish::matte);
    else if (t == "boost")    set (Shape::slim,    KnobStyle::chicken, Finish::matte);
    else if (t == "ts808")    { set (Shape::stomp, KnobStyle::dome,    Finish::matte); s.panel = c.darker (0.3f); s.nameBand = true; }
    else if (t == "rat")      set (Shape::wedge,   KnobStyle::dome,    Finish::matte);
    else if (t == "muff")     { set (Shape::stomp, KnobStyle::dome,    Finish::brushed); s.panel = Colour (0xffb3261e); s.nameBand = true; }
    else if (t == "klon")     { set (Shape::stomp, KnobStyle::dome,    Finish::brushed); s.panel = Colour (0xff3b2f17); s.nameBand = true; }
    else if (t == "sd1" || t == "ds1" || t == "hm2" || t == "os2" || t == "mt2")
    {
        set (Shape::compact, KnobStyle::small, Finish::matte);
        s.panel = Colour (0xff1d1e21);                       // control panel
    }
    else if (t == "mxr78" || t == "m77" || t == "grind") set (Shape::slim, KnobStyle::small, Finish::matte);
    else if (t == "bb")       { set (Shape::stomp, KnobStyle::silver,  Finish::brushed); s.panel = Colour (0xffd8701f); s.nameBand = true; }
    else if (t == "protein")  { set (Shape::wide,  KnobStyle::skirted, Finish::matte); s.panel = Colour (0xff2f9e5a); s.nameBand = true; }
    else if (t == "precision") set (Shape::stomp,  KnobStyle::small,   Finish::hammered);
    else if (t == "sd805")    { set (Shape::stomp, KnobStyle::chicken, Finish::sparkle); }
    else if (t == "tubeamp")  { set (Shape::head,  KnobStyle::chicken, Finish::tolex); s.panel = Colour (0xffc9a66b); }
    else if (t == "realamp")  { set (Shape::head,  KnobStyle::skirted, Finish::tolex); s.panel = Colour (0xff26272b); }
    else if (t == "cabsim")   { set (Shape::cab,   KnobStyle::small,   Finish::tolex); s.panel = Colour (0xff2a2622); }
    else if (t == "nam" || t == "ir") set (Shape::digital, KnobStyle::small, Finish::matte);
    else if (t == "eq")       { set (Shape::wide,  KnobStyle::fader,   Finish::brushed); s.panel = Colour (0xff1b1d22); }
    else if (t == "chorus")   set (Shape::stomp,   KnobStyle::dome,    Finish::sparkle);
    else if (t == "phaser")   set (Shape::stomp,   KnobStyle::skirted, Finish::matte);
    else if (t == "trem")     set (Shape::wedge,   KnobStyle::chicken, Finish::hammered);
    else if (t == "delay")    set (Shape::wide,    KnobStyle::silver,  Finish::brushed);
    else if (t == "reverb")   set (Shape::wide,    KnobStyle::silver,  Finish::sparkle);
    return s;
}

int knobCount (const juce::String& typeId)
{
    static std::map<juce::String, int> cache;
    if (auto it = cache.find (typeId); it != cache.end()) return it->second;
    int n = 0;
    if (auto m = ModuleFactory::create (typeId))
        for (auto& sp : m->getSpecs())
            if (sp.kind == ParamSpec::Kind::knob) ++n;
    cache[typeId] = n;
    return n;
}

//==============================================================================
juce::Path bodyPath (Rectangle<float> b, Shape shape)
{
    juce::Path p;
    switch (shape)
    {
        case Shape::round:   p.addEllipse (b); break;
        case Shape::head:
        case Shape::cab:     p.addRoundedRectangle (b, juce::jmin (b.getWidth(), b.getHeight()) * 0.035f); break;
        case Shape::compact:
        {
            // slightly chamfered top corners, rounded bottom
            const float c = b.getWidth() * 0.09f, r = b.getWidth() * 0.05f;
            p.startNewSubPath (b.getX() + c, b.getY());
            p.lineTo (b.getRight() - c, b.getY());
            p.lineTo (b.getRight(), b.getY() + c);
            p.lineTo (b.getRight(), b.getBottom() - r);
            p.quadraticTo (b.getRight(), b.getBottom(), b.getRight() - r, b.getBottom());
            p.lineTo (b.getX() + r, b.getBottom());
            p.quadraticTo (b.getX(), b.getBottom(), b.getX(), b.getBottom() - r);
            p.lineTo (b.getX(), b.getY() + c);
            p.closeSubPath();
            break;
        }
        default: p.addRoundedRectangle (b, juce::jmin (b.getWidth(), b.getHeight()) * 0.06f); break;
    }
    return p;
}

static void drawFinish (juce::Graphics& g, Rectangle<float> b, const Style& s)
{
    juce::Random rnd (int (s.body.getARGB() & 0x7fffffff));
    const float area = b.getWidth() * b.getHeight();
    switch (s.finish)
    {
        case Finish::brushed:
            for (float y = b.getY(); y < b.getBottom(); y += 1.5f)
            {
                g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rnd.nextFloat() * 0.06f));
                g.fillRect (b.getX(), y, b.getWidth(), 1.0f);
            }
            break;
        case Finish::sparkle:
            for (int i = 0; i < (int) (area / 40.0f); ++i)
            {
                g.setColour (juce::Colours::white.withAlpha (0.08f + rnd.nextFloat() * 0.3f));
                const float d = 0.6f + rnd.nextFloat() * 1.2f;
                g.fillEllipse (b.getX() + rnd.nextFloat() * b.getWidth(), b.getY() + rnd.nextFloat() * b.getHeight(), d, d);
            }
            break;
        case Finish::hammered:
            for (int i = 0; i < (int) (area / 260.0f); ++i)
            {
                const float d = 4.0f + rnd.nextFloat() * 9.0f;
                const float x = b.getX() + rnd.nextFloat() * b.getWidth(), y = b.getY() + rnd.nextFloat() * b.getHeight();
                juce::ColourGradient dim (juce::Colours::black.withAlpha (0.16f), x + d * 0.3f, y + d * 0.3f,
                                          juce::Colours::white.withAlpha (0.07f), x + d, y + d, true);
                g.setGradientFill (dim);
                g.fillEllipse (x, y, d, d);
            }
            break;
        case Finish::tolex:
            for (float y = b.getY(); y < b.getBottom(); y += 2.0f)
                for (float x = b.getX() + (rnd.nextFloat() * 2.0f); x < b.getRight(); x += 3.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (rnd.nextFloat() * 0.045f));
                    g.fillRect (x, y, 1.5f, 1.0f);
                }
            break;
        case Finish::matte: break;
    }
}

void drawBody (juce::Graphics& g, Rectangle<float> b, const Style& s)
{
    const auto path = bodyPath (b, s.shape);

    // soft contact shadow on the stage floor
    for (int k = 6; k >= 1; --k)
    {
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.fillPath (bodyPath (b.expanded ((float) k * 0.8f), s.shape), juce::AffineTransform::translation (0, 3.0f + (float) k * 1.6f));
    }

    const auto base = s.finish == Finish::tolex ? Colour (0xff1b1918) : s.body;
    juce::ColourGradient grad (base.brighter (0.22f), b.getX(), b.getY(), base.darker (0.38f), b.getX(), b.getBottom(), false);
    if (s.finish == Finish::brushed)
    {
        grad = juce::ColourGradient (base.brighter (0.35f), b.getX(), b.getY(), base.darker (0.25f), b.getRight(), b.getBottom(), false);
        grad.addColour (0.45, base.brighter (0.05f));
        grad.addColour (0.6, base.brighter (0.3f));
    }
    if (s.shape == Shape::round)
        grad = juce::ColourGradient (base.brighter (0.35f), b.getCentreX() - b.getWidth() * 0.2f, b.getCentreY() - b.getHeight() * 0.25f,
                                     base.darker (0.45f), b.getRight(), b.getBottom(), true);
    g.setGradientFill (grad);
    g.fillPath (path);

    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (path);
        drawFinish (g, b, s);

        if (s.shape == Shape::wedge)
        {
            // sloped front face: the top part catches the light differently
            const auto face = b.withHeight (b.getHeight() * 0.44f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRect (face);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (face.getX(), face.getBottom() - 1.5f, face.getWidth(), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRect (face.getX(), face.getBottom() + 1.5f, face.getWidth(), 1.0f);
        }
        if (s.shape == Shape::head || s.shape == Shape::cab)
        {
            // piping / corner protectors
            g.setColour (Colour (0xffd8cfb8).withAlpha (0.55f));
            g.drawRoundedRectangle (b.reduced (b.getHeight() * 0.035f), 4.0f, 1.6f);
            const float k = juce::jmin (b.getWidth(), b.getHeight()) * 0.09f;
            for (auto p : { b.getTopLeft(), b.getTopRight().translated (-k, 0), b.getBottomLeft().translated (0, -k), b.getBottomRight().translated (-k, -k) })
            {
                juce::ColourGradient m (Colour (0xffdcdcdc), p.x, p.y, Colour (0xff6a6a6a), p.x + k, p.y + k, false);
                g.setGradientFill (m);
                g.fillRoundedRectangle (p.x, p.y, k, k, 3.0f);
            }
        }
    }

    {
        // soft specular sheen + a light catch along the top edge (bevel)
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (path);
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.09f), b.getX(), b.getY(),
                                    juce::Colours::transparentWhite, b.getX() + b.getWidth() * 0.65f, b.getY() + b.getHeight() * 0.45f, false);
        g.setGradientFill (sheen);
        g.fillRect (b);
        g.reduceClipRegion (b.withHeight (b.getHeight() * 0.2f).toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.strokePath (bodyPath (b.reduced (1.5f), s.shape), juce::PathStrokeType (1.2f));
    }
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.strokePath (path, juce::PathStrokeType (1.2f));

    // screws on the boxy metal enclosures
    if (s.shape == Shape::stomp || s.shape == Shape::wide || s.shape == Shape::slim || s.shape == Shape::digital)
    {
        const float in = 9.0f;
        for (auto p : { b.getTopLeft().translated (in, in), b.getTopRight().translated (-in, in),
                        b.getBottomLeft().translated (in, -in), b.getBottomRight().translated (-in, -in) })
        {
            g.setColour (Colour (0xffa8a8a8)); g.fillEllipse (p.x - 3.2f, p.y - 3.2f, 6.4f, 6.4f);
            g.setColour (Colour (0xff4d4d4d)); g.drawLine (p.x - 2.2f, p.y + 1.2f, p.x + 2.2f, p.y - 1.2f, 1.1f);
        }
    }
}

void drawLed (juce::Graphics& g, Rectangle<float> r, bool on, float glow)
{
    const auto c = r.getCentre();
    const float R = r.getWidth() * 0.5f;
    if (on)
    {
        // the glow follows your playing
        const float k = 0.45f + 0.55f * juce::jlimit (0.0f, 1.0f, glow);
        juce::ColourGradient halo (Colour (0xffff4433).withAlpha (0.55f * k), c.x, c.y,
                                   Colour (0x00ff4433), c.x + R * (2.4f + 2.6f * k), c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (r.expanded (R * (1.4f + 2.6f * k)));
    }
    // chrome bezel with a dark gap, like a real panel-mount LED holder
    const float bz = R * 1.55f;
    g.setColour (juce::Colours::black.withAlpha (0.4f));
    g.fillEllipse (c.x - bz + 0.6f, c.y - bz + 1.2f, bz * 2, bz * 2);
    juce::ColourGradient bzG (Colour (0xffeeeeee), c.x - bz, c.y - bz, Colour (0xff4a4a4a), c.x + bz, c.y + bz, false);
    g.setGradientFill (bzG);
    g.fillEllipse (c.x - bz, c.y - bz, bz * 2, bz * 2);
    g.setColour (Colour (0xff0e0e0e));
    g.fillEllipse (c.x - R * 1.12f, c.y - R * 1.12f, R * 2.24f, R * 2.24f);
    // domed lens
    juce::ColourGradient lg (on ? Colour (0xffffd0c4) : Colour (0xff5c1818), c.x - R * 0.25f, c.y - R * 0.3f,
                             on ? Colour (0xffd8241a) : Colour (0xff250707), c.x + R, c.y + R, true);
    g.setGradientFill (lg);
    g.fillEllipse (r);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.8f : 0.3f));
    g.fillEllipse (c.x - R * 0.55f, c.y - R * 0.62f, R * 0.55f, R * 0.38f);
}

void drawStompSwitch (juce::Graphics& g, Rectangle<float> fs, float press)
{
    fs = fs.translated (0, press * 1.5f);
    // hex nut + chrome cap
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (fs.translated (0, 3));
    juce::Path hex;
    hex.addPolygon (fs.getCentre(), 6, fs.getWidth() * 0.62f, 0.0f);
    juce::ColourGradient nut (Colour (0xffbdbdbd), fs.getX(), fs.getY(), Colour (0xff5c5c5c), fs.getRight(), fs.getBottom(), false);
    g.setGradientFill (nut); g.fillPath (hex);
    const auto cap = fs.reduced (fs.getWidth() * 0.16f);
    juce::ColourGradient metal (Colour (0xfff4f4f4), cap.getX(), cap.getY(), Colour (0xff747474), cap.getRight(), cap.getBottom(), false);
    g.setGradientFill (metal); g.fillEllipse (cap);
    g.setColour (Colour (0xff505050)); g.drawEllipse (cap.reduced (cap.getWidth() * 0.12f), 1.0f);
}

// Chrome bat-handle toggle (amp power / standby): knurled ring, hex nut, a tapered lever with
// a ball end, its shadow on the panel, and optional engraved labels above and below.
void drawBatToggle (juce::Graphics& g, Rectangle<float> r, bool up, const char* upLabel, const char* downLabel, Colour ink)
{
    const auto c = r.getCentre();
    const float u = juce::jmin (r.getWidth(), r.getHeight() * 0.62f);   // size unit
    const float ringR = u * 0.42f;

    if (upLabel != nullptr || downLabel != nullptr)
    {
        g.setFont (juce::Font (juce::FontOptions (juce::jmax (7.0f, u * 0.26f), juce::Font::bold)));
        g.setColour (ink.withAlpha (up ? 0.95f : 0.45f));
        if (upLabel)   g.drawText (upLabel, Rectangle<float> (c.x - u, r.getY() - u * 0.42f, u * 2, u * 0.4f).toNearestInt(), juce::Justification::centred);
        g.setColour (ink.withAlpha (up ? 0.45f : 0.95f));
        if (downLabel) g.drawText (downLabel, Rectangle<float> (c.x - u, r.getBottom() + u * 0.02f, u * 2, u * 0.4f).toNearestInt(), juce::Justification::centred);
    }

    // mount: shadow, knurled ring, hex nut, bushing
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - ringR + 1.0f, c.y - ringR + 2.0f, ringR * 2, ringR * 2);
    juce::ColourGradient ringG (Colour (0xffe9e9e9), c.x - ringR, c.y - ringR, Colour (0xff5b5b5b), c.x + ringR, c.y + ringR, false);
    g.setGradientFill (ringG);
    g.fillEllipse (c.x - ringR, c.y - ringR, ringR * 2, ringR * 2);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (int i = 0; i < 28; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 28.0f;
        g.drawLine ({ c.getPointOnCircumference (ringR * 0.84f, a), c.getPointOnCircumference (ringR, a) }, 0.7f);
    }
    juce::Path hex;
    hex.addPolygon (c, 6, ringR * 0.72f, juce::MathConstants<float>::pi / 6.0f);
    juce::ColourGradient nutG (Colour (0xfff4f4f4), c.x - ringR, c.y - ringR, Colour (0xff6e6e6e), c.x + ringR * 0.6f, c.y + ringR, false);
    g.setGradientFill (nutG); g.fillPath (hex);
    g.setColour (juce::Colours::black.withAlpha (0.35f)); g.strokePath (hex, juce::PathStrokeType (0.6f));
    g.setColour (Colour (0xff2a2a2c));
    g.fillEllipse (c.x - ringR * 0.34f, c.y - ringR * 0.34f, ringR * 0.68f, ringR * 0.68f);

    // lever (seen slightly from the front, so it's foreshortened)
    const float len = r.getHeight() * 0.40f;
    const float dir = up ? -1.0f : 1.0f;
    const auto tip = c.translated (0, dir * len);
    const float wb = ringR * 0.42f, wt = ringR * 0.26f;
    juce::Path lever;
    lever.startNewSubPath (c.x - wb * 0.5f, c.y);
    lever.lineTo (tip.x - wt * 0.5f, tip.y);
    lever.lineTo (tip.x + wt * 0.5f, tip.y);
    lever.lineTo (c.x + wb * 0.5f, c.y);
    lever.closeSubPath();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillPath (lever, juce::AffineTransform::translation (2.0f, 3.0f));
    juce::ColourGradient barG (Colour (0xff6a6a6a), c.x - wb * 0.5f, 0, Colour (0xff5a5a5a), c.x + wb * 0.5f, 0, false);
    barG.addColour (0.35, Colour (0xfff6f6f6));
    barG.addColour (0.55, Colour (0xffbdbdbd));
    g.setGradientFill (barG);
    g.fillPath (lever);
    // ball end
    const float br = ringR * 0.3f;
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillEllipse (tip.x - br + 2.0f, tip.y - br + 3.0f, br * 2, br * 2);
    juce::ColourGradient ballG (Colour (0xffffffff), tip.x - br * 0.4f, tip.y - br * 0.5f, Colour (0xff6c6c6c), tip.x + br, tip.y + br, true);
    g.setGradientFill (ballG);
    g.fillEllipse (tip.x - br, tip.y - br, br * 2, br * 2);
}

// Illuminated rocker switch (cab on/off): black bezel, the lit half glows red with the signal.
void drawRocker (juce::Graphics& g, Rectangle<float> r, bool on, float glow)
{
    const float corner = r.getWidth() * 0.18f;
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0, 2.0f), corner);
    juce::ColourGradient bez (Colour (0xff3a3c42), r.getX(), r.getY(), Colour (0xff0b0c0e), r.getX(), r.getBottom(), false);
    g.setGradientFill (bez);
    g.fillRoundedRectangle (r, corner);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r.reduced (0.5f), corner, 0.8f);

    auto face = r.reduced (r.getWidth() * 0.16f, r.getWidth() * 0.16f);
    const auto topHalf = face.withHeight (face.getHeight() * 0.5f), botHalf = face.withTrimmedTop (face.getHeight() * 0.5f);
    const auto pressed = on ? topHalf : botHalf, raised = on ? botHalf : topHalf;
    const Colour lens = on ? Colour (0xffe8261c) : Colour (0xff4a1010);
    if (on)
    {
        const float k = 0.5f + 0.5f * glow;
        juce::ColourGradient halo (Colour (0xffff3b2a).withAlpha (0.35f * k), r.getCentreX(), r.getCentreY(),
                                   Colour (0x00ff3b2a), r.getCentreX() + r.getWidth() * 1.3f, r.getCentreY(), true);
        g.setGradientFill (halo);
        g.fillEllipse (r.expanded (r.getWidth() * 0.9f));
    }
    // pressed half sits low and dark, the raised half catches the light
    juce::ColourGradient pg (lens.darker (0.55f), pressed.getX(), pressed.getY(), lens.darker (0.2f), pressed.getX(), pressed.getBottom(), false);
    g.setGradientFill (pg); g.fillRoundedRectangle (pressed, corner * 0.5f);
    juce::ColourGradient rg (lens.brighter (on ? 0.5f : 0.15f), raised.getX(), raised.getY(), lens.darker (0.1f), raised.getX(), raised.getBottom(), false);
    g.setGradientFill (rg); g.fillRoundedRectangle (raised, corner * 0.5f);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.35f : 0.12f));
    g.fillRoundedRectangle (raised.withHeight (raised.getHeight() * 0.25f).reduced (2.0f, 0).translated (0, 1.0f), 1.5f);
    // I / O
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    const float sw = juce::jmax (1.0f, face.getWidth() * 0.09f);
    g.fillRect (topHalf.getCentreX() - sw * 0.5f, topHalf.getY() + topHalf.getHeight() * 0.25f, sw, topHalf.getHeight() * 0.5f);
    const float o = botHalf.getHeight() * 0.42f;
    g.drawEllipse (botHalf.getCentreX() - o * 0.5f, botHalf.getCentreY() - o * 0.5f, o, o, sw * 0.9f);
}

// Faceted pilot-light jewel in a knurled chrome bezel (amp heads); glows with the signal.
void drawJewel (juce::Graphics& g, Rectangle<float> r, bool on, float glow, Colour colour)
{
    const auto c = r.getCentre();
    const float R = r.getWidth() * 0.5f;
    if (on)
    {
        const float k = 0.45f + 0.55f * glow;
        juce::ColourGradient halo (colour.withAlpha (0.5f * k), c.x, c.y, colour.withAlpha (0.0f), c.x + R * (2.2f + 1.8f * k), c.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (r.expanded (R * (1.2f + 1.8f * k)));
    }
    // bezel
    const float bz = R * 1.38f;
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - bz + 1, c.y - bz + 2, bz * 2, bz * 2);
    juce::ColourGradient bzG (Colour (0xfff0f0f0), c.x - bz, c.y - bz, Colour (0xff4d4d4d), c.x + bz, c.y + bz, false);
    g.setGradientFill (bzG);
    g.fillEllipse (c.x - bz, c.y - bz, bz * 2, bz * 2);
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    for (int i = 0; i < 32; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float) i / 32.0f;
        g.drawLine ({ c.getPointOnCircumference (bz * 0.86f, a), c.getPointOnCircumference (bz, a) }, 0.6f);
    }
    g.setColour (Colour (0xff111111));
    g.fillEllipse (c.x - R * 1.05f, c.y - R * 1.05f, R * 2.1f, R * 2.1f);
    // jewel body
    const Colour lit = on ? colour : colour.darker (2.2f);
    juce::ColourGradient jg (lit.brighter (on ? 0.35f : 0.1f), c.x - R * 0.3f, c.y - R * 0.35f, lit.darker (0.7f), c.x + R, c.y + R, true);
    g.setGradientFill (jg);
    g.fillEllipse (r);
    // facets
    for (int i = 0; i < 8; ++i)
    {
        const float a0 = juce::MathConstants<float>::twoPi * (float) i / 8.0f, a1 = a0 + juce::MathConstants<float>::twoPi / 8.0f;
        juce::Path f;
        f.startNewSubPath (c.getPointOnCircumference (R * 0.42f, a0));
        f.lineTo (c.getPointOnCircumference (R * 0.98f, a0));
        f.lineTo (c.getPointOnCircumference (R * 0.98f, a1));
        f.lineTo (c.getPointOnCircumference (R * 0.42f, a1));
        f.closeSubPath();
        g.setColour ((i % 2 ? juce::Colours::white : juce::Colours::black).withAlpha (on ? 0.12f : 0.18f));
        g.fillPath (f);
    }
    juce::Path table;
    table.addPolygon (c, 8, R * 0.44f, juce::MathConstants<float>::pi / 8.0f);
    g.setColour (lit.brighter (on ? 0.6f : 0.2f).withAlpha (0.55f));
    g.fillPath (table);
    g.setColour (juce::Colours::white.withAlpha (on ? 0.85f : 0.35f));
    g.fillEllipse (c.x - R * 0.45f, c.y - R * 0.55f, R * 0.35f, R * 0.22f);
}

void drawSoftSwitch (juce::Graphics& g, Rectangle<float> r)
{
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.fillRoundedRectangle (r.translated (0, 2.5f), r.getHeight() * 0.3f);
    juce::ColourGradient rub (Colour (0xff4a4c52), r.getX(), r.getY(), Colour (0xff1b1c1f), r.getX(), r.getBottom(), false);
    g.setGradientFill (rub);
    g.fillRoundedRectangle (r, r.getHeight() * 0.3f);
    g.setColour (juce::Colours::white.withAlpha (0.1f));
    g.drawRoundedRectangle (r.reduced (1.5f), r.getHeight() * 0.28f, 1.0f);
}

void drawGrille (juce::Graphics& g, Rectangle<float> r, Colour cloth, bool speakers)
{
    juce::Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (r.toNearestInt());
    g.setColour (cloth);
    g.fillRect (r);
    if (speakers)
    {
        // speaker cones shining through the cloth
        const bool four = r.getWidth() > 1.2f * r.getHeight() * 0.9f || r.getHeight() > 120;
        const int n = four ? 2 : 1;
        const float cw = r.getWidth() / (float) n, ch = r.getHeight() / (float) n;
        for (int i = 0; i < n * n; ++i)
        {
            const float d = juce::jmin (cw, ch) * 0.86f;
            const auto cc = Point<float> (r.getX() + cw * ((float) (i % n) + 0.5f), r.getY() + ch * ((float) (i / n) + 0.5f));
            juce::ColourGradient cone (juce::Colours::black.withAlpha (0.55f), cc.x, cc.y, juce::Colours::black.withAlpha (0.12f), cc.x + d * 0.5f, cc.y, true);
            g.setGradientFill (cone);
            g.fillEllipse (cc.x - d * 0.5f, cc.y - d * 0.5f, d, d);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (cc.x - d * 0.12f, cc.y - d * 0.12f, d * 0.24f, d * 0.24f);
        }
    }
    // woven texture
    for (float y = r.getY(); y < r.getBottom(); y += 3.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
    }
    for (float x = r.getX(); x < r.getRight(); x += 3.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.1f));
        g.fillRect (x, r.getY(), 1.0f, r.getHeight());
    }
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawRect (r, 1.5f);
}

//==============================================================================
Mini miniGeometry (Rectangle<float> cell, const Style& s)
{
    float w = 0.84f, h = 0.97f;
    switch (s.shape)
    {
        case Shape::compact: w = 0.70f; h = 0.98f; break;
        case Shape::slim:    w = 0.62f; h = 0.88f; break;
        case Shape::stomp:   w = 0.80f; h = 0.98f; break;
        case Shape::wide:    w = 0.98f; h = 0.84f; break;
        case Shape::wedge:   w = 0.90f; h = 0.92f; break;
        case Shape::round:   w = 0.96f; h = 0.0f;  break;
        case Shape::head:    w = 1.0f;  h = 0.58f; break;
        case Shape::cab:     w = 0.98f; h = 0.0f;  break;
        case Shape::digital: w = 0.86f; h = 0.92f; break;
    }
    Mini m;
    const float bw = cell.getWidth() * w;
    const float bh = h > 0 ? cell.getHeight() * h : bw;  // round & cab are square
    m.body = Rectangle<float> (cell.getCentreX() - bw * 0.5f, cell.getBottom() - bh, bw, bh);
    const auto& b = m.body;
    m.jackY = s.shape == Shape::round ? b.getCentreY() - 10.0f : b.getY() + juce::jmin (b.getHeight() * 0.32f, 40.0f);

    const float fsD = juce::jlimit (12.0f, 24.0f, juce::jmin (b.getWidth() * 0.3f, b.getHeight() * 0.2f));
    switch (s.shape)
    {
        case Shape::compact: m.footswitch = b.withTrimmedTop (b.getHeight() * 0.52f).reduced (b.getWidth() * 0.1f, 0).withTrimmedBottom (b.getHeight() * 0.04f); break;
        case Shape::head:    m.footswitch = Rectangle<float> (b.getRight() - 20, b.getBottom() - 25, 14, 20); break;
        case Shape::cab:     m.footswitch = Rectangle<float> (b.getRight() - 19, b.getY() + 4, 12, 15); break;
        case Shape::digital: m.footswitch = Rectangle<float> (b.getWidth() * 0.34f, b.getHeight() * 0.11f).withCentre ({ b.getCentreX(), b.getBottom() - b.getHeight() * 0.13f }); break;
        case Shape::round:   m.footswitch = Rectangle<float> (fsD, fsD).withCentre ({ b.getCentreX(), b.getY() + b.getHeight() * 0.74f }); break;
        default:             m.footswitch = Rectangle<float> (fsD, fsD).withCentre ({ b.getCentreX(), b.getBottom() - b.getHeight() * 0.15f }); break;
    }
    return m;
}

// one small knob, drawn like the big ones (shadow, skirt/cap, highlight, pointer)
static void miniKnob (juce::Graphics& g, Point<float> c, float d, KnobStyle ks, int seed)
{
    const float r = d * 0.5f;
    const float a = -2.3f + 0.55f * (float) ((seed * 37) % 9);       // varied pointer angles look natural
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillEllipse (c.x - r + 0.6f, c.y - r + 1.2f, d, d);
    if (ks == KnobStyle::skirted)
    {
        juce::ColourGradient sk (Colour (0xffe6e6e6), c.x - r, c.y - r, Colour (0xff7a7a7a), c.x + r, c.y + r, false);
        g.setGradientFill (sk); g.fillEllipse (c.x - r, c.y - r, d, d);
    }
    const float cr = ks == KnobStyle::skirted ? r * 0.68f : r;
    const bool silver = ks == KnobStyle::silver;
    juce::ColourGradient cap (silver ? Colour (0xfff4f4f4) : Colour (0xff4a4c52), c.x, c.y - cr,
                              silver ? Colour (0xff9a9a9a) : Colour (0xff0d0e10), c.x, c.y + cr, false);
    g.setGradientFill (cap);
    if (ks == KnobStyle::chicken)
    {
        juce::Path p;
        p.addEllipse (-cr * 0.8f, -cr * 0.8f, cr * 1.6f, cr * 1.6f);
        p.addRoundedRectangle (-cr * 0.32f, -cr * 1.15f, cr * 0.64f, cr * 2.1f, cr * 0.3f);
        g.fillPath (p, juce::AffineTransform::rotation (a).translated (c));
    }
    else
        g.fillEllipse (c.x - cr, c.y - cr, cr * 2, cr * 2);
    g.setColour (juce::Colours::white.withAlpha (silver ? 0.5f : 0.12f));
    g.drawEllipse (c.x - cr + 0.4f, c.y - cr + 0.4f, cr * 2 - 0.8f, cr * 2 - 0.8f, 0.6f);
    const auto tip = c.getPointOnCircumference (cr * 0.85f, a);
    g.setColour (silver ? Colour (0xff1a1a1a) : Colour (0xfff4f4f4));
    g.drawLine ({ c.getPointOnCircumference (cr * 0.2f, a), tip }, juce::jmax (1.0f, d * 0.12f));
}

// knobs in rows, never bigger than maxD, centred in 'area'
static void miniKnobs (juce::Graphics& g, Rectangle<float> area, int n, KnobStyle ks, float maxD)
{
    if (n <= 0 || area.isEmpty()) return;
    if (ks == KnobStyle::fader)
    {
        const float cw = area.getWidth() / (float) n;
        for (int i = 0; i < n; ++i)
        {
            const float x = area.getX() + cw * ((float) i + 0.5f);
            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.fillRoundedRectangle (x - 1.0f, area.getY() + 1, 2.0f, area.getHeight() - 2, 1.0f);
            const float y = area.getY() + 3 + (area.getHeight() - 8) * (0.25f + 0.1f * (float) ((i * 7) % 5));
            juce::ColourGradient cap (Colour (0xfff2f2f2), x, y, Colour (0xff8c8c8c), x, y + 4, false);
            g.setGradientFill (cap);
            g.fillRoundedRectangle (x - 3.5f, y, 7.0f, 4.0f, 1.0f);
        }
        return;
    }
    const int cols = n <= 4 ? n : (n + 1) / 2;
    const int rows = (n + cols - 1) / cols;
    const float cw = area.getWidth() / (float) cols, ch = area.getHeight() / (float) rows;
    const float d = juce::jmin (maxD, cw * 0.74f, ch * 0.78f);
    for (int i = 0; i < n; ++i)
    {
        const int row = i / cols, inRow = juce::jmin (cols, n - row * cols);
        const float x0 = area.getX() + (float) (cols - inRow) * cw * 0.5f;
        miniKnob (g, { x0 + cw * ((float) (i % cols) + 0.5f), area.getY() + ch * ((float) row + 0.5f) }, d, ks, i + n);
    }
}

// lettering printed on the enclosure: a hairline shadow makes it look silk-screened
static void printText (juce::Graphics& g, const juce::String& text, Rectangle<float> r, Colour ink, float h,
                       bool italic = false, int lines = 2)
{
    if (r.getHeight() < 6.0f) return;
    const auto f = juce::Font (juce::FontOptions (juce::jmin (h, r.getHeight() * 0.9f),
                                                  juce::Font::bold | (italic ? juce::Font::italic : 0)));
    g.setFont (f);
    const bool lightInk = ink.getPerceivedBrightness() > 0.5f;
    g.setColour ((lightInk ? juce::Colours::black : juce::Colours::white).withAlpha (lightInk ? 0.45f : 0.25f));
    g.drawFittedText (text, r.translated (0, lightInk ? 1.0f : -0.6f).toNearestInt(), juce::Justification::centred, lines, 0.7f);
    g.setColour (ink);
    g.drawFittedText (text, r.toNearestInt(), juce::Justification::centred, lines, 0.7f);
}

Mini drawMini (juce::Graphics& g, Rectangle<float> cell, const Style& s, const juce::String& name,
               int knobs, bool on, bool withName, float glow, float press, float lift)
{
    auto m = miniGeometry (cell, s);
    m.body = m.body.translated (0, -lift);
    m.footswitch = m.footswitch.translated (0, -lift);
    m.jackY -= lift;
    const auto& b = m.body;
    const auto path = bodyPath (b, s.shape);

    // shadow on the board (bigger and softer when lifted)
    for (int k = 3; k >= 1; --k)
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        g.fillPath (path, juce::AffineTransform::translation (0, 1.5f + lift * 0.8f + (float) k * 1.2f));
    }

    const auto base = s.finish == Finish::tolex ? Colour (0xff1b1918) : s.body;
    juce::ColourGradient grad (base.brighter (0.16f), b.getX(), b.getY(), base.darker (0.3f), b.getX(), b.getBottom(), false);
    if (s.shape == Shape::round)
        grad = juce::ColourGradient (base.brighter (0.35f), b.getCentreX() - b.getWidth() * 0.2f, b.getCentreY() - b.getHeight() * 0.25f,
                                     base.darker (0.45f), b.getRight(), b.getBottom(), true);
    g.setGradientFill (grad);
    g.fillPath (path);
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (path);
        if (s.finish == Finish::sparkle || s.finish == Finish::brushed || s.finish == Finish::hammered) drawFinish (g, b, s);
        // soft specular sheen across the top-left
        juce::ColourGradient sheen (juce::Colours::white.withAlpha (0.10f), b.getX(), b.getY(),
                                    juce::Colours::transparentWhite, b.getX() + b.getWidth() * 0.6f, b.getY() + b.getHeight() * 0.5f, false);
        g.setGradientFill (sheen);
        g.fillRect (b);
    }
    // bevel: dark outline + light top edge
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.strokePath (path, juce::PathStrokeType (1.0f));
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (b.withHeight (b.getHeight() * 0.25f).toNearestInt());
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.strokePath (bodyPath (b.reduced (1.0f), s.shape), juce::PathStrokeType (0.8f));
    }

    const Colour bandInk = s.panel.getPerceivedBrightness() > 0.55f ? Colour (0xff17171a) : Colour (0xfff1eee8);
    Rectangle<float> ledR;
    switch (s.shape)
    {
        case Shape::compact:
        {
            // control panel with up to 4 knobs in a row, LED, name, rubber treadle
            auto panel = b.withHeight (b.getHeight() * 0.27f).reduced (b.getWidth() * 0.07f, b.getHeight() * 0.03f).translated (0, 2);
            g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillRoundedRectangle (panel.translated (0, 1), 3.0f);
            g.setColour (s.panel); g.fillRoundedRectangle (panel, 3.0f);
            miniKnobs (g, panel.reduced (3, 2), knobs, s.knob, panel.getHeight() * 0.75f);
            const float ledD = juce::jlimit (4.0f, 6.0f, b.getWidth() * 0.075f);
            ledR = Rectangle<float> (ledD, ledD).withCentre ({ b.getCentreX(), panel.getBottom() + ledD + 1.5f });
            auto t = m.footswitch.translated (0, press * 1.2f);
            g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRoundedRectangle (t.translated (0, 2), 4.0f);
            juce::ColourGradient pad (Colour (0xff2c2d31), t.getX(), t.getY(), Colour (0xff141518), t.getX(), t.getBottom(), false);
            g.setGradientFill (pad); g.fillRoundedRectangle (t, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            for (float y = t.getY() + 5; y < t.getBottom() - 4; y += 3.5f)
                g.fillRect (t.getX() + 4, y, t.getWidth() - 8, 1.2f);
            if (withName)
                printText (g, name.toUpperCase(), Rectangle<float> (b.getX() + 3, ledR.getBottom() + 2, b.getWidth() - 6, m.footswitch.getY() - ledR.getBottom() - 4), s.ink, 10.0f);
            break;
        }
        case Shape::head:
        {
            const auto grille = b.reduced (4).withHeight (b.getHeight() * 0.48f);
            drawGrille (g, grille, Colour (0xff3a3226), false);
            if (withName)
            {
                const auto plate = grille.withSizeKeepingCentre (grille.getWidth() * 0.62f, grille.getHeight() * 0.5f);
                if (name == "Toneical")
                    drawLogoBadge (g, plate, name, 2.0f);
                else
                {
                    juce::ColourGradient pg (Colour (0xfff1e6c8), plate.getX(), plate.getY(), Colour (0xffa58e5c), plate.getX(), plate.getBottom(), false);
                    g.setGradientFill (pg); g.fillRoundedRectangle (plate, 2.0f);
                    printText (g, name, plate.reduced (2, 0), Colour (0xff2a2117), 9.5f, true, 1);
                }
            }
            auto panel = b.reduced (4).withTrimmedTop (b.getHeight() * 0.52f);
            juce::ColourGradient pg2 (s.panel.brighter (0.15f), panel.getX(), panel.getY(), s.panel.darker (0.2f), panel.getX(), panel.getBottom(), false);
            g.setGradientFill (pg2); g.fillRect (panel);
            miniKnobs (g, panel.withTrimmedRight (24).reduced (3, 3), juce::jmin (knobs, 6), s.knob, 9.0f);
            drawBatToggle (g, m.footswitch.reduced (0, 2), on);
            drawJewel (g, Rectangle<float> (5, 5).withCentre ({ m.footswitch.getX() - 6, panel.getY() + 7 }), on, glow, Colour (0xffff8a2a));
            ledR = {};
            break;
        }
        case Shape::cab:
        {
            auto panel = b.reduced (4).withHeight (20);
            g.setColour (s.panel); g.fillRect (panel);
            drawGrille (g, b.reduced (4).withTrimmedTop (22), Colour (0xff2f2a24), true);
            drawRocker (g, m.footswitch.withSizeKeepingCentre (9.0f, 14.0f), on, glow);
            ledR = {};
            if (withName) printText (g, name, panel.withTrimmedRight (24).reduced (4, 0), Colour (0xffe8dcc0), 9.0f, true, 1);
            break;
        }
        case Shape::digital:
        {
            auto scr = b.reduced (b.getWidth() * 0.1f, 0).withY (b.getY() + b.getHeight() * 0.08f).withHeight (b.getHeight() * 0.3f);
            g.setColour (juce::Colours::black); g.fillRoundedRectangle (scr.expanded (1.5f), 3.0f);
            juce::ColourGradient lcd (Colour (0xff12291f), scr.getX(), scr.getY(), Colour (0xff07120d), scr.getX(), scr.getBottom(), false);
            g.setGradientFill (lcd); g.fillRoundedRectangle (scr, 2.5f);
            if (withName)
            {
                g.setColour (Colour (0xff59e3a2).withAlpha (on ? 0.95f : 0.35f));
                g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold)));
                g.drawFittedText (name, scr.reduced (3).toNearestInt(), juce::Justification::centred, 2, 0.7f);
            }
            const float kTop = scr.getBottom() + b.getHeight() * 0.06f;
            miniKnobs (g, Rectangle<float> (b.getX() + 6, kTop, b.getWidth() - 12, m.footswitch.getY() - kTop - b.getHeight() * 0.06f),
                       knobs, s.knob, 11.0f);
            drawSoftSwitch (g, m.footswitch.translated (0, press));
            ledR = Rectangle<float> (5, 5).withCentre ({ m.footswitch.getRight() + 7, m.footswitch.getCentreY() });
            break;
        }
        case Shape::round:
        {
            miniKnobs (g, Rectangle<float> (b.getX() + b.getWidth() * 0.24f, b.getY() + b.getHeight() * 0.13f, b.getWidth() * 0.52f, b.getHeight() * 0.22f),
                       knobs, s.knob, 13.0f);
            if (withName) printText (g, name, Rectangle<float> (b.getX() + 10, b.getY() + b.getHeight() * 0.38f, b.getWidth() - 20, b.getHeight() * 0.2f), s.ink, 11.0f, true);
            drawStompSwitch (g, m.footswitch, press);
            ledR = Rectangle<float> (5, 5).withCentre ({ b.getCentreX(), b.getY() + b.getHeight() * 0.07f });
            break;
        }
        default:   // stomp, slim, wedge, wide: knobs | LED | name | footswitch - separate zones, never overlapping
        {
            const bool fader = s.knob == KnobStyle::fader;
            const float top = b.getY() + b.getHeight() * 0.06f;
            const int rows = fader ? 1 : (knobs <= 4 ? 1 : 2);
            const float zoneH = fader ? b.getHeight() * 0.34f : b.getHeight() * (rows == 1 ? 0.2f : 0.32f);
            auto ka = Rectangle<float> (b.getX() + b.getWidth() * 0.08f, top, b.getWidth() * 0.84f, zoneH);
            if (s.shape == Shape::wedge)
            {
                // the sloped face carries the knobs
                const auto face = b.withHeight (b.getHeight() * 0.42f);
                ka = face.reduced (face.getWidth() * 0.08f, face.getHeight() * 0.14f);
            }
            const float maxD = s.shape == Shape::slim && knobs == 1 ? b.getWidth() * 0.42f : b.getWidth() * 0.2f;
            miniKnobs (g, ka, knobs, s.knob, maxD);

            const float ledD = juce::jlimit (4.0f, 6.0f, b.getWidth() * 0.065f);
            ledR = Rectangle<float> (ledD, ledD).withCentre ({ b.getCentreX(), ka.getBottom() + ledD * 0.9f + 1.0f });
            auto nameZone = Rectangle<float> (b.getX() + 4, ledR.getBottom() + 2, b.getWidth() - 8, m.footswitch.getY() - ledR.getBottom() - 4);
            if (withName)
            {
                if (s.nameBand)
                {
                    const auto band = nameZone.withSizeKeepingCentre (nameZone.getWidth(), juce::jmin (nameZone.getHeight(), 22.0f));
                    g.setColour (juce::Colours::black.withAlpha (0.3f)); g.fillRoundedRectangle (band.translated (0, 1), 2.5f);
                    g.setColour (s.panel); g.fillRoundedRectangle (band, 2.5f);
                    printText (g, name, band.reduced (3, 1), bandInk, 10.5f);
                }
                else
                    printText (g, name, nameZone, s.ink, 10.5f, s.shape == Shape::wedge);
            }
            drawStompSwitch (g, m.footswitch, press);
            break;
        }
    }
    if (! ledR.isEmpty()) drawLed (g, ledR, on, glow);

    if (! on)
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillPath (path);
    }
    return m;
}

juce::ScaledImage dragImage (juce::Rectangle<int> bounds, Rectangle<float> cell, const Style& s,
                             const juce::String& name, int knobs, bool on)
{
    constexpr float scale = 2.0f;
    juce::Image img (juce::Image::ARGB, juce::jmax (1, (int) (bounds.getWidth() * scale)),
                     juce::jmax (1, (int) (bounds.getHeight() * scale)), true);   // cleared = fully transparent
    {
        juce::Graphics g (img);
        g.addTransform (juce::AffineTransform::scale (scale));
        drawMini (g, cell, s, name, knobs, on, true);
    }
    return juce::ScaledImage (img, scale);
}

//==============================================================================
// Right-angle pancake plug seen from above: chrome shell, the cable leaves downwards.
void drawPlug (juce::Graphics& g, Point<float> jack, bool right)
{
    const float dir = right ? 1.0f : -1.0f;
    const float w = 11.0f, h = 20.0f;
    const float x = right ? jack.x + 1.0f : jack.x - 1.0f - w;
    const Rectangle<float> shell (x, jack.y - 8.0f, w, h);

    // tip going into the jack (barely visible) + nut on the pedal
    g.setColour (Colour (0xff3a3a3a));
    g.fillRoundedRectangle (right ? jack.x - 3.0f : jack.x - 1.0f, jack.y - 4.0f, 4.0f, 8.0f, 1.5f);

    // strain relief (ridged) below the shell
    const Rectangle<float> relief (shell.getCentreX() - 3.5f, shell.getBottom() - 1.0f, 7.0f, 8.0f);
    juce::ColourGradient rg (Colour (0xffcfcfcf), relief.getX(), 0, Colour (0xff6c6c6c), relief.getRight(), 0, false);
    g.setGradientFill (rg);
    g.fillRect (relief);
    g.setColour (Colour (0xff4a4a4a));
    for (float y = relief.getY() + 1.5f; y < relief.getBottom(); y += 2.0f)
        g.drawLine (relief.getX(), y, relief.getRight(), y, 0.7f);

    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (shell.translated (dir * 1.0f, 2.0f), 5.0f);
    juce::ColourGradient chrome (Colour (0xfff7f7f7), shell.getX(), shell.getY(), Colour (0xff616161), shell.getRight(), shell.getBottom(), false);
    chrome.addColour (0.45, Colour (0xffb9b9b9));
    chrome.addColour (0.55, Colour (0xffeeeeee));
    g.setGradientFill (chrome);
    g.fillRoundedRectangle (shell, 5.0f);
    g.setColour (Colour (0xff4d4d4d));
    g.drawRoundedRectangle (shell, 5.0f, 0.8f);
    // the screw on the cap
    g.setColour (Colour (0xff8a8a8a));
    g.fillEllipse (shell.getCentreX() - 2.0f, shell.getY() + 4.0f, 4.0f, 4.0f);
}

void drawCable (juce::Graphics& g, Point<float> a, bool aRight, Point<float> b, bool bRight, float floorY)
{
    auto exitOf = [] (Point<float> j, bool right) { return Point<float> (right ? j.x + 6.5f : j.x - 6.5f, j.y + 19.0f); };
    const auto p0 = exitOf (a, aRight), p1 = exitOf (b, bRight);
    const float low = juce::jmax (floorY, juce::jmax (p0.y, p1.y) + 8.0f);
    juce::Path path;
    path.startNewSubPath (p0);
    path.cubicTo (p0.x, low + 6.0f, p1.x, low + 6.0f, p1.x, p1.y);
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.strokePath (path, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (1.5f, 3.0f));
    g.setColour (Colour (0xff111112));
    g.strokePath (path, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.16f));
    g.strokePath (path, juce::PathStrokeType (1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::translation (-1.2f, 0.0f));
}

void drawJackPlate (juce::Graphics& g, Rectangle<float> r, const juce::String& text)
{
    juce::ColourGradient m (Colour (0xff4a4d55), r.getX(), r.getY(), Colour (0xff23252a), r.getX(), r.getBottom(), false);
    g.setGradientFill (m);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
    g.setColour (Colour (0xff9aa0ab));
    g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
    g.drawText (text, r.withTrimmedTop (r.getHeight() - 16), juce::Justification::centred);
}

} // namespace pg::PedalArt
