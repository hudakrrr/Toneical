// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

// switches over gear shapes handle the special shapes and send the rest to "default:" on purpose
#if defined (__GNUC__)
 #pragma GCC diagnostic ignored "-Wswitch-enum"
#endif

#include "PedalComponent.h"
#include "Presets.h"
#include "BinaryData.h"

namespace pg
{
using PedalArt::Shape;
using PedalArt::KnobStyle;

namespace
{
    juce::File lastBrowseDir;

    juce::String formatValue (const ParamSpec& s, double v)
    {
        if (s.maxValue - s.minValue <= 10.0f && s.suffix.isEmpty())
            return juce::String (v, 1);
        if (std::abs (v) >= 1000.0 && s.suffix.trim() == "Hz")
            return juce::String (v / 1000.0, 1) + " kHz";
        if (s.suffix.trim() == "dB")
            return (v > 0 ? "+" : "") + juce::String (v, 1) + " dB";
        return juce::String (v, std::abs (v) < 10.0 ? 1 : 0) + s.suffix;
    }

    juce::Colour contrastOn (juce::Colour c)
    {
        return c.getPerceivedBrightness() > 0.55f ? juce::Colour (0xff17171a) : juce::Colour (0xfff1eee8);
    }
}

PedalComponent::PedalComponent (ModulePtr m, Callbacks callbacks)
    : module (std::move (m)), cb (std::move (callbacks))
{
    style = PedalArt::styleFor (module->typeId(), juce::Colour (module->colour()));
    switch (style.shape)
    {
        case Shape::compact:                 switchKind = Switch::treadle; break;
        case Shape::head: case Shape::cab:   switchKind = Switch::toggle;  break;
        case Shape::digital:                 switchKind = Switch::soft;    break;
        default:                             switchKind = Switch::stomp;   break;
    }

    // lettering colour where the controls sit
    juce::Colour ink = style.ink;
    if (style.shape == Shape::compact || style.shape == Shape::cab || style.shape == Shape::digital) ink = juce::Colour (0xffeae6dd);
    if (style.shape == Shape::head) ink = contrastOn (style.panel);
    if (style.knob == KnobStyle::fader) ink = juce::Colour (0xffeae6dd);

    const bool fader = style.knob == KnobStyle::fader;
    for (int i = 0; i < module->numParams(); ++i)
    {
        const auto& s = module->getSpecs()[(size_t) i];
        switch (s.kind)
        {
            case ParamSpec::Kind::knob:
            {
                Knob k;
                k.index = i;
                k.slider = std::make_unique<juce::Slider> (fader ? juce::Slider::LinearVertical : juce::Slider::RotaryHorizontalVerticalDrag,
                                                           juce::Slider::TextBoxBelow);
                auto& sl = *k.slider;
                sl.getProperties().set ("knob", (int) style.knob);
                sl.setRange (s.minValue, s.maxValue, 0.0);
                if (s.skewCentre > s.minValue && s.skewCentre < s.maxValue)
                    sl.setSkewFactorFromMidPoint (s.skewCentre);
                sl.setDoubleClickReturnValue (true, s.defaultValue);
                sl.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 70, 16);
                sl.setColour (juce::Slider::rotarySliderOutlineColourId, style.shape == Shape::head ? style.panel : style.body);
                sl.setColour (juce::Slider::textBoxTextColourId, ink.withAlpha (0.85f));
                sl.textFromValueFunction = [s] (double v) { return formatValue (s, v); };
                sl.setValue (module->get (i), juce::dontSendNotification);
                sl.updateText();
                sl.onValueChange = [this, i, &sl] { module->set (i, (float) sl.getValue()); };
                sl.setTooltip (s.name + " (double-click to reset)");
                addAndMakeVisible (sl);

                k.label = std::make_unique<juce::Label> (juce::String(), s.name.toUpperCase());
                k.label->setJustificationType (juce::Justification::centred);
                k.label->setFont (uiFont (style.shape == Shape::compact ? 9.5f : 10.5f, true));
                k.label->setMinimumHorizontalScale (0.6f);
                k.label->setColour (juce::Label::textColourId, ink);
                k.label->setInterceptsMouseClicks (false, false);
                addAndMakeVisible (*k.label);
                knobs.push_back (std::move (k));
                break;
            }
            case ParamSpec::Kind::choice:
            {
                auto cbx = std::make_unique<juce::ComboBox>();
                cbx->addItemList (s.choices, 1);
                cbx->setSelectedItemIndex ((int) module->get (i), juce::dontSendNotification);
                cbx->setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xe0141518));
                cbx->setColour (juce::ComboBox::outlineColourId, juce::Colours::black);
                auto* raw = cbx.get();
                cbx->onChange = [this, i, raw] { module->set (i, (float) raw->getSelectedItemIndex()); };
                cbx->setTooltip (s.name);
                addAndMakeVisible (*cbx);
                combos.emplace_back (i, std::move (cbx));
                break;
            }
            case ParamSpec::Kind::toggle:
            {
                auto t = std::make_unique<juce::ToggleButton> (s.name);
                t->setToggleState (module->get (i) > 0.5f, juce::dontSendNotification);
                t->setColour (juce::ToggleButton::textColourId, ink);
                auto* raw = t.get();
                t->onClick = [this, i, raw] { module->set (i, raw->getToggleState() ? 1.0f : 0.0f); };
                addAndMakeVisible (*t);
                toggles.emplace_back (i, std::move (t));
                break;
            }
        }
    }

    // size of the enclosure for this kind of gear
    const int n = (int) knobs.size();
    switch (style.shape)
    {
        case Shape::stomp:   preferredWidth = juce::jmax (214, 40 + juce::jmin (n <= 4 ? (n == 4 ? 2 : n) : 3, 3) * 84); preferredHeight = 384; break;
        case Shape::slim:    preferredWidth = n <= 1 ? 160 : 186; preferredHeight = 350; break;
        case Shape::compact: preferredWidth = n >= 4 ? 246 : 214; preferredHeight = 384; break;
        case Shape::wide:    preferredWidth = juce::jmax (320, 44 + (fader ? n * 60 : juce::jmin (n, 5) * 80)); preferredHeight = fader ? 384 : 330; break;
        case Shape::wedge:   preferredWidth = juce::jmax (270, 40 + juce::jmin (n, 4) * 72); preferredHeight = 370; break;
        case Shape::round:   preferredWidth = 350; preferredHeight = 350; break;
        case Shape::head:    preferredWidth = 190 + n * 72 + 110; preferredHeight = 320; break;
        case Shape::cab:     preferredWidth = 430; preferredHeight = 410; break;
        case Shape::digital: preferredWidth = 300; preferredHeight = 400; break;
    }

    if (module->hasFileSlot())
    {
        fileButton.setButtonText (module->fileSlotLabel());
        fileButton.onClick = [this] { chooseFile(); };
        clearFileButton.setTooltip ("Unload");
        clearFileButton.onClick = [this] { module->clearFile(); updateStatus(); };
        fileLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold)));
        fileLabel.setColour (juce::Label::textColourId, juce::Colour (0xff59e3a2));
        fileLabel.setJustificationType (juce::Justification::centred);
        fileLabel.setMinimumHorizontalScale (0.6f);
        statusLabel.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain)));
        statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff59e3a2).withAlpha (0.75f));
        statusLabel.setJustificationType (juce::Justification::centredTop);
        statusLabel.setMinimumHorizontalScale (0.7f);
        for (auto* c : std::initializer_list<juce::Component*> { &fileButton, &clearFileButton, &fileLabel, &statusLabel })
            addAndMakeVisible (c);
        updateStatus();
    }
    else if (module->showsStatus())
    {
        statusLabel.setFont (uiFont (10.5f));
        statusLabel.setColour (juce::Label::textColourId, style.ink.withAlpha (0.8f));
        statusLabel.setJustificationType (juce::Justification::centred);
        statusLabel.setMinimumHorizontalScale (0.6f);
        addAndMakeVisible (statusLabel);
        updateStatus();
    }
    startTimerHz (30);   // the LED follows your playing, the footswitch springs back

    if (module->credit().isNotEmpty())
        setTooltip (module->credit());

    active = ! module->bypassed.load();
    setSize (preferredWidth, preferredHeight);
}

PedalComponent::~PedalComponent() = default;

void PedalComponent::refreshFromModule()
{
    for (auto& k : knobs) k.slider->setValue (module->get (k.index), juce::dontSendNotification);
    for (auto& [i, c] : combos) c->setSelectedItemIndex ((int) module->get (i), juce::dontSendNotification);
    for (auto& [i, t] : toggles) t->setToggleState (module->get (i) > 0.5f, juce::dontSendNotification);
    active = ! module->bypassed.load();
    repaint();
}

void PedalComponent::timerCallback()
{
    module->takeGlow (glow);
    if (std::abs (glow - glowShown) > 0.03f) { glowShown = glow; repaint (led.expanded (led.getWidth() * 4.0f).toNearestInt()); }
    if (press > 0.0f) { press = std::max (0.0f, press - 0.2f); repaint (footswitch.expanded (6).toNearestInt()); }
    if (++ticks % 8 == 0 && (module->hasFileSlot() || module->showsStatus())) updateStatus();
}

void PedalComponent::updateStatus()
{
    if (! module->hasFileSlot())
    {
        statusLabel.setText (module->getStatusText(), juce::dontSendNotification);
        return;
    }
    const auto f = module->getLoadedFile();
    fileLabel.setText (f == juce::File() ? "(empty)" : f.getFileNameWithoutExtension(), juce::dontSendNotification);
    fileLabel.setTooltip (f.getFullPathName());
    const auto status = module->getStatusText();
    statusLabel.setText (status, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId,
                           status.containsIgnoreCase ("warning") || status.containsIgnoreCase ("failed")
                               ? Colours::warn : fileLabel.findColour (juce::Label::textColourId).withAlpha (0.75f));
    clearFileButton.setVisible (f != juce::File());
}

void PedalComponent::chooseFile()
{
    // start in ~/Toneical/Captures (NAM) or ~/Toneical/IRs, created by the installer
    auto start = lastBrowseDir.isDirectory() ? lastBrowseDir
                                             : Presets::homeFolder().getChildFile (module->typeId() == "nam" ? "Captures" : "IRs");
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userHomeDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Choose " + module->displayName() + " file", start, module->fileWildcard());
    juce::Component::SafePointer<PedalComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe] (const juce::FileChooser& fc)
                          {
                              if (safe == nullptr) return;
                              const auto file = fc.getResult();
                              if (file == juce::File()) return;
                              lastBrowseDir = file.getParentDirectory();
                              juce::String err;
                              if (! safe->module->loadFile (file, err))
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Could not load file", err);
                              safe->updateStatus();
                          });
}

void PedalComponent::toggleBypass()
{
    press = 1.0f;
    active = ! active;
    module->bypassed.store (! active);
    repaint();
}

void PedalComponent::mouseDown (const juce::MouseEvent& e)
{
    if (footswitch.contains (e.position) || led.expanded (4).contains (e.position))
    {
        toggleBypass();
        return;
    }
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.addSectionHeader (module->displayName());
        m.addItem (1, active ? "Bypass" : "Engage");
        m.addItem (2, "Move left");
        m.addItem (3, "Move right");
        m.addItem (4, "Insert pedal after this...");
        m.addSeparator();
        m.addItem (5, "Remove");
        juce::Component::SafePointer<PedalComponent> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [safe] (int r)
        {
            if (safe == nullptr) return;
            auto& p = *safe;
            switch (r)
            {
                case 1: p.toggleBypass(); break;
                case 2: if (p.cb.onMove) p.cb.onMove (p, -1); break;
                case 3: if (p.cb.onMove) p.cb.onMove (p, +1); break;
                case 4: if (p.cb.onInsertAfter) p.cb.onInsertAfter (p); break;
                case 5: if (p.cb.onRemove) p.cb.onRemove (p); break;
                default: break;
            }
        });
    }
}

//==============================================================================
void PedalComponent::placeKnobs (juce::Rectangle<int> area, int cols, int maxKnob)
{
    const int n = (int) knobs.size();
    if (n == 0 || area.isEmpty()) return;
    cols = juce::jlimit (1, n, cols);
    const int rows = (n + cols - 1) / cols;
    const int cellW = area.getWidth() / cols, cellH = area.getHeight() / rows;
    const bool fader = style.knob == KnobStyle::fader;

    for (int k = 0; k < n; ++k)
    {
        const int row = k / cols, col = k % cols;
        const int inRow = juce::jmin (cols, n - row * cols);
        const int xOffset = (cols - inRow) * cellW / 2;
        juce::Rectangle<int> cell (area.getX() + xOffset + col * cellW, area.getY() + row * cellH, cellW, cellH);
        auto& kn = knobs[(size_t) k];
        kn.label->setBounds (cell.removeFromTop (14));
        kn.slider->setTextBoxStyle (juce::Slider::TextBoxBelow, true, juce::jmax (40, cellW - 2), 16);
        if (fader)
            kn.slider->setBounds (cell.reduced (2, 0));
        else
        {
            const int d = juce::jmin (maxKnob, cellW - 6, cell.getHeight() - 18);
            kn.slider->setBounds (cell.withSizeKeepingCentre (cellW, d + 18).withY (cell.getY()));
        }
    }
}

void PedalComponent::placeRow (juce::Rectangle<int> area)
{
    const int n = (int) (combos.size() + toggles.size());
    if (n == 0 || area.isEmpty()) return;
    const int w = juce::jmin (150, area.getWidth() / n);
    auto row = area.withSizeKeepingCentre (w * n, juce::jmin (26, area.getHeight()));
    for (auto& [i, c] : combos)  c->setBounds (row.removeFromLeft (w).reduced (3, 0));
    for (auto& [i, t] : toggles) t->setBounds (row.removeFromLeft (w).reduced (3, 0));
}

void PedalComponent::resized()
{
    const auto bounds = getLocalBounds().toFloat().reduced (6.0f).withTrimmedBottom (4.0f);
    body = bounds;
    if (style.shape == Shape::round)
        body = bounds.withSizeKeepingCentre (juce::jmin (bounds.getWidth(), bounds.getHeight()), juce::jmin (bounds.getWidth(), bounds.getHeight()));
    const auto b = body.toNearestInt();
    const int n = (int) knobs.size();
    const int rowItems = (int) (combos.size() + toggles.size());
    nameArea = panelArea = grilleArea = screenArea = {};

    auto stdSwitch = [this] (juce::Rectangle<int> area)
    {
        footswitch = juce::Rectangle<float> (54, 54).withCentre (area.getCentre().toFloat());
    };

    switch (style.shape)
    {
        case Shape::stomp:
        case Shape::slim:
        case Shape::wedge:
        {
            auto r = b.reduced (18, 14);
            const int cols = style.shape == Shape::slim ? juce::jmin (n, 2)
                           : style.shape == Shape::wedge ? juce::jmin (n, 4)
                           : (n == 4 ? 2 : juce::jmin (n, 3));
            const int rows = n == 0 ? 0 : (n + cols - 1) / cols;
            if (style.shape == Shape::wedge)
            {
                auto face = b.withHeight ((int) (body.getHeight() * 0.44f)).reduced (16, 10);
                led = juce::Rectangle<float> (11, 11).withCentre ({ (float) face.getRight() - 4.0f, (float) face.getY() + 4.0f });
                placeKnobs (face.withTrimmedTop (8), cols, 62);
                r.setTop (face.getBottom() + 14);
            }
            else
            {
                placeKnobs (r.removeFromTop (rows * (style.shape == Shape::slim && n == 1 ? 110 : 98)), cols, style.shape == Shape::slim && n == 1 ? 84 : 58);
            }
            auto sw = r.removeFromBottom (70);
            stdSwitch (sw);
            if (style.shape != Shape::wedge)
                led = juce::Rectangle<float> (11, 11).withCentre ({ footswitch.getRight() + 24.0f, footswitch.getCentreY() });
            for (auto& [i, c] : combos) { c->setBounds (r.removeFromTop (26).reduced (8, 0)); r.removeFromTop (6); }
            for (auto& [i, t] : toggles) t->setBounds (r.removeFromBottom (24).reduced (12, 0));
            if (module->showsStatus()) statusLabel.setBounds (r.removeFromBottom (18));
            nameArea = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 84));
            break;
        }
        case Shape::wide:
        {
            auto r = b.reduced (22, 16);
            const bool fader = style.knob == KnobStyle::fader;
            led = juce::Rectangle<float> (11, 11).withCentre ({ (float) b.getRight() - 22.0f, (float) b.getY() + 20.0f });
            r.removeFromTop (12);
            if (fader)
            {
                panelArea = r.removeFromTop ((int) (body.getHeight() * 0.56f));
                placeKnobs (panelArea.reduced (6, 8), n, 0);
            }
            else
            {
                const int cols = juce::jmin (n, 5), rows = n == 0 ? 0 : (n + cols - 1) / cols;
                placeKnobs (r.removeFromTop (rows * 90), cols, 60);
            }
            auto sw = r.removeFromBottom (70);
            stdSwitch (sw);
            if (rowItems > 0) { placeRow (r.removeFromTop (30)); r.removeFromTop (4); }
            nameArea = r.withSizeKeepingCentre (r.getWidth(), juce::jmin (r.getHeight(), 76));
            break;
        }
        case Shape::compact:
        {
            panelArea = b.withHeight ((int) (body.getHeight() * 0.36f)).reduced (14, 14);
            placeKnobs (panelArea.reduced (4, 6), juce::jmax (1, n), 46);
            auto rest = b.withTop (panelArea.getBottom());
            led = juce::Rectangle<float> (10, 10).withCentre ({ body.getCentreX(), (float) rest.getY() + 12.0f });
            int extra = 0;
            if (rowItems > 0)
            {
                placeRow (rest.withTrimmedTop (24).withHeight (26).reduced (14, 0));
                extra = 30;
            }
            nameArea = rest.withTrimmedTop (extra).withHeight (62).withTrimmedTop (20).reduced (14, 0);
            auto tread = rest.withTrimmedTop (66 + extra).reduced (16, 0).withTrimmedBottom (14);
            footswitch = tread.toFloat();
            break;
        }
        case Shape::round:
        {
            const float d = body.getWidth();
            auto at = [&] (float x, float y, float w, float h)
            { return juce::Rectangle<float> (body.getX() + x * d, body.getY() + y * d, w * d, h * d).toNearestInt(); };
            led = juce::Rectangle<float> (11, 11).withCentre ({ body.getCentreX(), body.getY() + d * 0.075f });
            placeKnobs (at (0.2f, 0.11f, 0.6f, 0.3f), juce::jmin (n, 3), 64);
            auto row = at (0.28f, 0.42f, 0.44f, 0.08f);
            placeRow (row);
            nameArea = at (0.15f, 0.53f, 0.7f, 0.11f);
            footswitch = juce::Rectangle<float> (54, 54).withCentre ({ body.getCentreX(), body.getY() + d * 0.76f });
            break;
        }
        case Shape::head:
        {
            grilleArea = b.withHeight ((int) (body.getHeight() * 0.46f)).reduced (16, 16).withTrimmedBottom (-6);
            nameArea = grilleArea.withSizeKeepingCentre (juce::jmin (300, grilleArea.getWidth() - 40), 54);
            if (module->showsStatus())
                statusLabel.setBounds (grilleArea.withTop (nameArea.getBottom() + 4).withHeight (20).reduced (20, 0));
            panelArea = b.withTop (grilleArea.getBottom() + 6).reduced (16, 0).withTrimmedBottom (16);
            auto p = panelArea.reduced (14, 8);
            auto left = p.removeFromLeft (170);
            for (auto& [i, c] : combos) c->setBounds (left.withSizeKeepingCentre (left.getWidth() - 10, 30));
            auto right = p.removeFromRight (100);
            // pilot jewel on the left, power toggle on the right, side by side like a real amp panel
            led = juce::Rectangle<float> (16, 16).withCentre ({ (float) right.getX() + 24.0f, (float) right.getCentreY() - 4.0f });
            footswitch = juce::Rectangle<float> (30, 44).withCentre ({ (float) right.getX() + 74.0f, (float) right.getCentreY() - 4.0f });
            for (auto& [i, t] : toggles) t->setBounds (p.removeFromBottom (22));
            placeKnobs (p, juce::jmax (1, n), 56);
            break;
        }
        case Shape::cab:
        {
            panelArea = b.withHeight (128).reduced (16, 16).withTrimmedBottom (-6);
            grilleArea = b.withTop (panelArea.getBottom() + 6).reduced (16, 0).withTrimmedBottom (16);
            auto p = panelArea.reduced (10, 6);
            auto left = p.removeFromLeft (150);
            nameArea = left.removeFromTop (30);
            for (auto& [i, c] : combos) c->setBounds (left.withSizeKeepingCentre (left.getWidth(), 28));
            auto right = p.removeFromRight (54);
            footswitch = juce::Rectangle<float> (26, 40).withCentre ({ (float) right.getCentreX(), (float) right.getCentreY() - 4.0f });
            led = footswitch;   // the rocker is lit, no separate LED
            placeKnobs (p, juce::jmax (1, n), 46);
            break;
        }
        case Shape::digital:
        {
            auto r = b.reduced (20, 16);
            nameArea = r.removeFromTop (30);
            screenArea = r.removeFromTop (92);
            {
                auto s = screenArea.reduced (10, 8);
                fileLabel.setBounds (s.removeFromTop (34));
                statusLabel.setBounds (s);
            }
            r.removeFromTop (8);
            if (module->hasFileSlot())
            {
                auto row = r.removeFromTop (28);
                clearFileButton.setBounds (row.removeFromRight (28));
                row.removeFromRight (6);
                fileButton.setBounds (row);
                r.removeFromTop (8);
            }
            auto sw = r.removeFromBottom (56);
            footswitch = juce::Rectangle<float> (74, 36).withCentre (sw.getCentre().toFloat());
            led = juce::Rectangle<float> (10, 10).withCentre ({ footswitch.getRight() + 22.0f, footswitch.getCentreY() });
            if (rowItems > 0) placeRow (r.removeFromBottom (30));
            placeKnobs (r, n <= 3 ? juce::jmax (1, n) : 2, 52);
            break;
        }
    }
}

//==============================================================================
void PedalComponent::paint (juce::Graphics& g)
{
    PedalArt::drawBody (g, body, style);

    auto nameFont = [] (float h, bool italic = false)
    {
        return juce::Font (juce::FontOptions (h, juce::Font::bold | (italic ? juce::Font::italic : 0)));
    };
    const auto name = module->displayName();

    switch (style.shape)
    {
        case Shape::compact:
        {
            const auto p = panelArea.toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.fillRoundedRectangle (p.translated (0, 2), 6.0f);
            g.setColour (style.panel);
            g.fillRoundedRectangle (p, 6.0f);
            // treadle: rubber pad with grip ridges, hinged at the top
            const auto t = footswitch.translated (0, press * 2.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (t.translated (0, 4), 10.0f);
            juce::ColourGradient pad (style.body.brighter (0.15f), t.getX(), t.getY(), style.body.darker (0.3f), t.getX(), t.getBottom(), false);
            g.setGradientFill (pad);
            g.fillRoundedRectangle (t, 10.0f);
            g.setColour (juce::Colour (0xff1f2023));
            const auto rubber = t.reduced (t.getWidth() * 0.12f, t.getHeight() * 0.1f).withTrimmedBottom (t.getHeight() * 0.25f);
            g.fillRoundedRectangle (rubber, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            for (float y = rubber.getY() + 8; y < rubber.getBottom() - 4; y += 7.0f)
                g.fillRoundedRectangle (rubber.getX() + 8, y, rubber.getWidth() - 16, 2.5f, 1.0f);
            g.setColour (style.ink);
            g.setFont (nameFont (20.0f));
            g.drawFittedText (name.toUpperCase(), nameArea, juce::Justification::centred, 1, 0.6f);
            g.setColour (style.ink.withAlpha (0.7f));
            g.setFont (nameFont (10.0f));
            g.drawText (module->category().toUpperCase(), t.withTrimmedTop (t.getHeight() - 26), juce::Justification::centred);
            PedalArt::drawLed (g, led, active, glow);
            break;
        }
        case Shape::head:
        {
            PedalArt::drawGrille (g, grilleArea.toFloat(), juce::Colour (0xff3d3427), false);
            // logo plate: the Toneical amp wears the app's own badge, the Valve Head a cream plate
            const auto plate = nameArea.toFloat();
            if (module->typeId() == "realamp")
                PedalArt::drawLogoBadge (g, plate, name, 6.0f);
            else
            {
                juce::ColourGradient pg (juce::Colour (0xfff1e6c8), plate.getX(), plate.getY(), juce::Colour (0xffa58e5c), plate.getX(), plate.getBottom(), false);
                g.setGradientFill (pg);
                g.fillRoundedRectangle (plate, 6.0f);
                g.setColour (juce::Colour (0xff6e5c39));
                g.drawRoundedRectangle (plate.reduced (0.75f), 6.0f, 1.5f);
                g.setFont (juce::Font (juce::FontOptions (PedalArt::logoTypeface()).withHeight (plate.getHeight() * 0.95f)));
                g.setColour (juce::Colours::white.withAlpha (0.35f));
                g.drawFittedText (name, plate.toNearestInt().translated (0, 1), juce::Justification::centred, 1, 0.7f);
                g.setColour (juce::Colour (0xff2a2117));
                g.drawFittedText (name, plate.toNearestInt(), juce::Justification::centred, 1, 0.7f);
            }
            // control panel
            const auto p = panelArea.toFloat();
            juce::ColourGradient cp (style.panel.brighter (0.2f), p.getX(), p.getY(), style.panel.darker (0.25f), p.getX(), p.getBottom(), false);
            g.setGradientFill (cp);
            g.fillRoundedRectangle (p, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.4f));
            g.drawRoundedRectangle (p, 4.0f, 1.2f);
            // pilot jewel + power toggle with engraved ON / OFF
            PedalArt::drawJewel (g, led, active, glow, juce::Colour (0xffff6a10));
            PedalArt::drawBatToggle (g, footswitch, active, "ON", "OFF", contrastOn (style.panel));
            g.setColour (contrastOn (style.panel).withAlpha (0.7f));
            g.setFont (nameFont (9.5f));
            g.drawText ("POWER", footswitch.withY (footswitch.getBottom() + 16).withHeight (12).expanded (20, 0), juce::Justification::centred);
            break;
        }
        case Shape::cab:
        {
            const auto p = panelArea.toFloat();
            g.setColour (style.panel); g.fillRoundedRectangle (p, 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.5f)); g.drawRoundedRectangle (p, 4.0f, 1.2f);
            PedalArt::drawGrille (g, grilleArea.toFloat(), juce::Colour (0xff35302a), true);
            g.setColour (juce::Colour (0xffe8dcc0));
            g.setFont (nameFont (16.0f, true));
            g.drawFittedText (name, nameArea, juce::Justification::centredLeft, 1, 0.7f);
            PedalArt::drawRocker (g, footswitch, active, glow);
            g.setColour (juce::Colour (0xffe8dcc0).withAlpha (0.7f));
            g.setFont (nameFont (9.0f));
            g.drawText ("ON / OFF", footswitch.withY (footswitch.getBottom() + 4).withHeight (12).expanded (20, 0), juce::Justification::centred);
            break;
        }
        case Shape::digital:
        {
            g.setColour (style.ink);
            g.setFont (nameFont (18.0f));
            g.drawFittedText (name.toUpperCase(), nameArea, juce::Justification::centred, 1, 0.7f);
            const auto s = screenArea.toFloat();
            g.setColour (juce::Colours::black); g.fillRoundedRectangle (s.expanded (3), 6.0f);
            juce::ColourGradient lcd (juce::Colour (0xff10261c), s.getX(), s.getY(), juce::Colour (0xff07120d), s.getX(), s.getBottom(), false);
            g.setGradientFill (lcd); g.fillRoundedRectangle (s, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRoundedRectangle (s.withHeight (s.getHeight() * 0.4f), 4.0f);
            if (! module->hasFileSlot())
            {
                g.setColour (juce::Colour (0xff59e3a2));
                g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::bold)));
                g.drawFittedText (module->category().toUpperCase(), screenArea, juce::Justification::centred, 1);
            }
            PedalArt::drawSoftSwitch (g, footswitch.translated (0, press * 1.5f));
            PedalArt::drawLed (g, led, active, glow);
            break;
        }
        default:
        {
            if (style.knob == KnobStyle::fader && ! panelArea.isEmpty())
            {
                g.setColour (style.panel); g.fillRoundedRectangle (panelArea.toFloat(), 6.0f);
            }
            const bool vintage = style.shape == Shape::round || style.shape == Shape::wedge;
            if (style.nameBand)
            {
                const auto band = nameArea.toFloat().withSizeKeepingCentre ((float) nameArea.getWidth(), juce::jmin ((float) nameArea.getHeight(), 64.0f));
                g.setColour (juce::Colours::black.withAlpha (0.3f));
                g.fillRoundedRectangle (band.translated (0, 2), 5.0f);
                g.setColour (style.panel);
                g.fillRoundedRectangle (band, 5.0f);
                g.setColour (contrastOn (style.panel));
                g.setFont (nameFont (22.0f));
                g.drawFittedText (name, band.toNearestInt().reduced (8, 0).withTrimmedBottom (12), juce::Justification::centred, 1, 0.6f);
                g.setColour (contrastOn (style.panel).withAlpha (0.6f));
                g.setFont (nameFont (9.5f));
                g.drawText (module->category().toUpperCase(), band.withTrimmedTop (band.getHeight() - 18), juce::Justification::centred);
            }
            else
            {
                g.setColour (style.ink);
                g.setFont (nameFont (vintage ? 25.0f : 22.0f, vintage));
                g.drawFittedText (name, nameArea.withTrimmedBottom (14), juce::Justification::centred, 2, 0.6f);
                g.setColour (style.ink.withAlpha (0.55f));
                g.setFont (nameFont (9.5f));
                if (style.shape != Shape::round)
                    g.drawText (module->category().toUpperCase(), nameArea.withTrimmedTop (nameArea.getHeight() - 16), juce::Justification::centred);
            }
            PedalArt::drawLed (g, led, active, glow);
            PedalArt::drawStompSwitch (g, footswitch, press);
            break;
        }
    }

    if (! active)
    {
        g.setColour (juce::Colours::black.withAlpha (0.3f));
        g.fillPath (PedalArt::bodyPath (body, style.shape));
    }
}

} // namespace pg
