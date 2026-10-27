// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "gui/LookAndFeel.h"
#include "gui/PedalComponent.h"
#include "gui/ChainStrip.h"
#include "gui/GearBrowser.h"
#include "gui/TransportBar.h"
#include "gui/TrackView.h"

//==============================================================================
class LevelMeter final : public juce::Component
{
public:
    void push (float peak)
    {
        const float db = juce::Decibels::gainToDecibels (peak, -60.0f);
        level = db > level ? db : level - 1.2f;
        if (db > hold) { hold = db; holdTicks = 40; } else if (--holdTicks < 0) hold -= 0.8f;
        clip = peak >= 0.99f ? 60 : juce::jmax (0, clip - 1);
        repaint();
    }
    void paint (juce::Graphics&) override;

private:
    float level = -60, hold = -60; int holdTicks = 0, clip = 0;
};

//==============================================================================
class TunerDisplay final : public juce::Component
{
public:
    void set (const pg::Tuner::Reading& r);
    void paint (juce::Graphics&) override;

private:
    pg::Tuner::Reading reading;
    float smoothedCents = 0; int holdFrames = 0;
};

//==============================================================================
// The big "stage" at the top: shows the selected pedal / amp / cab, scaled up.
class PedalStage final : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    juce::String hint;
};

//==============================================================================
class ToneicalEditor final : public juce::AudioProcessorEditor,
                            public juce::DragAndDropContainer,
                            private juce::ChangeListener,
                            private juce::Timer
{
public:
    explicit ToneicalEditor (ToneicalProcessor&);
    ~ToneicalEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void setRecordMode (bool);
    void showMessage (const juce::String&);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void layoutHeader (juce::Rectangle<int> area);
    juce::Rectangle<int> secLogo, secPreset, secInput, secTuner, secOutput, secAudio;   // top-bar modules
    static constexpr int kMinWidth = 1080;   // sum of the top-bar modules' minimum widths + margins

    void rebuildChain();
    void selectModule (pg::ModulePtr m);
    void layoutStage();
    void insertGear (const juce::String& typeId, int index);
    void showAddMenu (int insertIndex);
    int indexOf (const pg::ModulePtr&) const;
    void refreshPresetList();
    void savePreset();
    void loadPresetFromDisk();
    void updateStatus();

    ToneicalProcessor& proc;
    pg::PedalLookAndFeel laf;
    juce::TooltipWindow tooltips { this, 700 };

    // header
    juce::ComboBox presetBox;
    juce::TextButton saveButton { "Save" }, loadButton { "Open" }, audioButton { "Audio..." }, muteButton,
                     calibButton { "Auto" };
    int calibTicks = 0, statusHoldTicks = 0;
    juce::ComboBox inputBox;
    juce::ComboBox bufferBox;                 // buffer size (latency), like AmpliTube's
    void setBufferSize (int samples);
    int lastShownBuffer = -1;
    juce::String savedMinQuantum;             // PipeWire setting before we changed it
    juce::Slider inGain { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox },
                 outVol { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    juce::Label inLabel, outLabel, inValue, outValue;
    LevelMeter inMeter, outMeter;
    TunerDisplay tuner;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> inputAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> inGainAttach, outVolAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteAttach;

    // top: big view of the selected pedal; bottom: whole chain; right: gear browser
    PedalStage stage;
    std::unique_ptr<pg::PedalComponent> bigPedal;
    pg::ModulePtr selected;
    juce::Viewport stripView;
    std::unique_ptr<pg::ChainStrip> strip;
    pg::GearBrowser browser;

    // bottom: transport bar (both modes); record mode: the multitrack view instead of the board
    std::unique_ptr<pg::TransportBar> transport;
    std::unique_ptr<pg::TrackView> trackView;
    bool recordMode = false;

    // footer
    juce::Label status;

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> userPresets;
    int tick = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ToneicalEditor)
};
