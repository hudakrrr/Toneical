// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"

namespace pg
{
//==============================================================================
// Flat icon button for the transport bar (the icon is drawn, no image files).
class IconButton final : public juce::Button
{
public:
    enum class Icon { rig, tracks, record, play, stop, rewind, loop, metronome };
    IconButton (Icon i, const juce::String& tip);
    void setIcon (Icon i) { if (icon != i) { icon = i; repaint(); } }
    void setLit (juce::Colour c) { lit = c; repaint(); }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    Icon icon;
    juce::Colour lit = Colours::accent;
};

//==============================================================================
// The bar along the bottom (both modes): Rig / Record switch, transport, time display,
// metronome, time signature, tempo and tap tempo.
class TransportBar final : public juce::Component,
                           private juce::Timer
{
public:
    explicit TransportBar (ToneicalProcessor&);
    ~TransportBar() override;

    std::function<void (bool recordMode)> onModeChange;
    void setRecordMode (bool);

    // Space play/stop, R record, Home rewind, L loop, C metronome
    bool handleKey (const juce::KeyPress&);

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int height = 50;

private:
    class TimeDisplay;
    void timerCallback() override;
    void tap();
    void refresh();

    ToneicalProcessor& proc;
    rec::Session& session;

    IconButton rigMode { IconButton::Icon::rig, "Rig: pedals, amps and cabs" },
               recMode { IconButton::Icon::tracks, "Record: multitrack recorder" },
               recButton { IconButton::Icon::record, "Record (R) - onto the armed track, or a new one" },
               playButton { IconButton::Icon::play, "Play / Stop (Space)" },
               rewindButton { IconButton::Icon::rewind, "Back to the start (Home)" },
               loopButton { IconButton::Icon::loop, "Loop (L) - drag in the ruler to set the loop range" },
               clickButton { IconButton::Icon::metronome, "Metronome (C) - clicks along the timeline, or free-running while stopped" };
    juce::TextButton countInButton { "COUNT-IN" }, tapButton { "TAP" };
    juce::ComboBox sigBox;
    juce::Slider bpmSlider { juce::Slider::LinearBarVertical, juce::Slider::TextBoxAbove },
                 clickVol { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<TimeDisplay> display;

    juce::Rectangle<int> secMode, secTransport, secClick;
    std::vector<double> taps;
    bool recordMode = false;
    int blink = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};

} // namespace pg
