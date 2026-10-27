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
// The Record mode: a multitrack editor in the spirit of Audacity / a DAW.
//
//   - track headers on the left: name, Record-arm / Mute / Solo, volume, pan and the
//     track's own tone (any preset, or a copy of the current rig)
//   - timeline with bars & beats; click to move the playhead, drag its red triangle to
//     scrub, drag elsewhere in the ruler to set the loop range
//   - drag on a track to select a time range (across several tracks), drag a clip by its
//     title bar to move it, drag its edges to trim it, Split / S cuts at the playhead
//   - drop audio files (backing tracks) anywhere on the timeline
//   - Ctrl + wheel zooms, Alt + wheel changes the track height
class TrackView final : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        private juce::ChangeListener,
                        private juce::ScrollBar::Listener,
                        private juce::Timer
{
public:
    explicit TrackView (ToneicalProcessor&);
    ~TrackView() override;

    std::function<void()> onEditToneInRig;                    // switch the editor to the rig view
    std::function<void (const juce::String&)> onStatus;

    // A track's tone is being edited on the rig: copy the rig back to it (on returning).
    void finishToneEdit();
    juce::String toneEditTrackName() const { return toneEdit != nullptr ? toneEdit->name : juce::String(); }

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragMove (const juce::StringArray&, int x, int y) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int x, int y) override;

private:
    class Ruler;
    class Lanes;
    class Header;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double) override;
    void timerCallback() override;

    // view
    double timeToX (juce::int64 t) const { return ((double) t - viewStart) / spp; }
    juce::int64 xToTime (double x) const { return (juce::int64) std::max (0.0, viewStart + x * spp); }
    juce::int64 snapTime (juce::int64 t) const;
    int laneAt (int y) const;                       // y in Lanes coordinates, -1 if below the last track
    int laneY (int index) const { return index * trackH - scrollY; }
    void setZoom (double newSpp, double anchorX);
    void zoomToFit();
    void setTrackHeight (int h);
    void updateScrollBars();
    void syncHeaders();
    void wheel (const juce::MouseWheelDetails&, const juce::MouseEvent&, bool overTimeline);

    // editing
    std::vector<int> targetLanes() const;           // selected tracks, else the focused track
    void splitAtSelection();
    void deleteSelection();
    void selectAll();
    void showLaneMenu (int lane, juce::int64 t);
    void showTrackMenu (const rec::TrackPtr&);
    void showToneMenu (const rec::TrackPtr&, juce::Component* target);
    void applyTone (const rec::TrackPtr&, int menuId);
    void editToneInRig (const rec::TrackPtr&);
    void importDialog();
    void newProject();
    void openProject();
    void saveProject (bool saveAs);
    void exportDialog();
    void chooseExportFile (const rec::Session::ExportOptions&);
    void status (const juce::String& s) { if (onStatus) onStatus (s); }

    ToneicalProcessor& proc;
    rec::Session& session;
    rec::Arrangement arr;                           // cached copy of the session's arrangement

    double spp = 600.0;                             // samples per pixel
    double viewStart = 0.0;                         // sample at the left edge
    int scrollY = 0, trackH = 96;
    bool snap = true;

    // selection: a time range over some tracks, or a single clip
    juce::int64 selStart = 0, selEnd = 0;
    std::vector<rec::TrackPtr> selTracks;
    rec::TrackPtr focusTrack, toneEdit;
    rec::TrackPtr selClipTrack;
    juce::int64 selClipStart = -1;

    // file drag & drop indicator
    int dropLane = -2;
    juce::int64 dropTime = 0;
    int dragFileCount = 0;

    // imports in flight: drawn as "Loading..." boxes where the clips will land
    struct PendingImport { int lane; juce::int64 time; juce::String name; };
    std::vector<PendingImport> pending;
    void addPending (const juce::File&, int lane, juce::int64 time);
    void prunePending();

    // toolbar
    juce::TextButton splitButton { "Split" }, snapButton { "Snap" },
                     zoomOut { "-" }, zoomIn { "+" }, fitButton { "Fit" },
                     undoButton { "Undo" }, redoButton { "Redo" }, addTrackButton { "+ Track" }, importButton { "Import..." },
                     exportButton { "Export..." },
                     newButton { "New" }, openButton { "Open" }, saveButton { "Save" }, saveAsButton { "Save as" },
                     addTrackRowButton { "+ Add track" };   // under the last track header
    juce::Slider zoomSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox },
                 heightSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> toolbar, titleArea, cornerArea;

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Lanes> lanes;
    juce::Component headerHolder;
    std::vector<std::unique_ptr<Header>> headers;
    juce::ScrollBar hbar { false }, vbar { true };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::Array<juce::File> userPresetFiles;        // as listed in the last tone menu
    std::unique_ptr<juce::AlertWindow> exportWindow;
    juce::File lastExportFolder;
    bool wasExporting = false;
    juce::int64 lastPlayhead = -1;
    int tick = 0;

    static constexpr int headerW = 244, rulerH = 26, toolbarH = 36, minTrackH = 60, maxTrackH = 260;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TrackView)
};

} // namespace pg
