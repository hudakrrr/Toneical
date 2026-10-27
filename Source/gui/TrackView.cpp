// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "TrackView.h"
#include "Presets.h"

namespace pg
{
using namespace rec;

namespace
{
    constexpr int clipTitleH = 15;
    const juce::Colour playheadRed { 0xffe0453a };
    const juce::uint32 palette[] = { 0xfff2a33a, 0xff3fb6c8, 0xff9b7be0, 0xffe0605a,
                                     0xff6cc36a, 0xff5a8fe0, 0xffe07fb8, 0xffc8c04a };

    // waveform height on a dB scale (-48 dB .. 0 dB), so a clean DI is still visible
    float displayLevel (float peak)
    {
        if (peak <= 0.0f) return 0.0f;
        return juce::jlimit (0.0f, 1.0f, (20.0f * std::log10 (peak) + 48.0f) / 48.0f);
    }

    float peakOf (const AudioData& d, juce::int64 a0, juce::int64 a1)
    {
        const auto n = d.numSamples();
        a0 = juce::jlimit<juce::int64> (0, n, a0);
        a1 = juce::jlimit<juce::int64> (a0, n, std::max (a1, a0 + 1));
        float p = 0.0f;
        if (a1 - a0 >= AudioData::kPeakBlock)
        {
            const auto b1 = std::min<juce::int64> ((juce::int64) d.peaks.size(), (a1 + AudioData::kPeakBlock - 1) / AudioData::kPeakBlock);
            for (auto b = a0 / AudioData::kPeakBlock; b < b1; ++b) p = std::max (p, d.peaks[(size_t) b]);
        }
        else
            for (int ch = 0; ch < d.buffer.getNumChannels(); ++ch)
            {
                const float* x = d.buffer.getReadPointer (ch);
                for (auto i = a0; i < a1; ++i) p = std::max (p, std::abs (x[i]));
            }
        return p;
    }

    void styleSmallButton (juce::Button& b)
    {
        b.setWantsKeyboardFocus (false);
        b.setMouseClickGrabsKeyboardFocus (false);
    }
}

//==============================================================================
// One track's header: name, R / M / S, tone, volume and pan.
class TrackView::Header final : public juce::Component
{
public:
    Header (TrackView& o, TrackPtr t) : track (std::move (t)), owner (o)
    {
        name.setFont (uiFont (13.0f, true));
        name.setEditable (false, true, false);
        name.setTooltip ("Double-click to rename");
        name.onTextChange = [this] { track->name = name.getText(); owner.session.touch(); };
        name.addMouseListener (this, false);
        addAndMakeVisible (name);

        auto setupToggle = [this] (juce::TextButton& b, juce::Colour on, const juce::String& tip)
        {
            styleSmallButton (b);
            b.setColour (juce::TextButton::buttonOnColourId, on);
            b.setTooltip (tip);
            addAndMakeVisible (b);
        };
        setupToggle (arm, playheadRed, "Record-arm: the next recording goes onto this track");
        setupToggle (mute, Colours::accent, "Mute");
        setupToggle (solo, Colours::good, "Solo");
        arm.onClick = [this]
        {
            const bool on = ! track->armed.load();
            for (auto& l : owner.arr) l.track->armed.store (false);
            track->armed.store (on);
        };
        mute.onClick = [this] { track->mute.store (! track->mute.load()); owner.session.touch(); };
        solo.onClick = [this] { track->solo.store (! track->solo.load()); owner.session.touch(); };

        styleSmallButton (toneButton);
        toneButton.setTooltip ("The track's sound. Recordings are stored clean (DI) and played back through this, "
                               "so you can change it any time.");
        toneButton.onClick = [this] { owner.showToneMenu (track, &toneButton); };
        addAndMakeVisible (toneButton);

        volume.setRange (-60.0, 6.0, 0.1);
        volume.setSkewFactorFromMidPoint (-12.0);
        volume.setDoubleClickReturnValue (true, 0.0);
        volume.setValue (juce::Decibels::gainToDecibels (track->volume.load(), -60.0f), juce::dontSendNotification);
        volume.onValueChange = [this]
        {
            track->volume.store (volume.getValue() <= -59.9 ? 0.0f : juce::Decibels::decibelsToGain ((float) volume.getValue()));
            volume.setTooltip ("Volume " + juce::String (volume.getValue(), 1) + " dB");
            owner.session.touch();
        };
        volume.setTooltip ("Volume " + juce::String (volume.getValue(), 1) + " dB");
        pan.setRange (-1.0, 1.0, 0.01);
        pan.setDoubleClickReturnValue (true, 0.0);
        pan.setValue (track->pan.load(), juce::dontSendNotification);
        pan.onValueChange = [this] { track->pan.store ((float) pan.getValue()); owner.session.touch(); };
        pan.setTooltip ("Pan (double-click: centre)");
        for (auto* s : { &volume, &pan })
        {
            s->setScrollWheelEnabled (false);
            s->setWantsKeyboardFocus (false);
            s->setColour (juce::Slider::trackColourId, track->colour.withAlpha (0.6f));
            addAndMakeVisible (*s);
        }
        refresh();
    }

    void refresh()
    {
        if (! name.isBeingEdited()) name.setText (track->name, juce::dontSendNotification);
        toneButton.setButtonText (track->useTone.load() ? track->toneName : juce::String ("Dry (no amp)"));
        for (auto* s : { &volume, &pan }) s->setColour (juce::Slider::trackColourId, track->colour.withAlpha (0.6f));
        repaint();
    }

    void rename() { name.showEditor(); }

    void update()
    {
        arm.setToggleState (track->armed.load(), juce::dontSendNotification);
        mute.setToggleState (track->mute.load(), juce::dontSendNotification);
        solo.setToggleState (track->solo.load(), juce::dontSendNotification);
        const float pk = track->meter.exchange (0.0f);
        const float next = std::max (displayLevel (pk), level - 0.04f);
        if (std::abs (next - level) > 0.003f) { level = next; repaint (meterArea()); }
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        const bool focused = owner.focusTrack == track;
        g.setColour (focused ? Colours::panel.brighter (0.12f) : Colours::panel);
        g.fillRect (r);
        if (track->armed.load())
        {
            g.setColour (playheadRed.withAlpha (0.08f));
            g.fillRect (r);
        }
        g.setColour (track->colour);
        g.fillRect (r.removeFromLeft (5.0f));
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (0.0f, (float) getHeight() - 2.0f, (float) getWidth(), 2.0f);
        g.fillRect ((float) getWidth() - 1.0f, 0.0f, 1.0f, (float) getHeight());

        const auto m = meterArea().toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRect (m);
        g.setColour (level > 0.94f ? Colours::warn : Colours::good.withAlpha (0.85f));
        g.fillRect (m.withWidth (m.getWidth() * level));

        if (showCaptions)
        {
            g.setColour (Colours::dim);
            g.setFont (uiFont (9.5f, true));
            g.drawText ("VOL", volCaption, juce::Justification::centredLeft);
            g.drawText ("PAN", panCaption, juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (9).withTrimmedRight (6).withTrimmedBottom (7).withTrimmedTop (5);
        auto row1 = r.removeFromTop (22);
        for (auto* b : { &solo, &mute, &arm })
        {
            b->setBounds (row1.removeFromRight (22));
            row1.removeFromRight (3);
        }
        name.setBounds (row1);
        r.removeFromTop (4);
        toneButton.setBounds (r.removeFromTop (22));
        r.removeFromTop (3);
        showCaptions = r.getHeight() >= 16;
        volume.setVisible (showCaptions);
        pan.setVisible (showCaptions);
        if (showCaptions)
        {
            auto row3 = r.removeFromTop (20);
            volCaption = row3.removeFromLeft (26);
            volume.setBounds (row3.removeFromLeft (row3.getWidth() * 6 / 10));
            row3.removeFromLeft (6);
            panCaption = row3.removeFromLeft (26);
            pan.setBounds (row3);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.focusTrack = track;
        owner.grabKeyboardFocus();
        owner.repaint();
        if (e.mods.isPopupMenu()) owner.showTrackMenu (track);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.eventComponent != &name)   // the name label's events arrive twice (listener + parent)
            owner.wheel (w, e, false);
    }

    const TrackPtr track;

private:
    juce::Rectangle<int> meterArea() const { return { 9, getHeight() - 6, getWidth() - 15, 3 }; }

    TrackView& owner;
    juce::Label name;
    juce::TextButton arm { "R" }, mute { "M" }, solo { "S" }, toneButton;
    juce::Slider volume { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox },
                 pan { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> volCaption, panCaption;
    bool showCaptions = true;
    float level = 0.0f;
};

//==============================================================================
// Bars & beats. Click: move the playhead; drag: set the loop range.
class TrackView::Ruler final : public juce::Component
{
public:
    explicit Ruler (TrackView& o) : owner (o) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        juce::ColourGradient bg (juce::Colour (0xff262a31), 0, 0, juce::Colour (0xff1b1e23), 0, r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRect (r);

        auto& s = owner.session;
        const auto ls = s.getLoopStart(), le = s.getLoopEnd();
        if (le > ls)
        {
            const float x0 = (float) owner.timeToX (ls), x1 = (float) owner.timeToX (le);
            g.setColour (Colours::accent.withAlpha (s.isLooping() ? 0.45f : 0.12f));
            g.fillRect (x0, 0.0f, x1 - x0, 9.0f);
            g.setColour (Colours::accent.withAlpha (s.isLooping() ? 0.9f : 0.3f));
            g.fillRect (x0, 0.0f, 1.5f, r.getHeight());
            g.fillRect (x1 - 1.5f, 0.0f, 1.5f, r.getHeight());
        }

        const double spb = s.samplesPerBeat();
        const int bpb = s.getBeatsPerBar();
        const double beatPx = spb / owner.spp, barPx = beatPx * bpb;
        int barStep = 1;
        while (barPx * barStep < 46.0) barStep *= 2;
        const auto firstBeat = (juce::int64) std::floor (owner.viewStart / spb);
        const auto lastBeat = (juce::int64) std::ceil ((owner.viewStart + getWidth() * owner.spp) / spb);
        g.setFont (uiFont (11.0f));
        for (auto b = firstBeat; b <= lastBeat; ++b)
        {
            const float x = (float) (((double) b * spb - owner.viewStart) / owner.spp);
            const bool isBar = b % bpb == 0;
            const auto bar = b / bpb;
            if (isBar && bar % barStep == 0)
            {
                g.setColour (Colours::dim.withAlpha (0.8f));
                g.fillRect (x, r.getHeight() - 12.0f, 1.0f, 12.0f);
                g.setColour (Colours::text.withAlpha (0.8f));
                g.drawText (juce::String (bar + 1), juce::Rectangle<float> (x + 4.0f, 9.0f, 40.0f, 14.0f), juce::Justification::centredLeft);
            }
            else if (beatPx >= 8.0)
            {
                g.setColour (Colours::dim.withAlpha (0.35f));
                g.fillRect (x, r.getHeight() - (isBar ? 8.0f : 5.0f), 1.0f, isBar ? 8.0f : 5.0f);
            }
        }

        const float px = (float) owner.timeToX (s.getPosition());
        juce::Path tri;
        tri.addTriangle (px - 6.0f, r.getHeight() - 10.0f, px + 6.0f, r.getHeight() - 10.0f, px, r.getHeight());
        g.setColour (playheadRed);
        g.fillPath (tri);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRect (0.0f, r.getHeight() - 1.0f, r.getWidth(), 1.0f);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        owner.grabKeyboardFocus();
        downTime = owner.snapTime (owner.xToTime (e.x));
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getDistanceFromDragStart() < 4) return;
        owner.session.setLoopRange (downTime, owner.snapTime (owner.xToTime (e.x)));
        owner.session.setLoop (true);
        owner.repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mouseWasDraggedSinceMouseDown())
            owner.session.setPosition (owner.snapTime (owner.xToTime (e.x)));
        owner.repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { owner.wheel (w, e, true); }

private:
    TrackView& owner;
    juce::int64 downTime = 0;
};

//==============================================================================
// The clips. Drag on a track: select a range; drag a clip's title bar: move it;
// drag a clip's edge: trim it; split tool: click to cut.
class TrackView::Lanes final : public juce::Component
{
public:
    explicit Lanes (TrackView& o) : owner (o) {}

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override { owner.wheel (w, e, true); }

private:
    enum class Zone { none, body, title, leftEdge, rightEdge };
    struct Hit { int lane = -1, clip = -1; Zone zone = Zone::none; };
    Hit hitAt (juce::Point<int>) const;
    void drawClip (juce::Graphics&, const Clip&, int laneIndex, juce::Colour, bool selected, float alpha);
    juce::Rectangle<float> clipRect (const Clip& c, int laneIndex) const
    {
        const float x0 = (float) owner.timeToX (c.start), x1 = (float) owner.timeToX (c.end());
        return { x0, (float) owner.laneY (laneIndex) + 3.0f, std::max (1.0f, x1 - x0), (float) owner.trackH - 7.0f };
    }

    TrackView& owner;
    Zone drag = Zone::none;
    int dragLane = -1, dragClip = -1, previewLane = -1, anchorLane = -1;
    juce::int64 anchorTime = 0, downTime = 0;
    Clip original, preview;
};

TrackView::Lanes::Hit TrackView::Lanes::hitAt (juce::Point<int> p) const
{
    Hit h;
    h.lane = owner.laneAt (p.y);
    if (h.lane < 0) return h;
    const auto& clips = owner.arr[(size_t) h.lane].clips;
    for (size_t i = 0; i < clips.size(); ++i)
    {
        const auto r = clipRect (clips[i], h.lane);
        if ((float) p.x < r.getX() - 4.0f || (float) p.x > r.getRight() + 4.0f) continue;
        h.clip = (int) i;
        const bool wide = r.getWidth() > 16.0f;
        if (wide && std::abs ((float) p.x - r.getX()) <= 5.0f)          h.zone = Zone::leftEdge;
        else if (wide && std::abs ((float) p.x - r.getRight()) <= 5.0f) h.zone = Zone::rightEdge;
        else if ((float) p.x < r.getX() || (float) p.x > r.getRight())  { h.clip = -1; continue; }
        else if ((float) p.y < r.getY() + clipTitleH)                   h.zone = Zone::title;
        else                                                            h.zone = Zone::body;
        return h;
    }
    return h;
}

void TrackView::Lanes::drawClip (juce::Graphics& g, const Clip& c, int laneIndex, juce::Colour col, bool selected, float alpha)
{
    auto r = clipRect (c, laneIndex);
    if (r.getRight() < 0 || r.getX() > (float) getWidth()) return;
    g.setColour (col.withAlpha ((selected ? 0.34f : 0.2f) * alpha));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (col.withAlpha ((selected ? 1.0f : 0.55f) * alpha));
    g.drawRoundedRectangle (r, 3.0f, selected ? 1.6f : 1.0f);

    auto title = r.removeFromTop ((float) clipTitleH);
    g.setColour (col.withAlpha ((selected ? 0.75f : 0.45f) * alpha));
    g.fillRoundedRectangle (title, 3.0f);
    if (title.getWidth() > 24.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.85f * alpha));
        g.setFont (uiFont (11.0f, true));
        auto txt = title.withTrimmedLeft (std::max (0.0f, -title.getX()) + 5.0f);
        g.drawText (c.name, txt.toNearestInt(), juce::Justification::centredLeft, true);
    }

    const auto& d = *c.audio;
    const float mid = r.getCentreY(), half = r.getHeight() * 0.5f - 2.0f;
    g.setColour (col.brighter (0.5f).withAlpha (0.9f * alpha));
    g.fillRect (std::max (0.0f, r.getX()), mid - 0.5f, std::min ((float) getWidth(), r.getRight()) - std::max (0.0f, r.getX()), 1.0f);
    const int px0 = std::max (0, (int) r.getX()), px1 = std::min (getWidth(), (int) std::ceil (r.getRight()));
    for (int px = px0; px < px1; ++px)
    {
        const double t0 = owner.viewStart + px * owner.spp;
        const auto a0 = c.offset + (juce::int64) (t0 - (double) c.start);
        const auto a1 = a0 + std::max<juce::int64> (1, (juce::int64) owner.spp);
        const auto lo = std::max (a0, c.offset), hi = std::min (a1, c.offset + c.length);
        if (hi <= lo) continue;
        const float h = displayLevel (peakOf (d, lo, hi)) * half;
        if (h > 0.5f) g.fillRect ((float) px, mid - h, 1.0f, h * 2.0f);
    }
}

void TrackView::Lanes::paint (juce::Graphics& g)
{
    auto& s = owner.session;
    g.fillAll (juce::Colour (0xff121418));
    const int n = (int) owner.arr.size();
    const int th = owner.trackH;

    // lane backgrounds
    for (int i = 0; i < n; ++i)
    {
        const int y = owner.laneY (i);
        if (y + th < 0 || y > getHeight()) continue;
        const bool focused = owner.arr[(size_t) i].track == owner.focusTrack;
        g.setColour (focused ? juce::Colour (0xff1c2026) : (i % 2 ? juce::Colour (0xff16191d) : juce::Colour (0xff181b20)));
        g.fillRect (0, y, getWidth(), th);
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.fillRect (0, y + th - 2, getWidth(), 2);
    }
    {
        const int y = owner.laneY (n);
        if (y < getHeight() && owner.pending.empty())
        {
            g.setColour (Colours::dim.withAlpha (0.45f));
            g.setFont (uiFont (13.0f));
            g.drawText ("Drop audio files here (backing tracks: wav, flac, ogg, mp3, aiff) to add a track",
                        juce::Rectangle<int> (12, y, getWidth() - 24, std::min (th, 60)), juce::Justification::centredLeft);
        }
    }

    // grid
    const double spb = s.samplesPerBeat();
    const int bpb = s.getBeatsPerBar();
    const double beatPx = spb / owner.spp;
    const int contentBottom = std::min (getHeight(), owner.laneY (n));
    const auto firstBeat = (juce::int64) std::floor (owner.viewStart / spb);
    const auto lastBeat = (juce::int64) std::ceil ((owner.viewStart + getWidth() * owner.spp) / spb);
    int barStep = 1;
    while (beatPx * bpb * barStep < 12.0) barStep *= 2;
    for (auto b = firstBeat; b <= lastBeat; ++b)
    {
        const bool isBar = b % bpb == 0;
        if (! isBar && beatPx < 10.0) continue;
        if (isBar && (b / bpb) % barStep != 0) continue;
        const float x = (float) (((double) b * spb - owner.viewStart) / owner.spp);
        g.setColour (juce::Colours::white.withAlpha (isBar ? 0.07f : 0.03f));
        g.fillRect (x, 0.0f, 1.0f, (float) contentBottom);
    }

    // loop range
    if (s.isLooping() && s.getLoopEnd() > s.getLoopStart())
    {
        const float x0 = (float) owner.timeToX (s.getLoopStart()), x1 = (float) owner.timeToX (s.getLoopEnd());
        g.setColour (Colours::accent.withAlpha (0.05f));
        g.fillRect (x0, 0.0f, x1 - x0, (float) contentBottom);
    }

    // clips
    const bool dragging = (drag == Zone::title || drag == Zone::leftEdge || drag == Zone::rightEdge) && isMouseButtonDown()
                          && dragLane >= 0 && dragLane < n;
    for (int i = 0; i < n; ++i)
    {
        const int y = owner.laneY (i);
        if (y + th < 0 || y > getHeight()) continue;
        const auto& lane = owner.arr[(size_t) i];
        for (size_t c = 0; c < lane.clips.size(); ++c)
        {
            if (dragging && i == dragLane && (int) c == dragClip) continue;
            const bool sel = owner.selClipTrack == lane.track && owner.selClipStart == lane.clips[c].start;
            drawClip (g, lane.clips[c], i, lane.track->colour, sel, 1.0f);
        }
    }
    if (dragging && previewLane >= 0 && previewLane < n)
        drawClip (g, preview, previewLane, owner.arr[(size_t) previewLane].track->colour, true, 0.85f);

    // the take being recorded
    if (auto rt = s.getRecordTrack(); rt != nullptr && s.getTakeLength() > 0)
        for (int i = 0; i < n; ++i)
            if (owner.arr[(size_t) i].track == rt)
            {
                const auto start = s.getTakeStart();
                const auto& peaks = s.getTakePeaks();
                const float x0 = (float) owner.timeToX (start);
                const float x1 = (float) owner.timeToX (start + (juce::int64) peaks.size() * AudioData::kPeakBlock);
                juce::Rectangle<float> r (x0, (float) owner.laneY (i) + 3.0f, std::max (1.0f, x1 - x0), (float) th - 7.0f);
                g.setColour (playheadRed.withAlpha (0.22f));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (playheadRed.withAlpha (0.8f));
                g.drawRoundedRectangle (r, 3.0f, 1.0f);
                g.setColour (playheadRed.brighter (0.4f));
                const float mid = r.getCentreY(), half = r.getHeight() * 0.5f - 2.0f;
                for (int px = std::max (0, (int) x0); px < std::min (getWidth(), (int) x1); ++px)
                {
                    const double t0 = owner.viewStart + px * owner.spp - (double) start;
                    auto b0 = (juce::int64) (t0 / AudioData::kPeakBlock);
                    auto b1 = std::max (b0 + 1, (juce::int64) ((t0 + owner.spp) / AudioData::kPeakBlock));
                    b0 = std::max<juce::int64> (0, b0);
                    b1 = std::min<juce::int64> ((juce::int64) peaks.size(), b1);
                    float p = 0.0f;
                    for (auto b = b0; b < b1; ++b) p = std::max (p, peaks[(size_t) b]);
                    const float h = displayLevel (p) * half;
                    if (h > 0.5f) g.fillRect ((float) px, mid - h, 1.0f, h * 2.0f);
                }
            }

    // time selection
    if (owner.selEnd > owner.selStart)
    {
        const float x0 = (float) owner.timeToX (owner.selStart), x1 = (float) owner.timeToX (owner.selEnd);
        for (int i = 0; i < n; ++i)
            if (std::find (owner.selTracks.begin(), owner.selTracks.end(), owner.arr[(size_t) i].track) != owner.selTracks.end())
            {
                g.setColour (juce::Colours::white.withAlpha (0.14f));
                g.fillRect (x0, (float) owner.laneY (i), x1 - x0, (float) th - 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.5f));
                g.fillRect (x0, (float) owner.laneY (i), 1.0f, (float) th - 2.0f);
                g.fillRect (x1 - 1.0f, (float) owner.laneY (i), 1.0f, (float) th - 2.0f);
            }
    }

    // file drop target, with what will happen
    if (owner.dropLane >= -1)
    {
        const int lane = owner.dropLane >= 0 ? owner.dropLane : n;
        g.setColour (Colours::accent.withAlpha (0.12f));
        g.fillRect (0, owner.laneY (lane), getWidth(), th);
        const float x = (float) owner.timeToX (owner.dropTime);
        g.setColour (Colours::accent);
        g.fillRect (x, (float) owner.laneY (lane), 2.0f, (float) th);
        const auto bar = (int) std::floor ((double) owner.dropTime / s.samplesPerBar()) + 1;
        const juce::String what = (owner.dragFileCount == 1 ? juce::String ("1 file") : juce::String (owner.dragFileCount) + " files")
                                  + "  ->  " + (owner.dropLane >= 0 ? owner.arr[(size_t) owner.dropLane].track->name : juce::String ("new track"))
                                  + ", bar " + juce::String (bar);
        g.setFont (uiFont (13.0f, true));
        const auto tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), what) + 16.0f;
        juce::Rectangle<float> label (x + 6.0f, (float) owner.laneY (lane) + 6.0f, tw, 22.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (label, 4.0f);
        g.setColour (Colours::accent);
        g.drawText (what, label, juce::Justification::centred);
    }

    // imports in flight: a box where each clip will land
    const auto running = s.getImportName();
    bool runningDrawn = false;
    int newRows = 0;
    for (auto& p : owner.pending)
    {
        const int lane = p.lane >= 0 && p.lane < n ? p.lane : n + newRows++;
        const bool isRunning = ! runningDrawn && p.name == running;
        runningDrawn = runningDrawn || isRunning;
        const float x = (float) owner.timeToX (p.time);
        juce::Rectangle<float> r (x, (float) owner.laneY (lane) + 3.0f, 220.0f, (float) th - 7.0f);
        g.setColour (Colours::accent.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 3.0f);
        juce::Path outline;
        outline.addRoundedRectangle (r, 3.0f);
        juce::Path dashed;
        const float dashes[] = { 5.0f, 4.0f };
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (Colours::accent.withAlpha (0.8f));
        g.fillPath (dashed);
        g.setFont (uiFont (12.0f, true));
        g.setColour (Colours::text);
        g.drawText (p.name, r.reduced (8.0f, 4.0f).removeFromTop (16.0f), juce::Justification::centredLeft, true);
        g.setColour (Colours::dim);
        g.setFont (uiFont (11.0f));
        const float prog = isRunning ? s.getImportProgress() : 0.0f;
        g.drawText (isRunning ? "Loading... " + juce::String ((int) (prog * 100.0f)) + "%" : juce::String ("Waiting..."),
                    r.reduced (8.0f, 4.0f).withTrimmedTop (16.0f).removeFromTop (16.0f), juce::Justification::centredLeft);
        auto bar = r.reduced (8.0f, 0.0f).withTop (r.getBottom() - 10.0f).withHeight (4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 2.0f);
    }

    // progress panel (also for imports started from the Import... button)
    if (! s.getImportNames().isEmpty())
    {
        const int count = s.getImportNames().size();
        const float prog = s.getImportProgress();
        juce::Rectangle<float> panel ((float) getWidth() * 0.5f - 190.0f, (float) getHeight() - 70.0f, 380.0f, 56.0f);
        g.setColour (juce::Colours::black.withAlpha (0.82f));
        g.fillRoundedRectangle (panel, 8.0f);
        g.setColour (Colours::accent.withAlpha (0.6f));
        g.drawRoundedRectangle (panel, 8.0f, 1.0f);
        auto in = panel.reduced (14.0f, 9.0f);
        g.setColour (Colours::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText ("Importing " + s.getImportName() + (count > 1 ? "  (+" + juce::String (count - 1) + " more)" : juce::String()),
                    in.removeFromTop (18.0f), juce::Justification::centredLeft, true);
        auto line = in.removeFromTop (20.0f);
        g.setColour (Colours::dim);
        g.setFont (uiFont (12.0f));
        g.drawText (juce::String ((int) (prog * 100.0f)) + "%", line.removeFromRight (40.0f), juce::Justification::centredRight);
        auto bar = line.withSizeKeepingCentre (line.getWidth() - 8.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (Colours::accent);
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * prog), 3.0f);
    }

    // playhead
    const float px = (float) owner.timeToX (s.getPosition());
    g.setColour (playheadRed);
    g.fillRect (px - 0.75f, 0.0f, 1.5f, (float) getHeight());
}

void TrackView::Lanes::mouseMove (const juce::MouseEvent& e)
{
    if (owner.tool == Tool::split) { setMouseCursor (juce::MouseCursor::CrosshairCursor); return; }
    switch (hitAt (e.getPosition()).zone)
    {
        case Zone::leftEdge:
        case Zone::rightEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Zone::title:     setMouseCursor (juce::MouseCursor::DraggingHandCursor); break;
        case Zone::none:
        case Zone::body:      setMouseCursor (juce::MouseCursor::IBeamCursor); break;
    }
}

void TrackView::Lanes::mouseDown (const juce::MouseEvent& e)
{
    owner.grabKeyboardFocus();
    drag = Zone::none;
    const auto hit = hitAt (e.getPosition());
    const auto t = owner.xToTime (e.x);
    if (hit.lane >= 0) owner.focusTrack = owner.arr[(size_t) hit.lane].track;

    if (e.mods.isPopupMenu())
    {
        owner.showLaneMenu (hit.lane, t);
        owner.repaint();
        return;
    }
    if (owner.tool == Tool::split)
    {
        if (hit.lane >= 0)
        {
            auto copy = owner.arr;
            if (copy[(size_t) hit.lane].splitAt (owner.snapTime (t))) owner.session.commit (std::move (copy));
        }
        return;
    }

    downTime = t;
    dragLane = hit.lane;
    dragClip = hit.clip;
    if (hit.clip >= 0 && hit.zone != Zone::body)
    {
        drag = hit.zone;
        original = preview = owner.arr[(size_t) hit.lane].clips[(size_t) hit.clip];
        previewLane = hit.lane;
        owner.selClipTrack = owner.arr[(size_t) hit.lane].track;
        owner.selClipStart = original.start;
        owner.selEnd = owner.selStart;   // a clip selection replaces the range selection
    }
    else
    {
        drag = Zone::body;
        anchorTime = owner.snapTime (t);
        anchorLane = hit.lane;
    }
    owner.repaint();
}

void TrackView::Lanes::mouseDrag (const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 3 || drag == Zone::none) return;
    const int n = (int) owner.arr.size();
    const auto t = owner.xToTime (e.x);

    if (drag == Zone::body)
    {
        if (anchorLane < 0) return;
        const auto t2 = owner.snapTime (t);
        owner.selStart = std::min (anchorTime, t2);
        owner.selEnd = std::max (anchorTime, t2);
        int lane = owner.laneAt (e.y);
        if (lane < 0) lane = e.y < 0 ? 0 : n - 1;
        owner.selTracks.clear();
        for (int i = std::min (lane, anchorLane); i <= std::max (lane, anchorLane); ++i)
            owner.selTracks.push_back (owner.arr[(size_t) i].track);
        owner.selClipTrack = nullptr;
    }
    else if (dragLane >= 0)
    {
        const juce::int64 minLen = std::max<juce::int64> (64, (juce::int64) (owner.spp * 4));
        preview = original;
        if (drag == Zone::title)
        {
            preview.start = std::max<juce::int64> (0, owner.snapTime (original.start + (t - downTime)));
            int lane = owner.laneAt (e.y);
            previewLane = lane >= 0 ? lane : (e.y < 0 ? 0 : n - 1);
        }
        else if (drag == Zone::leftEdge)
        {
            const auto s = juce::jlimit (original.start - original.offset, original.end() - minLen, owner.snapTime (t));
            preview.offset = original.offset + (s - original.start);
            preview.length = original.end() - s;
            preview.start = s;
        }
        else if (drag == Zone::rightEdge)
        {
            const auto end = juce::jlimit (original.start + minLen, original.start + (original.audio->numSamples() - original.offset),
                                           owner.snapTime (t));
            preview.length = end - original.start;
        }
    }
    owner.lanes->repaint();
}

void TrackView::Lanes::mouseUp (const juce::MouseEvent& e)
{
    const auto mode = drag;
    drag = Zone::none;
    if (mode == Zone::none) return;

    if (mode == Zone::body)
    {
        if (! e.mouseWasDraggedSinceMouseDown() || owner.selEnd == owner.selStart)
        {
            // a click: move the playhead here, this track becomes the target for split/paste
            owner.selEnd = owner.selStart;
            owner.selTracks.clear();
            owner.selClipTrack = nullptr;
            if (anchorLane >= 0) owner.selTracks.push_back (owner.arr[(size_t) anchorLane].track);
            if (! owner.session.isRecording())
                owner.session.setPosition (owner.snapTime (owner.xToTime (e.x)));
        }
        owner.repaint();
        return;
    }

    if (e.mouseWasDraggedSinceMouseDown() && dragLane >= 0 && dragLane < (int) owner.arr.size()
        && dragClip < (int) owner.arr[(size_t) dragLane].clips.size()
        && (preview.start != original.start || preview.length != original.length || previewLane != dragLane))
    {
        auto copy = owner.arr;
        auto& from = copy[(size_t) dragLane].clips;
        from.erase (from.begin() + dragClip);
        copy[(size_t) previewLane].insert (preview);
        owner.selClipTrack = copy[(size_t) previewLane].track;
        owner.selClipStart = preview.start;
        owner.session.commit (std::move (copy));
    }
    owner.repaint();
}

void TrackView::Lanes::mouseDoubleClick (const juce::MouseEvent& e)
{
    // double-click a clip: select its time range on that track
    const auto hit = hitAt (e.getPosition());
    if (hit.clip < 0 || owner.tool != Tool::select) return;
    const auto& c = owner.arr[(size_t) hit.lane].clips[(size_t) hit.clip];
    owner.selStart = c.start;
    owner.selEnd = c.end();
    owner.selTracks = { owner.arr[(size_t) hit.lane].track };
    owner.selClipTrack = nullptr;
    owner.repaint();
}

//==============================================================================
TrackView::TrackView (ToneicalProcessor& p) : proc (p), session (p.getSession())
{
    setWantsKeyboardFocus (true);

    selectTool.setTooltip ("Select tool: drag on a track to select a range, drag a clip's title bar to move it, its edges to trim it");
    splitTool.setTooltip ("Split tool: click on a clip to cut it in two (S splits at the selection / playhead)");
    for (auto* b : { &selectTool, &splitTool })
    {
        b->setRadioGroupId (1);
        b->setClickingTogglesState (true);
    }
    selectTool.setToggleState (true, juce::dontSendNotification);
    selectTool.onClick = [this] { tool = Tool::select; };
    splitTool.onClick  = [this] { tool = Tool::split; };
    snapButton.setClickingTogglesState (true);
    snapButton.setToggleState (snap, juce::dontSendNotification);
    snapButton.setTooltip ("Snap to the beat grid");
    snapButton.onClick = [this] { snap = snapButton.getToggleState(); };

    zoomOut.setTooltip ("Zoom out (-, Ctrl + wheel)");
    zoomIn.setTooltip ("Zoom in (+, Ctrl + wheel)");
    fitButton.setTooltip ("Show the whole project (F)");
    zoomOut.onClick = [this] { setZoom (spp * 1.5, lanes->getWidth() * 0.5); };
    zoomIn.onClick  = [this] { setZoom (spp / 1.5, lanes->getWidth() * 0.5); };
    fitButton.onClick = [this] { zoomToFit(); };
    zoomSlider.setRange (-16.0, 0.0);
    zoomSlider.setTooltip ("Zoom");
    zoomSlider.onValueChange = [this] { setZoom (std::pow (2.0, -zoomSlider.getValue()), lanes->getWidth() * 0.5); };
    heightSlider.setRange (minTrackH, maxTrackH, 1.0);
    heightSlider.setValue (trackH, juce::dontSendNotification);
    heightSlider.setTooltip ("Track height (Alt + wheel)");
    heightSlider.onValueChange = [this] { setTrackHeight ((int) heightSlider.getValue()); };

    undoButton.setTooltip ("Undo (Ctrl+Z)");
    redoButton.setTooltip ("Redo (Ctrl+Shift+Z)");
    undoButton.onClick = [this] { session.undo(); };
    redoButton.onClick = [this] { session.redo(); };
    addTrackButton.onClick = [this] { focusTrack = session.addTrack(); };
    importButton.setTooltip ("Add audio files (backing tracks) - or just drag them onto the tracks");
    importButton.onClick = [this] { importDialog(); };
    exportButton.setTooltip ("Export the mix as WAV or MP3 (Ctrl+E) - what you hear: mute, solo, volume, pan and track tones");
    exportButton.onClick = [this]
    {
        if (session.isExporting()) session.cancelExport();
        else exportDialog();
    };
    newButton.onClick    = [this] { newProject(); };
    openButton.onClick   = [this] { openProject(); };
    saveButton.onClick   = [this] { saveProject (false); };
    saveAsButton.onClick = [this] { saveProject (true); };
    saveButton.setTooltip ("Save the project (Ctrl+S): tracks, tones and audio go into a project folder");

    for (auto* b : { &selectTool, &splitTool, &snapButton, &zoomOut, &zoomIn, &fitButton, &undoButton, &redoButton,
                     &addTrackButton, &importButton, &exportButton, &newButton, &openButton, &saveButton, &saveAsButton })
    {
        styleSmallButton (*b);
        addAndMakeVisible (*b);
    }
    for (auto* s : { &zoomSlider, &heightSlider })
    {
        s->setWantsKeyboardFocus (false);
        s->setColour (juce::Slider::trackColourId, Colours::dim.withAlpha (0.5f));
        addAndMakeVisible (*s);
    }

    ruler = std::make_unique<Ruler> (*this);
    lanes = std::make_unique<Lanes> (*this);
    addAndMakeVisible (*ruler);
    addAndMakeVisible (*lanes);
    addAndMakeVisible (headerHolder);
    for (auto* sb : { &hbar, &vbar })
    {
        sb->setAutoHide (false);
        sb->addListener (this);
        addAndMakeVisible (*sb);
    }

    session.addChangeListener (this);
    arr = session.getArrangement();
    syncHeaders();
    zoomSlider.setValue (-std::log2 (spp), juce::dontSendNotification);
    startTimerHz (30);
}

TrackView::~TrackView()
{
    stopTimer();
    session.removeChangeListener (this);
}

//==============================================================================
juce::int64 TrackView::snapTime (juce::int64 t) const
{
    if (! snap) return t;
    double unit = session.samplesPerBeat();
    if (unit / spp < 10.0) unit *= session.getBeatsPerBar();
    while (unit / spp < 10.0) unit *= 2.0;
    return std::max<juce::int64> (0, (juce::int64) (std::round ((double) t / unit) * unit));
}

int TrackView::laneAt (int y) const
{
    if (y + scrollY < 0) return -1;
    const int i = (y + scrollY) / trackH;
    return i < (int) arr.size() ? i : -1;
}

void TrackView::setZoom (double newSpp, double anchorX)
{
    newSpp = juce::jlimit (1.0, 65536.0, newSpp);
    const double anchorT = viewStart + anchorX * spp;
    spp = newSpp;
    viewStart = std::max (0.0, anchorT - anchorX * spp);
    zoomSlider.setValue (-std::log2 (spp), juce::dontSendNotification);
    updateScrollBars();
    ruler->repaint();
    lanes->repaint();
}

void TrackView::zoomToFit()
{
    const double len = std::max ((double) session.getLength(), session.getSampleRate() * 20.0);
    viewStart = 0;
    setZoom (len * 1.05 / std::max (100, lanes->getWidth()), 0.0);
}

void TrackView::setTrackHeight (int h)
{
    h = juce::jlimit ((int) minTrackH, (int) maxTrackH, h);
    if (h == trackH) return;
    trackH = h;
    heightSlider.setValue (h, juce::dontSendNotification);
    syncHeaders();
    updateScrollBars();
    lanes->repaint();
}

void TrackView::updateScrollBars()
{
    const double sr = session.getSampleRate();
    const double visible = std::max (1, lanes->getWidth()) * spp;
    const double len = std::max ((double) session.getLength(), sr * 60.0) + sr * 30.0;
    hbar.setRangeLimits (0.0, std::max (len, viewStart + visible), juce::dontSendNotification);
    hbar.setCurrentRange (viewStart, visible, juce::dontSendNotification);

    const int contentH = ((int) arr.size() + 1) * trackH;
    const int viewH = std::max (1, lanes->getHeight());
    scrollY = juce::jlimit (0, std::max (0, contentH - viewH), scrollY);
    vbar.setRangeLimits (0.0, std::max (contentH, viewH), juce::dontSendNotification);
    vbar.setCurrentRange (scrollY, viewH, juce::dontSendNotification);
}

void TrackView::scrollBarMoved (juce::ScrollBar* sb, double start)
{
    if (sb == &hbar) viewStart = std::max (0.0, start);
    else { scrollY = (int) start; syncHeaders(); }
    ruler->repaint();
    lanes->repaint();
}

void TrackView::wheel (const juce::MouseWheelDetails& w, const juce::MouseEvent& e, bool overTimeline)
{
    const float dy = w.deltaY, dx = w.deltaX;
    if (e.mods.isCommandDown())
    {
        const double anchor = overTimeline ? lanes->getLocalPoint (e.eventComponent, e.position).x : lanes->getWidth() * 0.5;
        if (! juce::exactlyEqual (dy, 0.0f)) setZoom (spp * (dy > 0 ? 0.8 : 1.25), anchor);
    }
    else if (e.mods.isAltDown())
    {
        if (! juce::exactlyEqual (dy, 0.0f)) setTrackHeight (trackH + (dy > 0 ? 10 : -10));
    }
    else if (e.mods.isShiftDown() || std::abs (dx) > std::abs (dy))
    {
        const float d = std::abs (dx) > std::abs (dy) ? dx : dy;
        viewStart = std::max (0.0, viewStart - d * lanes->getWidth() * 0.5 * spp);
        updateScrollBars();
        ruler->repaint();
        lanes->repaint();
    }
    else
    {
        scrollY -= (int) (dy * 300.0f);
        updateScrollBars();
        syncHeaders();
        lanes->repaint();
    }
}

void TrackView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { wheel (w, e, false); }

void TrackView::syncHeaders()
{
    bool same = headers.size() == arr.size();
    for (size_t i = 0; same && i < arr.size(); ++i) same = headers[i]->track == arr[i].track;
    if (! same)
    {
        headers.clear();
        for (auto& lane : arr)
        {
            headers.push_back (std::make_unique<Header> (*this, lane.track));
            headerHolder.addAndMakeVisible (*headers.back());
        }
    }
    for (size_t i = 0; i < headers.size(); ++i)
    {
        headers[i]->setBounds (0, laneY ((int) i), headerHolder.getWidth(), trackH);
        headers[i]->refresh();
    }
}

//==============================================================================
void TrackView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    arr = session.getArrangement();
    // drop references to tracks that are gone
    selTracks.erase (std::remove_if (selTracks.begin(), selTracks.end(), [this] (const TrackPtr& t)
                     { return std::none_of (arr.begin(), arr.end(), [&] (const Lane& l) { return l.track == t; }); }),
                     selTracks.end());
    syncHeaders();
    updateScrollBars();
    repaint();
}

void TrackView::timerCallback()
{
    for (auto& h : headers) h->update();
    undoButton.setEnabled (session.canUndo());
    redoButton.setEnabled (session.canRedo());

    const auto pos = session.getPosition();
    if (pos != lastPlayhead || session.isRecording())
    {
        const int w = lanes->getWidth();
        if ((session.isPlaying() || session.isRecording()) && w > 0)
        {
            // page along with the playhead
            const double x = timeToX (pos);
            if (x > w * 0.95 || x < 0)
            {
                viewStart = std::max (0.0, (double) pos - w * 0.05 * spp);
                updateScrollBars();
                ruler->repaint();
                lanes->repaint();
            }
        }
        for (auto t : { lastPlayhead, pos })
        {
            const int x = (int) timeToX (t);
            lanes->repaint (x - 3, 0, 6, lanes->getHeight());
            ruler->repaint (x - 8, 0, 16, ruler->getHeight());
        }
        if (auto rt = session.getRecordTrack())
            for (size_t i = 0; i < arr.size(); ++i)
                if (arr[i].track == rt) lanes->repaint (0, laneY ((int) i), lanes->getWidth(), trackH);
        lastPlayhead = pos;
    }
    if (++tick % 15 == 0) repaint (titleArea);

    if (! pending.empty() || ! session.getImportNames().isEmpty())
    {
        prunePending();
        if (tick % 2 == 0) lanes->repaint();
    }

    if (session.isExporting())
    {
        if (tick % 10 == 0)
            status ("Exporting the mix... " + juce::String ((int) (session.getExportProgress() * 100.0f)) + "%   (click Cancel export to stop)");
        if (! wasExporting) exportButton.setButtonText ("Cancel export");
        wasExporting = true;
    }
    else if (wasExporting)
    {
        exportButton.setButtonText ("Export...");
        wasExporting = false;
    }
}

//==============================================================================
std::vector<int> TrackView::targetLanes() const
{
    std::vector<int> out;
    for (size_t i = 0; i < arr.size(); ++i)
        if (std::find (selTracks.begin(), selTracks.end(), arr[i].track) != selTracks.end()
            || (selTracks.empty() && arr[i].track == focusTrack))
            out.push_back ((int) i);
    return out;
}

void TrackView::splitAtSelection()
{
    auto lanesToCut = targetLanes();
    if (lanesToCut.empty())
        for (size_t i = 0; i < arr.size(); ++i) lanesToCut.push_back ((int) i);
    std::vector<juce::int64> points;
    if (selEnd > selStart) points = { selStart, selEnd };
    else points = { session.getPosition() };

    auto copy = arr;
    bool any = false;
    for (int i : lanesToCut)
        for (auto t : points) any = copy[(size_t) i].splitAt (t) || any;
    if (any) session.commit (std::move (copy));
    else status ("Nothing to split there - put the playhead (or a selection) over a clip");
}

void TrackView::deleteSelection()
{
    if (selEnd > selStart && ! selTracks.empty())
    {
        auto copy = arr;
        for (int i : targetLanes()) copy[(size_t) i].clearRange (selStart, selEnd);
        session.commit (std::move (copy));
        selEnd = selStart;
        return;
    }
    if (selClipTrack != nullptr)
    {
        auto copy = arr;
        for (auto& lane : copy)
            if (lane.track == selClipTrack)
                for (size_t c = 0; c < lane.clips.size(); ++c)
                    if (lane.clips[c].start == selClipStart)
                    {
                        lane.clips.erase (lane.clips.begin() + (long) c);
                        selClipTrack = nullptr;
                        session.commit (std::move (copy));
                        return;
                    }
    }
}

void TrackView::selectAll()
{
    selStart = 0;
    selEnd = session.getLength();
    selTracks.clear();
    for (auto& lane : arr) selTracks.push_back (lane.track);
    selClipTrack = nullptr;
    lanes->repaint();
}

void TrackView::showLaneMenu (int lane, juce::int64 t)
{
    const int clip = lane >= 0 ? arr[(size_t) lane].clipAt (t) : -1;
    const bool hasSel = selEnd > selStart;
    juce::PopupMenu m;
    if (clip >= 0)
    {
        m.addItem (1, "Split clip here");
        m.addItem (2, "Delete clip");
        m.addItem (3, "Select clip range");
        m.addSeparator();
    }
    m.addItem (4, hasSel ? "Split at selection edges" : "Split at playhead");
    m.addItem (5, "Delete selected range", hasSel);
    m.addItem (6, "Loop selected range", hasSel);
    m.addSeparator();
    m.addItem (7, "Add track");
    m.addItem (8, "Import audio...");

    juce::Component::SafePointer<TrackView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, lane, clip, t] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& v = *safe;
        auto copy = v.arr;
        switch (r)
        {
            case 1: if (copy[(size_t) lane].splitAt (v.snapTime (t))) v.session.commit (std::move (copy)); break;
            case 2: copy[(size_t) lane].clips.erase (copy[(size_t) lane].clips.begin() + clip); v.session.commit (std::move (copy)); break;
            case 3:
                v.selStart = v.arr[(size_t) lane].clips[(size_t) clip].start;
                v.selEnd = v.arr[(size_t) lane].clips[(size_t) clip].end();
                v.selTracks = { v.arr[(size_t) lane].track };
                break;
            case 4: v.splitAtSelection(); break;
            case 5: v.deleteSelection(); break;
            case 6: v.session.setLoopRange (v.selStart, v.selEnd); v.session.setLoop (true); break;
            case 7: v.focusTrack = v.session.addTrack(); break;
            case 8: v.importDialog(); break;
            default: break;
        }
        v.repaint();
    });
}

void TrackView::showTrackMenu (const TrackPtr& track)
{
    juce::PopupMenu colours;
    for (int i = 0; i < (int) std::size (palette); ++i)
        colours.addColouredItem (100 + i, juce::String::repeatedString (juce::String::charToString (0x25a0), 6),
                                 juce::Colour (palette[i]), true, track->colour == juce::Colour (palette[i]));
    int index = -1;
    for (size_t i = 0; i < arr.size(); ++i) if (arr[i].track == track) index = (int) i;

    juce::PopupMenu m;
    m.addSectionHeader (track->name);
    m.addItem (1, "Rename");
    m.addSubMenu ("Colour", colours);
    m.addItem (2, "Move up", index > 0);
    m.addItem (3, "Move down", index >= 0 && index < (int) arr.size() - 1);
    m.addSeparator();
    m.addItem (4, "Edit this track's tone in the rig...", track->useTone.load());
    m.addItem (5, "Use the current rig (" + proc.currentPresetName + ")");
    m.addItem (6, "Dry - no amp", true, ! track->useTone.load());
    m.addSeparator();
    m.addItem (7, "Delete track");

    juce::Component::SafePointer<TrackView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, track, index] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        auto& v = *safe;
        if (r >= 100) { track->colour = juce::Colour (palette[r - 100]); v.session.touch(); return; }
        switch (r)
        {
            case 1:
                for (auto& h : v.headers) if (h->track == track) h->rename();
                break;
            case 2:
            case 3:
            {
                auto copy = v.arr;
                if (index < 0 || index >= (int) copy.size()) break;
                std::swap (copy[(size_t) index], copy[(size_t) (r == 2 ? index - 1 : index + 1)]);
                v.session.commit (std::move (copy));
                break;
            }
            case 4: v.editToneInRig (track); break;
            case 5: v.applyTone (track, 2); break;
            case 6: v.applyTone (track, 1); break;
            case 7: v.session.removeTrack (track); break;
            default: break;
        }
    });
}

void TrackView::showToneMenu (const TrackPtr& track, juce::Component* target)
{
    juce::PopupMenu m;
    m.addItem (1, "Dry - no amp (backing tracks)", true, ! track->useTone.load());
    m.addItem (2, "Copy the current rig (" + proc.currentPresetName + ")");
    m.addItem (3, "Edit this tone in the rig...", track->useTone.load());
    m.addSeparator();
    const auto& f = Presets::factory();
    for (auto& cat : Presets::categories())
    {
        juce::PopupMenu sub;
        for (size_t i = 0; i < f.size(); ++i)
            if (f[i].category == cat)
                sub.addItem (100 + (int) i, f[i].name, true, track->useTone.load() && track->toneName == f[i].name);
        m.addSubMenu (cat, sub);
    }
    userPresetFiles = Presets::userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + Presets::fileExtension);
    userPresetFiles.sort();
    if (! userPresetFiles.isEmpty())
    {
        juce::PopupMenu sub;
        for (int i = 0; i < userPresetFiles.size(); ++i)
            sub.addItem (1000 + i, userPresetFiles[i].getFileNameWithoutExtension());
        m.addSubMenu ("My presets", sub);
    }

    juce::Component::SafePointer<TrackView> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, track] (int r)
    {
        if (safe != nullptr && r > 0) safe->applyTone (track, r);
    });
}

void TrackView::applyTone (const TrackPtr& track, int id)
{
    const auto& f = Presets::factory();
    if (id == 1)
    {
        track->useTone.store (false);
        track->toneName = "Dry";
    }
    else if (id == 2)
    {
        track->tone.fromValueTree (proc.getChain().toValueTree());
        track->toneName = proc.currentPresetName;
        track->useTone.store (true);
    }
    else if (id == 3)
    {
        editToneInRig (track);
        return;
    }
    else if (id >= 100 && id - 100 < (int) f.size())
    {
        track->tone.setModules (f[(size_t) id - 100].build());
        track->toneName = f[(size_t) id - 100].name;
        track->useTone.store (true);
    }
    else if (id >= 1000 && id - 1000 < userPresetFiles.size())
    {
        juce::String err;
        const auto file = userPresetFiles[id - 1000];
        if (! Presets::load (track->tone, file, err)) { status (err); return; }
        track->toneName = file.getFileNameWithoutExtension();
        track->useTone.store (true);
    }
    else return;

    session.touch();
    syncHeaders();
    status (track->name + " now plays through: " + (track->useTone.load() ? track->toneName : juce::String ("no amp (dry)")));
}

void TrackView::editToneInRig (const TrackPtr& track)
{
    toneEdit = track;
    if (track->useTone.load())
    {
        proc.getChain().fromValueTree (track->tone.toValueTree());
        proc.currentPresetName = track->toneName;
    }
    if (onEditToneInRig) onEditToneInRig();
}

void TrackView::finishToneEdit()
{
    auto track = std::move (toneEdit);
    toneEdit = nullptr;
    if (track == nullptr || std::none_of (arr.begin(), arr.end(), [&] (const Lane& l) { return l.track == track; }))
        return;
    track->tone.fromValueTree (proc.getChain().toValueTree());
    track->toneName = proc.currentPresetName;
    track->useTone.store (true);
    session.touch();
    syncHeaders();
    status ("Updated the tone of " + track->name + " from the rig (" + track->toneName + ")");
}

//==============================================================================
void TrackView::addPending (const juce::File& f, int lane, juce::int64 time)
{
    pending.push_back ({ lane, time, f.getFileNameWithoutExtension() });
    lanes->repaint();
}

// drop the boxes of imports that have finished (their clip is on the track now, or an error was shown)
void TrackView::prunePending()
{
    auto names = session.getImportNames();
    const auto before = pending.size();
    pending.erase (std::remove_if (pending.begin(), pending.end(), [&] (const PendingImport& p)
    {
        const int i = names.indexOf (p.name);
        if (i < 0) return true;
        names.remove (i);
        return false;
    }), pending.end());
    if (pending.size() != before) lanes->repaint();
}

void TrackView::importDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Import audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   Session::audioWildcard());
    juce::Component::SafePointer<TrackView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              for (auto& f : fc.getResults())
                              {
                                  safe->addPending (f, -1, 0);
                                  safe->session.importFile (f, -1, 0);
                              }
                          });
}

void TrackView::newProject()
{
    juce::Component::SafePointer<TrackView> safe (this);
    auto doIt = [safe]
    {
        if (safe == nullptr) return;
        safe->session.newProject();
        safe->selEnd = safe->selStart = 0;
        safe->selTracks.clear();
        safe->focusTrack = nullptr;
        safe->viewStart = 0;
        safe->status ("New project");
    };
    if (! session.isDirty()) { doIt(); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("New project")
                                      .withMessage ("Discard the unsaved changes in \"" + session.getProjectName() + "\"?")
                                      .withButton ("Discard")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [doIt] (int result) { if (result == 1) doIt(); });
}

void TrackView::openProject()
{
    juce::Component::SafePointer<TrackView> safe (this);
    auto choose = [safe]
    {
        if (safe == nullptr) return;
        safe->chooser = std::make_unique<juce::FileChooser> ("Open project", Session::projectsFolder(),
                                                             juce::String ("*") + Session::projectExtension);
        safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                    [safe] (const juce::FileChooser& fc)
                                    {
                                        if (safe == nullptr || fc.getResult() == juce::File()) return;
                                        juce::String err;
                                        if (safe->session.loadProject (fc.getResult(), err))
                                        {
                                            safe->selEnd = safe->selStart = 0;
                                            safe->selTracks.clear();
                                            safe->viewStart = 0;
                                            safe->zoomToFit();
                                            safe->status ("Opened " + safe->session.getProjectName());
                                        }
                                        else
                                            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", err);
                                    });
    };
    if (! session.isDirty()) { choose(); return; }
    juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                      .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                      .withTitle ("Open project")
                                      .withMessage ("Discard the unsaved changes in \"" + session.getProjectName() + "\"?")
                                      .withButton ("Discard")
                                      .withButton ("Cancel")
                                      .withAssociatedComponent (this),
                                  [choose] (int result) { if (result == 1) choose(); });
}

void TrackView::saveProject (bool saveAs)
{
    auto save = [this] (const juce::File& f)
    {
        juce::String err;
        if (session.saveProject (f, err)) status ("Saved " + f.getFullPathName());
        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", err);
        repaint (titleArea);
    };
    if (! saveAs && session.getProjectFile().existsAsFile())
    {
        save (session.getProjectFile());
        return;
    }
    chooser = std::make_unique<juce::FileChooser> ("Save project (a folder with this name is created for the audio)",
                                                   Session::projectsFolder().getChildFile (session.getProjectName() + Session::projectExtension),
                                                   juce::String ("*") + Session::projectExtension);
    juce::Component::SafePointer<TrackView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, save] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              // every project gets its own folder: <name>/<name>.toneproj + audio/
                              const auto picked = fc.getResult();
                              const auto name = picked.getFileNameWithoutExtension();
                              auto dir = picked.getParentDirectory();
                              if (dir.getFileName() != name) dir = dir.getChildFile (name);
                              dir.createDirectory();
                              save (dir.getChildFile (name + Session::projectExtension));
                          });
}

void TrackView::exportDialog()
{
    if (session.getLength() <= 0)
    {
        status ("There is nothing to export yet - record something or drop in an audio file");
        return;
    }
    const bool hasLoop = session.getLoopEnd() > session.getLoopStart();
    const bool canMp3 = Session::findMp3Encoder() != juce::File();

    exportWindow = std::make_unique<juce::AlertWindow> ("Export mix",
        "Renders the tracks as you hear them (mute, solo, volume, pan and each track's tone)."
        + juce::String (canMp3 ? "" : "\n\nMP3 needs 'lame' or 'ffmpeg' installed - neither was found."),
        juce::MessageBoxIconType::NoIcon, this);
    exportWindow->addComboBox ("format", { "WAV  24-bit", "WAV  32-bit float", "MP3  320 kbps", "MP3  192 kbps" }, "Format");
    exportWindow->addComboBox ("range", hasLoop ? juce::StringArray { "Whole song", "Loop range only" } : juce::StringArray { "Whole song" }, "Range");
    exportWindow->addComboBox ("level", { "Normalize (peaks at -1 dBFS)", "As mixed" }, "Level");
    exportWindow->addButton ("Export...", 1, juce::KeyPress (juce::KeyPress::returnKey));
    exportWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TrackView> safe (this);
    exportWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr) return;
        auto window = std::move (safe->exportWindow);
        if (result != 1 || window == nullptr) return;
        Session::ExportOptions o;
        const int fmt = window->getComboBoxComponent ("format")->getSelectedItemIndex();
        o.format = fmt == 0 ? Session::ExportOptions::Format::wav24
                 : fmt == 1 ? Session::ExportOptions::Format::wav32 : Session::ExportOptions::Format::mp3;
        o.mp3Kbps = fmt == 3 ? 192 : 320;
        o.loopRangeOnly = window->getComboBoxComponent ("range")->getSelectedItemIndex() == 1;
        o.normalize = window->getComboBoxComponent ("level")->getSelectedItemIndex() == 0;
        safe->chooseExportFile (o);
    }), false);
}

void TrackView::chooseExportFile (const Session::ExportOptions& o)
{
    const juce::String ext = o.format == Session::ExportOptions::Format::mp3 ? ".mp3" : ".wav";
    auto folder = lastExportFolder;
    if (! folder.isDirectory())
        folder = session.getProjectFile() != juce::File() ? session.getProjectFile().getParentDirectory()
                                                          : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Export mix", folder.getChildFile (session.getProjectName() + ext), "*" + ext);
    juce::Component::SafePointer<TrackView> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, o, ext] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              const auto file = fc.getResult().withFileExtension (ext);
                              safe->lastExportFolder = file.getParentDirectory();
                              juce::String err;
                              if (! safe->session.exportMix (file, o, err))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Export", err);
                          });
}

//==============================================================================
bool TrackView::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    const auto c = juce::CharacterFunctions::toLowerCase (k.getTextCharacter());
    const int code = k.getKeyCode();
    if (mods.isCommandDown())
    {
        if (code == 'Z' || c == 'z') { if (mods.isShiftDown()) session.redo(); else session.undo(); return true; }
        if (code == 'Y' || c == 'y') { session.redo(); return true; }
        if (code == 'S' || c == 's') { saveProject (mods.isShiftDown()); return true; }
        if (code == 'O' || c == 'o') { openProject(); return true; }
        if (code == 'A' || c == 'a') { selectAll(); return true; }
        if (code == 'E' || c == 'e') { exportButton.triggerClick(); return true; }
        return false;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
    if (k == juce::KeyPress::escapeKey) { selEnd = selStart; selClipTrack = nullptr; selTracks.clear(); lanes->repaint(); return true; }
    if (k == juce::KeyPress::endKey) { session.setPosition (session.getLength()); return true; }
    if (c == 's') { splitAtSelection(); return true; }
    if (c == '+' || c == '=') { zoomIn.triggerClick(); return true; }
    if (c == '-') { zoomOut.triggerClick(); return true; }
    if (c == 'f') { zoomToFit(); return true; }
    return false;   // transport keys are handled by the editor
}

//==============================================================================
// Every file is accepted: whatever can't be read is reported instead of being refused silently.
bool TrackView::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (juce::File (f).existsAsFile()) return true;
    return false;
}

void TrackView::fileDragMove (const juce::StringArray& files, int x, int y)
{
    dragFileCount = 0;
    for (auto& f : files) if (juce::File (f).existsAsFile()) ++dragFileCount;
    const auto p = lanes->getLocalPoint (this, juce::Point<int> (x, y));
    dropLane = laneAt (std::max (0, p.y));
    dropTime = p.x < 0 ? 0 : snapTime (xToTime (p.x));
    lanes->repaint();
}

void TrackView::fileDragExit (const juce::StringArray&)
{
    dropLane = -2;
    lanes->repaint();
}

void TrackView::filesDropped (const juce::StringArray& files, int x, int y)
{
    fileDragMove (files, x, y);
    int lane = dropLane;
    juce::StringArray skipped;
    for (auto& path : files)
    {
        const juce::File f (path);
        if (! f.existsAsFile()) { skipped.add (f.getFileName()); continue; }
        addPending (f, lane, dropTime);
        session.importFile (f, lane, dropTime);
        lane = lane >= 0 && lane + 1 < (int) arr.size() ? lane + 1 : -1;   // several files: one per track, downwards
    }
    if (! skipped.isEmpty()) status ("Skipped (not a file): " + skipped.joinIntoString (", "));
    dropLane = -2;
    lanes->repaint();
    grabKeyboardFocus();
}

//==============================================================================
void TrackView::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    juce::ColourGradient tb (juce::Colour (0xff23262d), 0, 0, juce::Colour (0xff1a1d22), 0, (float) toolbarH, false);
    g.setGradientFill (tb);
    g.fillRect (toolbar);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (toolbar.removeFromBottom (1));

    g.setFont (uiFont (14.0f, true));
    g.setColour (Colours::text);
    g.drawFittedText (session.getProjectName() + (session.isDirty() ? " *" : ""), titleArea, juce::Justification::centred, 1, 0.7f);

    g.setColour (juce::Colour (0xff1d2026));
    g.fillRect (cornerArea);
    g.setColour (Colours::dim);
    g.setFont (uiFont (10.5f, true));
    g.drawText ("TRACKS  (" + juce::String ((int) arr.size()) + ")", cornerArea.reduced (10, 0), juce::Justification::centredLeft);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (cornerArea.getX(), cornerArea.getBottom() - 1, cornerArea.getWidth(), 1);
}

void TrackView::resized()
{
    auto r = getLocalBounds();
    toolbar = r.removeFromTop (toolbarH);
    {
        auto t = toolbar.reduced (8, 5);
        auto place = [&t] (juce::Component& c, int w, int gapAfter = 4) { c.setBounds (t.removeFromLeft (w)); t.removeFromLeft (gapAfter); };
        place (selectTool, 56, 2);
        place (splitTool, 50, 12);
        place (snapButton, 48, 12);
        place (zoomOut, 26, 2);
        place (zoomSlider, 80, 2);
        place (zoomIn, 26, 4);
        place (fitButton, 38, 12);
        place (heightSlider, 56, 12);
        place (undoButton, 50, 2);
        place (redoButton, 50, 12);
        place (addTrackButton, 66, 4);
        place (importButton, 74, 4);
        place (exportButton, 74, 12);
        for (auto* b : { &saveAsButton, &saveButton, &openButton, &newButton })
        {
            b->setBounds (t.removeFromRight (b == &saveAsButton ? 64 : 50));
            t.removeFromRight (4);
        }
        titleArea = t;
    }

    auto left = r.removeFromLeft (headerW);
    cornerArea = left.removeFromTop (rulerH);
    left.removeFromBottom (10);
    headerHolder.setBounds (left);
    ruler->setBounds (r.removeFromTop (rulerH).withTrimmedRight (10));
    hbar.setBounds (r.removeFromBottom (10).withTrimmedRight (10));
    vbar.setBounds (r.removeFromRight (10));
    lanes->setBounds (r);
    updateScrollBars();
    syncHeaders();
}

} // namespace pg
