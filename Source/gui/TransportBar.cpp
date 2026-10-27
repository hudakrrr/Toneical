// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TransportBar.h"

namespace pg
{
namespace
{
    const juce::Colour recRed { 0xffe0453a };
}

//==============================================================================
IconButton::IconButton (Icon i, const juce::String& tip) : juce::Button (tip), icon (i)
{
    setTooltip (tip);
    setWantsKeyboardFocus (false);   // Space must reach the transport, not "click" the focused button
}

void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool on = getToggleState();
    g.setColour (juce::Colours::black.withAlpha (on ? 0.55f : 0.35f));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.12f : 0.05f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    if (on)
    {
        g.setColour (lit.withAlpha (0.8f));
        g.fillRoundedRectangle (r.getX() + 6.0f, r.getBottom() - 3.0f, r.getWidth() - 12.0f, 2.0f, 1.0f);
    }

    auto col = on ? lit : Colours::text.withAlpha (isEnabled() ? 0.85f : 0.3f);
    if (icon == Icon::record) col = on ? recRed : recRed.withAlpha (0.75f);
    if (down) col = col.brighter (0.3f);
    g.setColour (col);

    const float s = std::min (r.getWidth(), r.getHeight()) * 0.42f;
    const auto c = r.getCentre().translated (0, down ? 1.0f : 0.0f);
    juce::Path p;
    switch (icon)
    {
        case Icon::record:
            g.fillEllipse (c.x - s * 0.55f, c.y - s * 0.55f, s * 1.1f, s * 1.1f);
            if (on)
            {
                g.setColour (recRed.withAlpha (0.25f));
                g.fillEllipse (c.x - s * 0.9f, c.y - s * 0.9f, s * 1.8f, s * 1.8f);
            }
            return;
        case Icon::play:
            p.addTriangle (c.x - s * 0.4f, c.y - s * 0.55f, c.x - s * 0.4f, c.y + s * 0.55f, c.x + s * 0.55f, c.y);
            break;
        case Icon::stop:
            p.addRoundedRectangle (c.x - s * 0.45f, c.y - s * 0.45f, s * 0.9f, s * 0.9f, 2.0f);
            break;
        case Icon::rewind:
            p.addRectangle (c.x - s * 0.7f, c.y - s * 0.5f, s * 0.14f, s);
            p.addTriangle (c.x - s * 0.5f, c.y, c.x, c.y - s * 0.5f, c.x, c.y + s * 0.5f);
            p.addTriangle (c.x, c.y, c.x + s * 0.5f, c.y - s * 0.5f, c.x + s * 0.5f, c.y + s * 0.5f);
            break;
        case Icon::loop:
        {
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, s * 0.6f, s * 0.45f, 0, 0.5f, juce::MathConstants<float>::twoPi - 0.2f, true);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            const auto tip = arc.getCurrentPosition();
            p.addTriangle (tip.x - 4.0f, tip.y - 4.5f, tip.x - 4.0f, tip.y + 4.5f, tip.x + 3.0f, tip.y);
            break;
        }
        case Icon::metronome:
            p.startNewSubPath (c.x - s * 0.5f, c.y + s * 0.6f);
            p.lineTo (c.x - s * 0.18f, c.y - s * 0.65f);
            p.lineTo (c.x + s * 0.18f, c.y - s * 0.65f);
            p.lineTo (c.x + s * 0.5f, c.y + s * 0.6f);
            p.closeSubPath();
            g.strokePath (p, juce::PathStrokeType (1.6f, juce::PathStrokeType::mitered));
            g.drawLine (c.x, c.y + s * 0.3f, c.x + s * 0.45f, c.y - s * 0.55f, 1.8f);
            return;
        case Icon::rig:
        {
            const auto body = juce::Rectangle<float> (s * 1.1f, s * 1.4f).withCentre (c);
            g.drawRoundedRectangle (body, 2.5f, 1.6f);
            g.fillEllipse (body.getX() + body.getWidth() * 0.18f, body.getY() + 4.0f, 4.0f, 4.0f);
            g.fillEllipse (body.getRight() - body.getWidth() * 0.18f - 4.0f, body.getY() + 4.0f, 4.0f, 4.0f);
            g.fillEllipse (c.x - 3.5f, body.getBottom() - 10.0f, 7.0f, 7.0f);
            return;
        }
        case Icon::tracks:
            for (int i = 0; i < 3; ++i)
            {
                const float y = c.y - s * 0.55f + (float) i * s * 0.55f;
                const float w = s * (i == 1 ? 1.5f : (i == 0 ? 1.1f : 1.3f));
                p.addRoundedRectangle (c.x - s * 0.75f, y - 2.0f, w, 4.0f, 1.5f);
            }
            break;
    }
    g.fillPath (p);
}

//==============================================================================
// The LCD: bars:beats:ticks (click to show minutes:seconds instead).
class TransportBar::TimeDisplay final : public juce::Component,
                                        public juce::SettableTooltipClient
{
public:
    explicit TimeDisplay (rec::Session& s) : session (s)
    {
        setTooltip ("Playhead position - click to switch between bars:beats and minutes:seconds");
    }

    void mouseUp (const juce::MouseEvent& e) override { if (! e.mouseWasDraggedSinceMouseDown()) { showTime = ! showTime; repaint(); } }

    void update()
    {
        const auto p = session.getPosition();
        const bool counting = session.isCountingIn();
        if (p != lastPos || counting != lastCounting) { lastPos = p; lastCounting = counting; repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRoundedRectangle (r, 5.0f);
        auto scr = r.reduced (2.0f);
        juce::ColourGradient lcd (juce::Colour (0xff10161c), scr.getX(), scr.getY(), juce::Colour (0xff07090c), scr.getX(), scr.getBottom(), false);
        g.setGradientFill (lcd);
        g.fillRoundedRectangle (scr, 4.0f);

        const double sr = session.getSampleRate();
        const auto pos = session.getPosition();
        const double beats = (double) pos / session.samplesPerBeat();
        const int bpb = session.getBeatsPerBar();
        const auto bar = (juce::int64) std::floor (beats / bpb) + 1;
        const int beat = (int) std::fmod (std::floor (beats), (double) bpb) + 1;
        const int ticks = (int) ((beats - std::floor (beats)) * 960.0);
        const juce::String bbt = juce::String (bar).paddedLeft ('0', 3) + ":" + juce::String (beat) + ":" + juce::String (ticks).paddedLeft ('0', 3);
        const double secs = (double) pos / sr;
        const juce::String clock = juce::String ((int) secs / 60).paddedLeft ('0', 2) + ":"
                                   + juce::String (std::fmod (secs, 60.0), 3).paddedLeft ('0', 6);

        auto main = scr.reduced (8.0f, 2.0f);
        auto sub = main.removeFromBottom (12.0f);
        g.setColour (Colours::text);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold)));
        g.drawFittedText (lastCounting ? juce::String ("COUNT-IN") : (showTime ? clock : bbt), main.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        g.setColour (Colours::dim);
        g.setFont (uiFont (10.0f));
        g.drawFittedText (showTime ? "bar " + bbt : clock, sub.toNearestInt(), juce::Justification::centred, 1, 0.8f);
    }

private:
    rec::Session& session;
    bool showTime = false, lastCounting = false;
    juce::int64 lastPos = -1;
};

//==============================================================================
TransportBar::TransportBar (ToneicalProcessor& p) : proc (p), session (p.getSession())
{
    for (auto* b : { &rigMode, &recMode })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    rigMode.onClick = [this] { setRecordMode (false); if (onModeChange) onModeChange (false); };
    recMode.onClick = [this] { setRecordMode (true);  if (onModeChange) onModeChange (true); };

    recButton.onClick    = [this] { proc.toggleRecord(); refresh(); };
    playButton.onClick   = [this] { session.togglePlay(); refresh(); };
    rewindButton.onClick = [this] { session.setPosition (session.isLooping() && session.getLoopEnd() > session.getLoopStart() ? session.getLoopStart() : 0); };
    loopButton.onClick   = [this] { session.setLoop (! session.isLooping()); refresh(); };
    clickButton.onClick  = [this] { session.setMetronome (! session.isMetronomeOn()); refresh(); };
    for (auto* b : { &recButton, &playButton, &rewindButton, &loopButton, &clickButton })
        addAndMakeVisible (*b);

    countInButton.setTooltip ("Count in one bar before recording starts");
    countInButton.setWantsKeyboardFocus (false);
    countInButton.onClick = [this] { session.setCountIn (! session.isCountInOn()); refresh(); };
    addAndMakeVisible (countInButton);

    const juce::StringArray sigs { "2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "12/8" };
    for (int i = 0; i < sigs.size(); ++i) sigBox.addItem (sigs[i], i + 1);
    sigBox.setTooltip ("Time signature");
    sigBox.setWantsKeyboardFocus (false);
    sigBox.onChange = [this]
    {
        const auto t = sigBox.getText();
        session.setTimeSignature (t.upToFirstOccurrenceOf ("/", false, false).getIntValue(),
                                  t.fromFirstOccurrenceOf ("/", false, false).getIntValue());
    };
    addAndMakeVisible (sigBox);

    bpmSlider.setRange (30.0, 300.0, 0.1);
    bpmSlider.setTooltip ("Tempo - drag up/down, double-click to type it");
    bpmSlider.setColour (juce::Slider::trackColourId, Colours::accent.withAlpha (0.12f));
    bpmSlider.setColour (juce::Slider::backgroundColourId, juce::Colours::black.withAlpha (0.4f));
    bpmSlider.setColour (juce::Slider::textBoxTextColourId, Colours::text);
    bpmSlider.setTextValueSuffix (" bpm");
    bpmSlider.setNumDecimalPlacesToDisplay (1);
    bpmSlider.setMouseDragSensitivity (600);
    bpmSlider.setVelocityBasedMode (false);
    bpmSlider.onValueChange = [this] { session.setBpm (bpmSlider.getValue()); };
    addAndMakeVisible (bpmSlider);

    tapButton.setTooltip ("Tap tempo: click along with the beat");
    tapButton.setWantsKeyboardFocus (false);
    tapButton.onClick = [this] { tap(); };
    addAndMakeVisible (tapButton);

    clickVol.setRange (0.0, 1.0);
    clickVol.setDoubleClickReturnValue (true, 0.5);
    clickVol.setTooltip ("Metronome volume");
    clickVol.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::panel);
    clickVol.onValueChange = [this] { session.setClickLevel ((float) clickVol.getValue()); };
    addAndMakeVisible (clickVol);

    display = std::make_unique<TimeDisplay> (session);
    addAndMakeVisible (*display);

    refresh();
    startTimerHz (30);
}

TransportBar::~TransportBar() { stopTimer(); }

void TransportBar::setRecordMode (bool r)
{
    recordMode = r;
    rigMode.setToggleState (! r, juce::dontSendNotification);
    recMode.setToggleState (r, juce::dontSendNotification);
}

bool TransportBar::handleKey (const juce::KeyPress& k)
{
    if (k.getModifiers().isCommandDown() || k.getModifiers().isAltDown()) return false;
    if (k == juce::KeyPress::spaceKey)              { playButton.triggerClick(); return true; }
    if (k == juce::KeyPress::homeKey)               { rewindButton.triggerClick(); return true; }
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    if (c == 'r') { recButton.triggerClick(); return true; }
    if (c == 'l') { loopButton.triggerClick(); return true; }
    if (c == 'c') { clickButton.triggerClick(); return true; }
    return false;
}

void TransportBar::tap()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (! taps.empty() && now - taps.back() > 2.0) taps.clear();
    taps.push_back (now);
    if (taps.size() > 5) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
        bpmSlider.setValue (std::round (60.0 / avg * 10.0) / 10.0);
    }
}

void TransportBar::refresh()
{
    const bool playing = session.isPlaying() || session.isCountingIn();
    playButton.setIcon (playing ? IconButton::Icon::stop : IconButton::Icon::play);
    playButton.setToggleState (playing, juce::dontSendNotification);
    playButton.setLit (Colours::good);
    const bool rec = session.isRecording();
    recButton.setToggleState (rec && (! session.isCountingIn() || blink < 8), juce::dontSendNotification);
    loopButton.setToggleState (session.isLooping(), juce::dontSendNotification);
    clickButton.setToggleState (session.isMetronomeOn(), juce::dontSendNotification);
    countInButton.setToggleState (session.isCountInOn(), juce::dontSendNotification);
    if (! bpmSlider.isMouseButtonDown() && std::abs (bpmSlider.getValue() - session.getBpm()) > 0.01)
        bpmSlider.setValue (session.getBpm(), juce::dontSendNotification);
    if (std::abs (clickVol.getValue() - session.getClickLevel()) > 0.001)
        clickVol.setValue (session.getClickLevel(), juce::dontSendNotification);
    const auto sig = juce::String (session.getBeatsPerBar()) + "/" + juce::String (session.getBeatUnit());
    if (sigBox.getText() != sig) sigBox.setText (sig, juce::dontSendNotification);
}

void TransportBar::timerCallback()
{
    blink = (blink + 1) % 16;
    refresh();
    display->update();
}

void TransportBar::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    juce::ColourGradient bg (juce::Colour (0xff23262d), 0, r.getY(), juce::Colour (0xff17191e), 0, r.getBottom(), false);
    g.setGradientFill (bg);
    g.fillRect (r);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (r.getX(), r.getY(), r.getWidth(), 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.05f));
    g.fillRect (r.getX(), r.getY() + 1.0f, r.getWidth(), 1.0f);

    for (auto sec : { secMode, secTransport, secClick })
    {
        const auto m = sec.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.28f));
        g.fillRoundedRectangle (m, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.04f));
        g.drawRoundedRectangle (m.translated (0, 1.0f).reduced (0.5f), 6.0f, 1.0f);
    }
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (12, 5);
    const int bh = r.getHeight() - 8, bw = 44;

    secMode = r.removeFromLeft (2 * bw + 14);
    {
        auto m = secMode.reduced (4);
        rigMode.setBounds (m.removeFromLeft (bw));
        m.removeFromLeft (6);
        recMode.setBounds (m.removeFromLeft (bw));
    }

    secClick = r.removeFromRight (juce::jmin (430, r.getWidth() / 2 - 20));
    {
        auto m = secClick.reduced (4);
        clickButton.setBounds (m.removeFromLeft (bw));
        m.removeFromLeft (4);
        clickVol.setBounds (m.removeFromLeft (bh).reduced (2));
        m.removeFromLeft (6);
        countInButton.setBounds (m.removeFromLeft (80).withSizeKeepingCentre (80, 28));
        m.removeFromLeft (8);
        sigBox.setBounds (m.removeFromLeft (70).withSizeKeepingCentre (70, 28));
        m.removeFromLeft (8);
        tapButton.setBounds (m.removeFromRight (52).withSizeKeepingCentre (52, 28));
        m.removeFromRight (8);
        bpmSlider.setBounds (m.withSizeKeepingCentre (m.getWidth(), 28));
    }

    const int tw = 4 * bw + 3 * 6 + 12 + 190 + 8;
    secTransport = r.withSizeKeepingCentre (juce::jmin (tw, r.getWidth() - 20), r.getHeight());
    {
        auto m = secTransport.reduced (4);
        for (auto* b : { &recButton, &playButton, &rewindButton, &loopButton })
        {
            b->setBounds (m.removeFromLeft (bw));
            m.removeFromLeft (6);
        }
        m.removeFromLeft (6);
        display->setBounds (m);
    }
}

} // namespace pg
