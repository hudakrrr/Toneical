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
// The selected piece of gear, big, in the top "stage". Each type has its own
// enclosure (see PedalArt): stomp boxes, compact pedals with a treadle, a round fuzz,
// an amp head, a speaker cab, digital boxes with a screen. Knobs/switches talk
// directly to the module's atomics.
class PedalComponent final : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void (PedalComponent&)> onRemove;
        std::function<void (PedalComponent&, int delta)> onMove;
        std::function<void (PedalComponent&)> onInsertAfter;
    };

    PedalComponent (ModulePtr module, Callbacks callbacks);
    ~PedalComponent() override;

    ModulePtr getModule() const { return module; }
    int getPreferredWidth() const  { return preferredWidth; }
    int getPreferredHeight() const { return preferredHeight; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

    void refreshFromModule(); // after preset load etc.

private:
    enum class Switch { stomp, treadle, toggle, soft };

    void timerCallback() override;
    void updateStatus();
    float glow = 0.0f, glowShown = -1.0f, press = 0.0f;
    int ticks = 0;
    void chooseFile();
    void placeKnobs (juce::Rectangle<int> area, int cols, int maxKnob);
    void placeRow (juce::Rectangle<int> area);   // combos + toggles side by side
    void toggleBypass();

    ModulePtr module;
    Callbacks cb;
    PedalArt::Style style;
    Switch switchKind = Switch::stomp;
    int preferredWidth = 210, preferredHeight = 384;
    bool active = true;

    struct Knob
    {
        int index;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::Label> label;
    };
    std::vector<Knob> knobs;
    std::vector<std::pair<int, std::unique_ptr<juce::ComboBox>>> combos;
    std::vector<std::pair<int, std::unique_ptr<juce::ToggleButton>>> toggles;

    juce::TextButton fileButton, clearFileButton { juce::String::fromUTF8 ("\xc3\x97") };
    juce::Label fileLabel, statusLabel;
    std::unique_ptr<juce::FileChooser> chooser;

    juce::Rectangle<float> body, footswitch, led;
    juce::Rectangle<int> nameArea, panelArea, grilleArea, screenArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalComponent)
};

} // namespace pg
