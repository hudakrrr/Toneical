// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"
#include "Presets.h"
#include "modules/ModuleFactory.h"
#include "BinaryData.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_devices/juce_audio_devices.h>
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

using namespace pg;

namespace
{
#if JucePlugin_Build_Standalone
    juce::StandalonePluginHolder* standaloneHolder() { return juce::StandalonePluginHolder::getInstance(); }
#else
    void* standaloneHolder() { return nullptr; }
#endif

    constexpr int headerH = 92, footerH = 26, stripH = 176;
}

//==============================================================================
void LevelMeter::paint (juce::Graphics& g)
{
    // LED-segment meter: green, amber from -12 dB, red from -3 dB; peak-hold segment; clip light
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.65f));
    g.fillRoundedRectangle (r, 2.5f);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawRoundedRectangle (r.translated (0, 0.5f), 2.5f, 0.8f);
    auto inner = r.reduced (2.0f);
    const float clipW = juce::jmin (6.0f, inner.getHeight() + 1.0f);
    auto clipR = inner.removeFromRight (clipW);
    inner.removeFromRight (2.0f);

    const int n = juce::jlimit (8, 40, (int) (inner.getWidth() / 4.0f));
    const float segW = inner.getWidth() / (float) n;
    const auto dbAt = [] (float t) { return -60.0f + 60.0f * t; };
    const int lit = (int) std::floor (juce::jlimit (0.0f, 1.0f, (level + 60.0f) / 60.0f) * (float) n);
    const int holdSeg = (int) std::floor (juce::jlimit (0.0f, 0.999f, (hold + 60.0f) / 60.0f) * (float) n);
    for (int i = 0; i < n; ++i)
    {
        const float db = dbAt (((float) i + 0.5f) / (float) n);
        const auto col = db > -3.0f ? Colours::warn : (db > -12.0f ? Colours::accent : Colours::good);
        const bool on = i < lit || (i == holdSeg && hold > -59.0f);
        g.setColour (on ? col : col.withAlpha (0.12f));
        g.fillRect (inner.getX() + segW * (float) i + 0.5f, inner.getY(), segW - 1.0f, inner.getHeight());
    }
    g.setColour (clip > 0 ? Colours::warn : Colours::warn.withAlpha (0.15f));
    g.fillRoundedRectangle (clipR, 1.5f);
}

//==============================================================================
void TunerDisplay::set (const Tuner::Reading& r)
{
    if (r.valid) { reading = r; holdFrames = 12; smoothedCents += 0.35f * (r.cents - smoothedCents); }
    else if (holdFrames > 0 && --holdFrames == 0) reading.valid = false;
    repaint();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    if (r.getWidth() < 40) return;
    // recessed LCD
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRoundedRectangle (r, 6.0f);
    auto scr = r.reduced (2.0f);
    juce::ColourGradient lcd (juce::Colour (0xff10161c), scr.getX(), scr.getY(), juce::Colour (0xff07090c), scr.getX(), scr.getBottom(), false);
    g.setGradientFill (lcd);
    g.fillRoundedRectangle (scr, 5.0f);
    g.setColour (juce::Colours::white.withAlpha (0.04f));
    g.fillRoundedRectangle (scr.withHeight (scr.getHeight() * 0.45f), 5.0f);

    auto in = scr.reduced (10.0f, 6.0f);
    const bool inTune = reading.valid && std::abs (smoothedCents) < 4.0f;
    const auto lit = inTune ? Colours::good : Colours::accent;
    const bool roomy = in.getWidth() > 220.0f;

    // note name, glowing when a string rings
    auto noteArea = in.removeFromLeft (juce::jmin (64.0f, in.getWidth() * 0.28f));
    g.setFont (uiFont (juce::jmin (34.0f, noteArea.getHeight() * 0.85f), true));
    if (reading.valid)
    {
        g.setColour (lit.withAlpha (0.25f));
        g.drawText (reading.note, noteArea.translated (0, 1).expanded (2).toNearestInt(), juce::Justification::centred);
    }
    g.setColour (reading.valid ? (inTune ? Colours::good : Colours::text) : Colours::dim.withAlpha (0.35f));
    g.drawText (reading.valid ? reading.note : "-", noteArea.toNearestInt(), juce::Justification::centred);

    // cents scale with a needle
    auto info = in.removeFromBottom (13.0f);
    auto scale = in.reduced (4.0f, 3.0f);
    const float cx = scale.getCentreX(), half = scale.getWidth() * 0.5f;
    const int step = scale.getWidth() > 240.0f ? 5 : (scale.getWidth() > 120.0f ? 10 : 25);   // fewer ticks on a small screen
    for (int c = -50; c <= 50; c += step)
    {
        const float x = cx + half * (float) c / 50.0f;
        const bool major = c % 25 == 0;
        const float h = c == 0 ? scale.getHeight() : scale.getHeight() * (major ? 0.55f : 0.3f);
        g.setColour ((c == 0 ? Colours::good : Colours::dim).withAlpha (major ? 0.8f : 0.35f));
        g.fillRect (x - 0.5f, scale.getCentreY() - h * 0.5f, 1.0f, h);
    }
    if (reading.valid)
    {
        const float x = cx + half * juce::jlimit (-50.0f, 50.0f, smoothedCents) / 50.0f;
        juce::ColourGradient glow (lit.withAlpha (0.45f), x, scale.getCentreY(), lit.withAlpha (0.0f), x + 18.0f, scale.getCentreY(), true);
        g.setGradientFill (glow);
        g.fillEllipse (x - 18.0f, scale.getY() - 4.0f, 36.0f, scale.getHeight() + 8.0f);
        g.setColour (lit);
        g.fillRoundedRectangle (x - 2.5f, scale.getY(), 5.0f, scale.getHeight(), 2.5f);
        // in-tune arrows
        if (inTune)
        {
            g.setColour (Colours::good);
            juce::Path p;
            p.addTriangle (cx - 16, scale.getCentreY() - 5, cx - 16, scale.getCentreY() + 5, cx - 9, scale.getCentreY());
            p.addTriangle (cx + 16, scale.getCentreY() - 5, cx + 16, scale.getCentreY() + 5, cx + 9, scale.getCentreY());
            g.fillPath (p);
        }
    }

    g.setFont (uiFont (10.5f));
    g.setColour (Colours::dim);
    const juce::String txt = reading.valid ? juce::String (reading.hz, 1) + " Hz   " + (smoothedCents >= 0 ? "+" : "") + juce::String (smoothedCents, 0) + " cents"
                                           : (roomy ? "TUNER - play a single string" : "TUNER");
    g.drawFittedText (txt, info.toNearestInt(), juce::Justification::centred, 1, 0.8f);
}

//==============================================================================
void PedalStage::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float floorY = r.getBottom() - r.getHeight() * 0.2f;

    // back wall: dark, with faint acoustic-panel slats
    juce::ColourGradient wall (juce::Colour (0xff1d2027), r.getCentreX(), r.getY(), juce::Colour (0xff121418), r.getCentreX(), floorY, false);
    g.setGradientFill (wall);
    g.fillRect (r.withBottom (floorY));
    for (float x = r.getX(); x < r.getRight(); x += 46.0f)
    {
        g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRect (x, r.getY(), 1.5f, floorY - r.getY());
        g.setColour (juce::Colours::white.withAlpha (0.015f)); g.fillRect (x + 1.5f, r.getY(), 1.0f, floorY - r.getY());
    }

    // stage floor: boards in perspective
    juce::ColourGradient floor (juce::Colour (0xff221d19), r.getCentreX(), floorY, juce::Colour (0xff0e0c0b), r.getCentreX(), r.getBottom(), false);
    g.setGradientFill (floor);
    g.fillRect (r.withTop (floorY));
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    for (int i = -14; i <= 14; ++i)
    {
        const float x0 = r.getCentreX() + (float) i * 70.0f, x1 = r.getCentreX() + (float) i * 120.0f;
        g.drawLine (x0, floorY, x1, r.getBottom(), 1.0f);
    }
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.fillRect (r.getX(), floorY, r.getWidth(), 1.0f);

    // warm spotlight from above
    juce::ColourGradient spot (juce::Colour (0xfff2c27a).withAlpha (0.11f), r.getCentreX(), floorY,
                               juce::Colour (0x00f2c27a), r.getCentreX() + r.getWidth() * 0.42f, floorY, true);
    g.setGradientFill (spot);
    g.fillEllipse (r.getCentreX() - r.getWidth() * 0.42f, r.getY() - r.getHeight() * 0.1f, r.getWidth() * 0.84f, r.getHeight() * 1.2f);

    // vignette
    juce::ColourGradient vig (juce::Colours::transparentBlack, r.getCentreX(), r.getCentreY(),
                              juce::Colours::black.withAlpha (0.45f), r.getX(), r.getY(), true);
    g.setGradientFill (vig);
    g.fillRect (r);

    if (hint.isNotEmpty())
    {
        g.setColour (Colours::dim);
        g.setFont (uiFont (14.0f));
        g.drawFittedText (hint, getLocalBounds().reduced (40), juce::Justification::centred, 3);
    }
}

//==============================================================================
ToneicalEditor::ToneicalEditor (ToneicalProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&laf);
    juce::LookAndFeel::setDefaultLookAndFeel (&laf);

    // presets
    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.onChange = [this]
    {
        const int id = presetBox.getSelectedId();
        const auto& f = Presets::factory();
        if (id >= 1 && id <= (int) f.size())
        {
            proc.getChain().setModules (f[(size_t) id - 1].build());
            proc.currentPresetName = f[(size_t) id - 1].name;
            status.setText (f[(size_t) id - 1].category + "  |  " + f[(size_t) id - 1].description, juce::dontSendNotification);
            statusHoldTicks = 8 * 30;
            repaint();
        }
        else if (id >= 1000 && id - 1000 < userPresets.size())
        {
            juce::String err;
            const auto file = userPresets[id - 1000];
            if (Presets::load (proc.getChain(), file, err)) { proc.currentPresetName = file.getFileNameWithoutExtension(); repaint(); }
            else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset", err);
        }
    };
    addAndMakeVisible (presetBox);
    saveButton.onClick = [this] { savePreset(); };
    loadButton.onClick = [this] { loadPresetFromDisk(); };
    saveButton.setTooltip ("Save the whole board (including loaded NAM/IR file paths)");
    loadButton.setTooltip ("Open a .toneical preset file");
    addAndMakeVisible (saveButton);
    addAndMakeVisible (loadButton);
    refreshPresetList();

    // Standalone: "Toneical --preset 'Hot Lead'" (or a number, or a .toneical file) picks the board on start-up
    static bool commandLineHandled = false;
    if (! commandLineHandled && juce::JUCEApplicationBase::isStandaloneApp())
    {
        commandLineHandled = true;
        auto args = juce::StringArray::fromTokens (juce::JUCEApplicationBase::getCommandLineParameters(), true);
        const int at = args.indexOf ("--preset");
        if (at >= 0 && at + 1 < args.size())
        {
            const auto wanted = args[at + 1].unquoted();
            const auto& f = Presets::factory();
            juce::String err;
            if (juce::File::isAbsolutePath (wanted) && juce::File (wanted).existsAsFile())
            {
                if (Presets::load (proc.getChain(), juce::File (wanted), err))
                    proc.currentPresetName = juce::File (wanted).getFileNameWithoutExtension();
            }
            else
                for (size_t i = 0; i < f.size(); ++i)
                    if (f[i].name.equalsIgnoreCase (wanted) || juce::String ((int) i + 1) == wanted)
                    {
                        proc.getChain().setModules (f[i].build());
                        proc.currentPresetName = f[i].name;
                    }
        }
    }
    presetBox.setText (proc.currentPresetName, juce::dontSendNotification);

    // input section
    inputBox.addItemList ({ "Input 1", "Input 2", "Inputs 1+2" }, 1);
    inputBox.setTooltip ("Which interface input your guitar is plugged into");
    addAndMakeVisible (inputBox);
    inputAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.getParams(), "input", inputBox);

    auto setupSmallKnob = [this] (juce::Slider& s, juce::Label& name, juce::Label& value, const juce::String& text)
    {
        s.setColour (juce::Slider::rotarySliderOutlineColourId, Colours::panel);
        addAndMakeVisible (s);
        name.setText (text, juce::dontSendNotification);
        name.setFont (uiFont (10.5f, true));
        name.setColour (juce::Label::textColourId, Colours::dim);
        name.setJustificationType (juce::Justification::centredLeft);
        addChildComponent (name);   // the header sections carry captions instead
        value.setFont (uiFont (12.0f, true)); value.setMinimumHorizontalScale (0.7f);
        value.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (value);
        s.onValueChange = [&s, &value, text] { value.setText (text + "  " + (s.getValue() > 0 ? "+" : "") + juce::String (s.getValue(), 1) + " dB", juce::dontSendNotification); };
    };
    setupSmallKnob (inGain, inLabel, inValue, "Gain");
    setupSmallKnob (outVol, outLabel, outValue, "Master");
    inGainAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getParams(), "ingain", inGain);
    outVolAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.getParams(), "outgain", outVol);
    inGain.onValueChange(); outVol.onValueChange();
    inGain.setDoubleClickReturnValue (true, 0.0);
    calibButton.setTooltip ("Auto-set the input gain: click, then play hard (full strums) for 6 seconds");
    calibButton.onClick = [this]
    {
        if (calibTicks > 0) return;
        proc.startInputCalibration();
        calibTicks = 6 * 30;
        calibButton.setToggleState (true, juce::dontSendNotification);
    };
    addAndMakeVisible (calibButton);
    outVol.setDoubleClickReturnValue (true, -6.0);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);

    addAndMakeVisible (tuner);
    muteButton.setButtonText ("Mute");
    muteButton.setClickingTogglesState (true);
    muteButton.setTooltip ("Silence the output while you tune");
    addAndMakeVisible (muteButton);
    muteAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.getParams(), "tunermute", muteButton);

#if JucePlugin_Build_Standalone
    if (auto* holder = standaloneHolder())
    {
        // A guitar rig must hear its input: JUCE mutes it by default to prevent feedback.
        holder->getMuteInputValue().setValue (false);
        audioButton.onClick = [] { if (auto* h = standaloneHolder()) h->showAudioSettingsDialog(); };
        audioButton.setButtonText ("Audio...");
        audioButton.setTooltip ("Pick your audio interface, sample rate (48 kHz!) and buffer size");
        addAndMakeVisible (audioButton);

        for (int b : { 16, 32, 48, 64, 96, 128, 256, 512 })
            bufferBox.addItem (juce::String (b) + " samples (" + juce::String (b / 48.0, 2) + " ms)", b);
        bufferBox.setTextWhenNothingSelected ("Buffer");
        bufferBox.setTooltip ("Buffer size = latency. 16-64 samples feel like a real amp; if you hear crackles, "
                              "take the next bigger size. With JACK/PipeWire this sets the PipeWire quantum.");
        bufferBox.onChange = [this] { if (bufferBox.getSelectedId() > 0) setBufferSize (bufferBox.getSelectedId()); };
        addAndMakeVisible (bufferBox);
    }
#endif

    // stage (top), chain strip (bottom), gear browser (right)
    addAndMakeVisible (stage);
    ChainStrip::Callbacks scb;
    scb.onSelect = [this] (const ModulePtr& m) { selectModule (m); };
    scb.onMove   = [this] (int from, int to)
    {
        juce::MessageManager::callAsync ([this, from, to, safe = juce::Component::SafePointer<ToneicalEditor> (this)]
                                         { if (safe != nullptr) proc.getChain().move (from, to); });
    };
    scb.onInsert = [this] (const juce::String& type, int idx) { insertGear (type, idx); };
    scb.onRemove = [this] (int idx)
    {
        juce::MessageManager::callAsync ([this, idx, safe = juce::Component::SafePointer<ToneicalEditor> (this)]
                                         { if (safe != nullptr) proc.getChain().remove (idx); });
    };
    scb.onAddMenu = [this] (int idx) { showAddMenu (idx); };
    strip = std::make_unique<ChainStrip> (scb);
    stripView.setViewedComponent (strip.get(), false);
    stripView.setScrollBarsShown (false, true);
    stripView.setScrollBarThickness (8);
    addAndMakeVisible (stripView);
    browser.onDoubleClick = [this] (const juce::String& type)
    {
        const int sel = indexOf (selected);
        insertGear (type, sel >= 0 ? sel + 1 : -1);
    };
    addAndMakeVisible (browser);

    status.setFont (uiFont (12.0f));
    status.setColour (juce::Label::textColourId, Colours::dim);
    addAndMakeVisible (status);

    proc.getChain().addChangeListener (this);
    rebuildChain();

    setResizable (true, true);
    setResizeLimits (kMinWidth, 720, 4000, 2400);
    setSize (1440, 920);
    {
        // --size WxH (handy for screenshots / testing small windows)
        const auto args = juce::StringArray::fromTokens (juce::JUCEApplicationBase::getCommandLineParameters(), true);
        if (const int at = args.indexOf ("--size"); at >= 0 && at + 1 < args.size())
        {
            const int w = args[at + 1].upToFirstOccurrenceOf ("x", false, true).getIntValue();
            const int h = args[at + 1].fromFirstOccurrenceOf ("x", false, true).getIntValue();
            if (w > 0 && h > 0) setSize (juce::jmax (kMinWidth, w), juce::jmax (720, h));
        }
    }
    startTimerHz (30);
}

ToneicalEditor::~ToneicalEditor()
{
    stopTimer();
    if (savedMinQuantum.isNotEmpty())
    {
        // give PipeWire its old settings back
        juce::ChildProcess p1, p2;
        p1.start ("pw-metadata -n settings 0 clock.force-quantum 0"); p1.waitForProcessToFinish (2000);
        p2.start ("pw-metadata -n settings 0 clock.min-quantum " + savedMinQuantum); p2.waitForProcessToFinish (2000);
    }
    proc.getChain().removeChangeListener (this);
    bigPedal.reset();
    strip.reset();
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void ToneicalEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);

    // header: a brushed-metal rack faceplate with recessed sections
    auto header = getLocalBounds().removeFromTop (headerH).toFloat();
    juce::ColourGradient grad (juce::Colour (0xff2b2f37), 0, 0, juce::Colour (0xff1a1d22), 0, header.getBottom(), false);
    g.setGradientFill (grad);
    g.fillRect (header);
    {
        juce::Random rnd (7);
        for (float y = 0; y < header.getBottom(); y += 1.5f)
        {
            g.setColour ((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha (rnd.nextFloat() * 0.035f));
            g.fillRect (0.0f, y, header.getWidth(), 1.0f);
        }
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.fillRect (0.0f, 0.0f, header.getWidth(), 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.7f));
    g.fillRect (0.0f, header.getBottom() - 2.0f, header.getWidth(), 2.0f);
    g.setColour (Colours::accent.withAlpha (0.35f));
    g.fillRect (0.0f, header.getBottom() - 1.0f, header.getWidth(), 1.0f);
    // rack screws
    for (float x : { 7.0f, header.getRight() - 7.0f })
        for (float y : { 12.0f, header.getBottom() - 14.0f })
        {
            juce::ColourGradient sc (juce::Colour (0xffb0b0b0), x - 3, y - 3, juce::Colour (0xff454545), x + 3, y + 3, false);
            g.setGradientFill (sc); g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f);
            g.setColour (juce::Colour (0xff2a2a2a)); g.drawLine (x - 2.2f, y, x + 2.2f, y, 1.0f);
        }
    // rack modules: recessed panels with an engraved caption
    auto module = [&] (juce::Rectangle<int> sec, const juce::String& caption)
    {
        if (sec.getWidth() < 10) return;
        const auto m = sec.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.32f));
        g.fillRoundedRectangle (m, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.drawRoundedRectangle (m.translated (0, 1.0f).reduced (0.5f), 6.0f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRoundedRectangle (m.reduced (0.5f), 6.0f, 1.0f);
        if (caption.isNotEmpty())
        {
            g.setFont (uiFont (9.5f, true));
            g.setColour (juce::Colours::black.withAlpha (0.6f));
            g.drawText (caption, m.withHeight (16.0f).translated (9.0f, 2.0f).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (Colours::dim);
            g.drawText (caption, m.withHeight (16.0f).translated (9.0f, 1.0f).toNearestInt(), juce::Justification::centredLeft);
        }
    };
    module (secPreset, "PRESET");
    module (secInput, "INPUT");
    module (secOutput, "OUTPUT");
    module (secAudio, "AUDIO");

    // logo (script font embedded from assets/fonts), sized to its module
    static const auto logoFace = juce::Typeface::createSystemTypefaceFor (BinaryData::LogoFont_ttf,
                                                                          (size_t) BinaryData::LogoFont_ttfSize);
    {
        auto lg = secLogo.toFloat();
        const float h = juce::jlimit (40.0f, 64.0f, lg.getWidth() * 0.32f);
        auto logoR = lg.removeFromTop (lg.getHeight() - 16.0f);
        g.setFont (juce::Font (juce::FontOptions (logoFace).withHeight (h)));
        g.setColour (Colours::accent.withAlpha (0.25f));
        g.drawFittedText ("Toneical", logoR.translated (0, 1.5f).toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
        g.setColour (Colours::accent);
        g.drawFittedText ("Toneical", logoR.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f);
        g.setColour (Colours::dim);
        g.setFont (uiFont (10.5f));
        g.drawFittedText ("pedals  -  amps  -  cabs  -  NAM", lg.translated (4.0f, -2.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
    }

    auto footer = getLocalBounds().removeFromBottom (footerH).toFloat();
    juce::ColourGradient fg (juce::Colour (0xff1c1f25), 0, footer.getY(), juce::Colour (0xff15171b), 0, footer.getBottom(), false);
    g.setGradientFill (fg);
    g.fillRect (footer);
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (footer.getX(), footer.getY(), footer.getWidth(), 1.0f);
}

// The top bar is a row of "rack modules". Each has a preferred and a minimum width; when the
// window gets narrower they shrink towards their minimum (the tuner gives way first), so
// nothing ever slides on top of anything else. The window can't be made narrower than the sum
// of the minimums (see kMinWidth).
void ToneicalEditor::layoutHeader (juce::Rectangle<int> area)
{
    auto row = area.reduced (14, 8);
    const bool hasAudio = audioButton.isVisible();
    struct Sec { juce::Rectangle<int>* rect; int pref, min; };
    std::vector<Sec> secs = { { &secLogo, 200, 140 }, { &secPreset, 250, 168 }, { &secInput, 230, 172 },
                              { &secTuner, 420, 120 }, { &secOutput, 240, 176 } };
    if (hasAudio) secs.push_back ({ &secAudio, 190, 138 });
    const int gap = 10;
    const int avail = row.getWidth() - gap * (int) (secs.size() - 1);
    int prefSum = 0, minSum = 0;
    for (auto& s : secs) { prefSum += s.pref; minSum += s.min; }

    std::vector<int> w;
    if (avail >= prefSum)
    {
        for (auto& s : secs) w.push_back (s.pref);
        w[3] += avail - prefSum;                          // spare room goes to the tuner
    }
    else
    {
        // take space from the tuner first, then from everyone in proportion to their slack
        int deficit = prefSum - avail;
        for (auto& s : secs) w.push_back (s.pref);
        const int fromTuner = juce::jmin (deficit, secs[3].pref - secs[3].min);
        w[3] -= fromTuner; deficit -= fromTuner;
        if (deficit > 0)
        {
            int slack = 0;
            for (size_t i = 0; i < secs.size(); ++i) if (i != 3) slack += secs[i].pref - secs[i].min;
            for (size_t i = 0; i < secs.size(); ++i)
                if (i != 3 && slack > 0)
                    w[i] -= juce::jmin (secs[i].pref - secs[i].min, (deficit * (secs[i].pref - secs[i].min) + slack - 1) / slack);
        }
    }
    int x = row.getX();
    for (size_t i = 0; i < secs.size(); ++i)
    {
        *secs[i].rect = juce::Rectangle<int> (x, row.getY(), juce::jmax (0, w[i]), row.getHeight());
        x += w[i] + gap;
    }
    if (! hasAudio) secAudio = {};

    // inside the modules: caption strip on top, then two rows of controls
    auto body = [] (juce::Rectangle<int> s) { return s.withTrimmedTop (16).reduced (8, 2); };
    const int rowH = 28;

    {   // PRESET: list on top, Save / Open below
        auto b = body (secPreset);
        presetBox.setBounds (b.removeFromTop (rowH));
        b.removeFromTop (6);
        auto r2 = b.removeFromTop (rowH);
        const int bw = (r2.getWidth() - 6) / 2;
        saveButton.setBounds (r2.removeFromLeft (bw));
        r2.removeFromLeft (6);
        loadButton.setBounds (r2);
    }
    {   // INPUT: channel on top; gain knob, value, meter, Auto below
        auto b = body (secInput);
        inputBox.setBounds (b.removeFromTop (rowH));
        b.removeFromTop (6);
        auto r2 = b.removeFromTop (rowH);
        inGain.setBounds (r2.removeFromLeft (rowH + 2));
        r2.removeFromLeft (4);
        calibButton.setBounds (r2.removeFromRight (44).withSizeKeepingCentre (44, 22));
        r2.removeFromRight (6);
        inValue.setBounds (r2.removeFromTop (14));
        inMeter.setBounds (r2.removeFromTop (12).reduced (0, 1));
    }
    tuner.setBounds (secTuner.reduced (0, 1));   // the tuner is its own screen, no caption
    {   // OUTPUT: master knob, value + meter, mute
        auto b = body (secOutput);
        outVol.setBounds (b.removeFromLeft (52).withSizeKeepingCentre (52, 52));
        b.removeFromLeft (6);
        auto top = b.removeFromTop (rowH);
        outValue.setBounds (top.removeFromTop (14));
        outMeter.setBounds (top.removeFromTop (12).reduced (0, 1));
        b.removeFromTop (6);
        muteButton.setBounds (b.removeFromTop (rowH));
    }
    if (hasAudio)
    {   // AUDIO: device dialog + buffer size
        auto b = body (secAudio);
        audioButton.setBounds (b.removeFromTop (rowH));
        b.removeFromTop (6);
        bufferBox.setBounds (b.removeFromTop (rowH));
    }
}

void ToneicalEditor::resized()
{
    auto r = getLocalBounds();
    layoutHeader (r.removeFromTop (headerH));

    status.setBounds (r.removeFromBottom (footerH).reduced (12, 0));
    browser.setBounds (r.removeFromRight (juce::jlimit (240, 320, getWidth() / 5)));
    stripView.setBounds (r.removeFromBottom (stripH));
    strip->setSize (juce::jmax (strip->preferredWidth(), stripView.getWidth()), stripH - 8);
    stage.setBounds (r);
    layoutStage();
}

void ToneicalEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildChain();
    repaint();
}

int ToneicalEditor::indexOf (const ModulePtr& m) const
{
    const auto mods = proc.getChain().getModules();
    for (size_t i = 0; i < mods.size(); ++i)
        if (mods[i] == m) return (int) i;
    return -1;
}

void ToneicalEditor::rebuildChain()
{
    const auto mods = proc.getChain().getModules();
    strip->setModules (mods);
    strip->setSize (juce::jmax (strip->preferredWidth(), stripView.getWidth()), juce::jmax (40, stripView.getHeight() - 8));

    // keep the selection if that pedal is still on the board, otherwise pick the amp (or the first pedal)
    if (selected == nullptr || indexOf (selected) < 0)
    {
        ModulePtr pick;
        // --select N on the command line opens the N-th pedal (handy for screenshots)
        const auto args = juce::StringArray::fromTokens (juce::JUCEApplicationBase::getCommandLineParameters(), true);
        if (const int at = args.indexOf ("--select"); at >= 0 && at + 1 < args.size())
            if (const int n = args[at + 1].getIntValue(); n >= 1 && n <= (int) mods.size()) pick = mods[(size_t) n - 1];
        if (pick == nullptr)
            for (auto& m : mods) if (m->category() == "Amp") { pick = m; break; }
        if (pick == nullptr && ! mods.empty()) pick = mods.front();
        selectModule (pick);
    }
    else
        strip->setSelected (selected);
}

void ToneicalEditor::selectModule (ModulePtr m)
{
    selected = m;
    strip->setSelected (m);
    bigPedal.reset();
    if (m != nullptr)
    {
        PedalComponent::Callbacks cb;
        cb.onRemove = [this] (PedalComponent& p)
        {
            const int idx = indexOf (p.getModule());
            juce::MessageManager::callAsync ([this, idx, safe = juce::Component::SafePointer<ToneicalEditor> (this)]
                                             { if (safe != nullptr) proc.getChain().remove (idx); });
        };
        cb.onMove = [this] (PedalComponent& p, int delta)
        {
            const int idx = indexOf (p.getModule());
            juce::MessageManager::callAsync ([this, idx, delta, safe = juce::Component::SafePointer<ToneicalEditor> (this)]
                                             { if (safe != nullptr) proc.getChain().move (idx, idx + delta); });
        };
        cb.onInsertAfter = [this] (PedalComponent& p) { showAddMenu (indexOf (p.getModule()) + 1); };
        bigPedal = std::make_unique<PedalComponent> (m, cb);
        stage.addChildComponent (*bigPedal);
        stage.hint.clear();
    }
    else
        stage.hint = "The board is empty.\nDrag gear from the right onto the pedalboard below, or click + to add a pedal.";
    layoutStage();
    if (bigPedal != nullptr)
    {
        bigPedal->setAlpha (0.0f);
        juce::Desktop::getInstance().getAnimator().fadeIn (bigPedal.get(), 170);   // a short fade when you pick a pedal
    }
    stage.repaint();
}

void ToneicalEditor::layoutStage()
{
    if (bigPedal == nullptr) return;
    const auto area = stage.getLocalBounds().reduced (20, 16);
    const float w = (float) bigPedal->getPreferredWidth(), h = (float) bigPedal->getPreferredHeight();
    const float scale = juce::jlimit (0.6f, 2.2f, juce::jmin ((float) area.getHeight() / h, (float) area.getWidth() / w));
    bigPedal->setBounds (0, 0, (int) w, (int) h);
    bigPedal->setTransform (juce::AffineTransform::scale (scale)
                                .translated ((float) area.getCentreX() - w * scale * 0.5f, (float) area.getCentreY() - h * scale * 0.5f));
}

void ToneicalEditor::insertGear (const juce::String& typeId, int index)
{
    auto m = ModuleFactory::create (typeId);
    if (m == nullptr) return;
    proc.getChain().insert (m, index);   // rebuilds the strip via the change message...
    rebuildChain();
    selectModule (m);                     // ...and the new pedal opens in the big view
    if (index < 0)
        stripView.setViewPosition (strip->getWidth(), 0);
}

void ToneicalEditor::showAddMenu (int insertIndex)
{
    juce::PopupMenu menu;
    juce::StringArray cats;
    for (auto& info : ModuleFactory::all()) cats.addIfNotAlreadyThere (info.category);

    const auto& all = ModuleFactory::all();
    for (auto& cat : cats)
    {
        menu.addSectionHeader (cat);
        for (size_t i = 0; i < all.size(); ++i)
            if (all[i].category == cat)
                menu.addItem ((int) i + 1, all[i].name);
    }

    juce::Component::SafePointer<ToneicalEditor> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition(), [safe, insertIndex] (int r)
    {
        if (safe == nullptr || r <= 0) return;
        const auto& list = ModuleFactory::all();
        safe->insertGear (list[(size_t) r - 1].typeId, insertIndex);
    });
}

void ToneicalEditor::refreshPresetList()
{
    presetBox.clear (juce::dontSendNotification);
    const auto& f = Presets::factory();
    for (auto& cat : Presets::categories())
    {
        presetBox.addSectionHeading (cat);
        for (size_t i = 0; i < f.size(); ++i)
            if (f[i].category == cat)
                presetBox.addItem (f[i].name, (int) i + 1);
        presetBox.addSeparator();
    }

    userPresets = Presets::userFolder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + Presets::fileExtension);
    userPresets.sort();
    if (! userPresets.isEmpty())
    {
        presetBox.addSectionHeading ("My presets");
        for (int i = 0; i < userPresets.size(); ++i)
            presetBox.addItem (userPresets[i].getFileNameWithoutExtension(), 1000 + i);
    }
}

void ToneicalEditor::savePreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Save preset",
                                                   Presets::userFolder().getChildFile (proc.currentPresetName + Presets::fileExtension),
                                                   juce::String ("*") + Presets::fileExtension);
    juce::Component::SafePointer<ToneicalEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              auto file = fc.getResult().withFileExtension (Presets::fileExtension);
                              juce::String err;
                              if (! Presets::save (safe->proc.getChain(), file, err))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save failed", err);
                              safe->proc.currentPresetName = file.getFileNameWithoutExtension();
                              safe->refreshPresetList();
                              safe->repaint();
                          });
}

void ToneicalEditor::loadPresetFromDisk()
{
    chooser = std::make_unique<juce::FileChooser> ("Open preset", Presets::userFolder(), juce::String ("*") + Presets::fileExtension);
    juce::Component::SafePointer<ToneicalEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr || fc.getResult() == juce::File()) return;
                              juce::String err;
                              if (Presets::load (safe->proc.getChain(), fc.getResult(), err))
                                  safe->proc.currentPresetName = fc.getResult().getFileNameWithoutExtension();
                              else
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Open failed", err);
                              safe->repaint();
                          });
}

void ToneicalEditor::updateStatus()
{
    juce::String s;
#if JucePlugin_Build_Standalone
    if (auto* holder = standaloneHolder())
    {
        auto& dm = holder->deviceManager;
        if (auto* dev = dm.getCurrentAudioDevice())
        {
            const double rate = dev->getCurrentSampleRate();
            const int buf = dev->getCurrentBufferSizeSamples();
            const int lat = dev->getInputLatencyInSamples() + dev->getOutputLatencyInSamples() + 2 * buf;
            s << dev->getTypeName() << ": " << dev->getName() << "   |   "
              << juce::String (rate / 1000.0, 1) << " kHz, " << buf << " samples"
              << "   |   ~" << juce::String (1000.0 * lat / rate, 1) << " ms round trip"
              << "   |   CPU " << juce::String (dm.getCpuUsage() * 100.0, 0) << "%";
            if (proc.getNumInputChannelsActive() == 0)
                s << "   |   NO INPUT - click Audio... and enable your interface's inputs";
        }
        else
            s << "No audio device - click Audio... (top right)";
    }
    else
#endif
    {
        s << juce::String (proc.getCurrentSampleRate() / 1000.0, 1) << " kHz";
    }
    s << "   |   click a pedal below to edit it, drag gear in from the right";
    status.setText (s, juce::dontSendNotification);
}

void ToneicalEditor::setBufferSize (int samples)
{
   #if JucePlugin_Build_Standalone
    auto* h = standaloneHolder();
    if (h == nullptr) return;
    auto& dm = h->deviceManager;
    auto* dev = dm.getCurrentAudioDevice();
    if (dev == nullptr) { status.setText ("No audio device - click Audio... first", juce::dontSendNotification); return; }

    if (dev->getTypeName() == "JACK")
    {
        // JACK under PipeWire: the server decides the buffer ("quantum"); ask it for ours.
        auto run = [] (const juce::String& cmd) -> juce::String
        {
            juce::ChildProcess p;
            if (! p.start (cmd)) return "!";
            auto out = p.readAllProcessOutput();
            return p.getExitCode() == 0 ? out : "!";
        };
        const auto settings = run ("pw-metadata -n settings 0");
        if (settings == "!")
        {
            status.setText ("This JACK server isn't PipeWire - set the buffer size in your JACK tool (e.g. QjackCtl), "
                            "or choose ALSA in Audio...", juce::dontSendNotification);
            return;
        }
        if (savedMinQuantum.isEmpty())
        {
            // remember the old minimum so we can restore it when Toneical closes
            const auto line = juce::StringArray::fromLines (settings);
            savedMinQuantum = "32";
            for (auto& l : line)
                if (l.contains ("'clock.min-quantum'"))
                    savedMinQuantum = l.fromFirstOccurrenceOf ("value:'", false, false).upToFirstOccurrenceOf ("'", false, false);
            if (savedMinQuantum.getIntValue() <= 0) savedMinQuantum = "32";
        }
        run ("pw-metadata -n settings 0 clock.min-quantum " + juce::String (juce::jmin (samples, savedMinQuantum.getIntValue())));
        run ("pw-metadata -n settings 0 clock.force-quantum " + juce::String (samples));
        status.setText ("Asked PipeWire for a " + juce::String (samples) + "-sample buffer ("
                        + juce::String (samples / dev->getCurrentSampleRate() * 1000.0, 2) + " ms). Crackles? Pick the next bigger size.",
                        juce::dontSendNotification);
    }
    else
    {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        dm.getAudioDeviceSetup (setup);
        setup.bufferSize = samples;
        const auto err = dm.setAudioDeviceSetup (setup, true);
        status.setText (err.isNotEmpty() ? "Buffer size not accepted: " + err
                                         : "Buffer: " + juce::String (samples) + " samples", juce::dontSendNotification);
    }
    lastShownBuffer = -1;   // re-read what the device really uses
   #else
    juce::ignoreUnused (samples);
   #endif
}

void ToneicalEditor::timerCallback()
{
   #if JucePlugin_Build_Standalone
    if (auto* h = standaloneHolder())
        if (auto* dev = h->deviceManager.getCurrentAudioDevice())
        {
            const int b = dev->getCurrentBufferSizeSamples();
            if (b != lastShownBuffer)
            {
                lastShownBuffer = b;
                if (bufferBox.indexOfItemId (b) < 0)
                    bufferBox.addItem (juce::String (b) + " samples (" + juce::String (b / dev->getCurrentSampleRate() * 1000.0, 2) + " ms)", b);
                bufferBox.setSelectedId (b, juce::dontSendNotification);
            }
        }
   #endif
    if (calibTicks > 0)
    {
        calibButton.setButtonText (juce::String ((calibTicks + 29) / 30) + "s");
        status.setText ("Calibrating input: play HARD - full strums, all strings - until the countdown ends...", juce::dontSendNotification);
        if (--calibTicks == 0)
        {
            const float peak = proc.finishInputCalibration();
            calibButton.setButtonText ("Auto");
            calibButton.setToggleState (false, juce::dontSendNotification);
            if (peak < 0.001f)
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "No guitar signal",
                    "Nothing arrived on the selected input. Check the input selector (Input 1 / 2), the interface's gain knob "
                    "and that the interface's inputs are enabled in Audio...");
            else
            {
                const float gainDb = juce::jlimit (-24.0f, 24.0f, ToneicalProcessor::kCalibTargetDb - juce::Decibels::gainToDecibels (peak));
                if (auto* p = proc.getParams().getParameter ("ingain"))
                    p->setValueNotifyingHost (p->convertTo0to1 (gainDb));
                juce::String msg;
                msg << "Input calibrated: your peaks were " << juce::String (juce::Decibels::gainToDecibels (peak), 1)
                    << " dBFS, input gain set to " << (gainDb > 0 ? "+" : "") << juce::String (gainDb, 1) << " dB.";
                if (peak > 0.97f) msg << "  Your interface is clipping - turn its gain knob down a bit and calibrate again.";
                else if (gainDb > 18.0f) msg << "  The signal is very low - turn the interface's gain knob up and calibrate again.";
                status.setText (msg, juce::dontSendNotification);
                tick = 1; // keep the message visible for a few seconds
                statusHoldTicks = 6 * 30;
            }
        }
    }
    if (statusHoldTicks > 0) --statusHoldTicks;

    inMeter.push (proc.takeInputPeak());
    outMeter.push (proc.takeOutputPeak());
    if (++tick % 3 == 0)
        tuner.set (proc.getTuner().analyse());
    if (tick % 15 == 0 && calibTicks == 0 && statusHoldTicks == 0)
        updateStatus();
}
