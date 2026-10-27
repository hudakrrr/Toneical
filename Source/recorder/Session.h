// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_graphics/juce_graphics.h>
#include <array>
#include "Chain.h"
#include "dsp/WorkerPool.h"

namespace pg::rec
{
//==============================================================================
// A recorded take or an imported file, at the session's sample rate.
// Immutable once it is shared with the audio thread.
struct AudioData
{
    juce::String id;                    // file name inside the project's audio/ folder
    juce::AudioBuffer<float> buffer;    // 1 channel (guitar DI) or 2 (backing tracks)
    std::vector<float> peaks;           // max |x| of every kPeakBlock samples, for drawing
    static constexpr int kPeakBlock = 128;

    juce::int64 numSamples() const noexcept { return buffer.getNumSamples(); }
    void computePeaks();
    static juce::String makeId (const juce::String& baseName);   // unique, file-name safe
};
using AudioPtr = std::shared_ptr<const AudioData>;

// A window onto an AudioData, placed on the timeline. Everything in samples.
struct Clip
{
    AudioPtr audio;
    juce::int64 start = 0;      // timeline position
    juce::int64 offset = 0;     // first sample used from 'audio'
    juce::int64 length = 0;
    juce::String name;
    juce::int64 end() const noexcept { return start + length; }
};

//==============================================================================
// Mixer strip + tone of one track. The same object is shared by every version of the
// arrangement (undo history), so mixer moves are not part of undo. The audio thread
// reads the atomics; the rest belongs to the message thread.
struct Track
{
    juce::String name;
    juce::Colour colour;
    juce::String toneName { "Dry" };     // preset the tone came from
    std::atomic<float> volume { 1.0f }, pan { 0.0f };
    std::atomic<bool> mute { false }, solo { false }, armed { false };
    std::atomic<bool> useTone { false }; // false: play the audio as it is (backing tracks)
    std::atomic<float> meter { 0.0f };   // peak since the GUI last looked (exchange (0))

    // Recordings are stored clean (DI) and re-amped through this on playback,
    // so a track's sound can be changed after it was recorded.
    Chain tone;

    double preparedRate = 0;             // message thread
    int preparedBlock = 0;
    float lastGainL = 1, lastGainR = 1;  // audio thread
    std::vector<float> outL, outR;       // audio thread: this track's share of the block
};
using TrackPtr = std::shared_ptr<Track>;

// One row of the arrangement. Clips are sorted by start and never overlap.
struct Lane
{
    TrackPtr track;
    std::vector<Clip> clips;

    void clearRange (juce::int64 from, juce::int64 to);   // cuts away everything in [from, to)
    void read (juce::int64 pos, float* L, float* R, int n) const noexcept;   // the clips' audio, unprocessed
    void insert (Clip c);                                   // overwrites what was there
    bool splitAt (juce::int64 t);
    int clipAt (juce::int64 t) const;                       // index, -1 if none
};
using Arrangement = std::vector<Lane>;

//==============================================================================
// The multitrack recorder: arrangement, transport, metronome, recording and project files.
//
// Edits happen on the message thread and are published to the audio thread as immutable
// versions through an atomic pointer (the same scheme as pg::Chain); every published
// version is also an undo step.
class Session final : public juce::ChangeBroadcaster
{
public:
    Session();
    ~Session() override;

    // ---- arrangement (message thread) ----------------------------------------------
    Arrangement getArrangement() const;
    void commit (Arrangement next);              // publish as a new undo step
    void touch();                                // non-undoable change (mixer, tone, name)
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

    TrackPtr makeTrack (const juce::String& name = {});         // not yet in the arrangement
    TrackPtr addTrack (const juce::String& name = {});           // appended, undoable
    void removeTrack (const TrackPtr&);
    juce::int64 getLength() const;               // end of the last clip

    // Decodes in the background, then adds the clip at 'start' on lane 'laneIndex'
    // (a new track when the index is past the last lane).
    void importFile (const juce::File&, int laneIndex, juce::int64 start);
    static bool isAudioFile (const juce::File&);    // decodable without ffmpeg
    static juce::String audioWildcard();
    // imports waiting or running (message thread); the first one is running
    const juce::StringArray& getImportNames() const noexcept { return importNames; }
    juce::String getImportName() const;
    float getImportProgress() const noexcept { return importProgress.load(); }   // of the running one, 0..1

    // ---- project (message thread) ----------------------------------------------------
    // A project is a folder: <name>/<name>.toneproj plus an audio/ folder with the takes.
    static constexpr const char* projectExtension = ".toneproj";
    static juce::File projectsFolder();          // ~/Toneical/Projects
    juce::File getProjectFile() const { return projectFile; }
    juce::String getProjectName() const;
    bool isDirty() const noexcept { return dirty; }
    void newProject();
    bool saveProject (const juce::File&, juce::String& error);
    bool loadProject (const juce::File&, juce::String& error);

    std::function<void (const juce::String&)> onMessage;   // status-bar messages

    // ---- export (message thread) ------------------------------------------------------
    // Renders the mix (what you hear: mute / solo / volume / pan / track tones) in the
    // background. MP3 needs the 'lame' or 'ffmpeg' program; the result is reported via onMessage.
    struct ExportOptions
    {
        enum class Format { wav24, wav32, mp3 } format = Format::wav24;
        int mp3Kbps = 320;
        bool loopRangeOnly = false;
        bool normalize = true;          // peak at -1 dBFS
    };
    bool exportMix (const juce::File& target, const ExportOptions&, juce::String& error);
    bool isExporting() const noexcept { return exporting.load(); }
    float getExportProgress() const noexcept { return exportProgress.load(); }
    void cancelExport() { exportCancel.store (true); }
    static juce::File findMp3Encoder();             // lame or ffmpeg on the PATH (invalid if none)
    static juce::File findProgram (const juce::String& name);   // on the PATH or next to Toneical

    // ---- transport (message thread) ----------------------------------------------------
    void play();
    void stop();                                 // also ends a recording
    void togglePlay() { if (isPlaying() || isRecording()) stop(); else play(); }
    // Records the guitar (clean) onto the armed track - a new track if none is armed. The
    // track's tone becomes a copy of the rig you hear while recording.
    void startRecording (const juce::ValueTree& rigChain, const juce::String& rigName);
    void stopRecording();                        // punch out, keeps playing
    bool isPlaying() const noexcept   { return playing.load(); }
    bool isRecording() const noexcept { return recState.load() != idle; }
    bool isCountingIn() const noexcept { return recState.load() == countingIn; }

    juce::int64 getPosition() const noexcept { return position.load(); }
    void setPosition (juce::int64 samples);

    void setLoop (bool on) { loopOn.store (on); }
    bool isLooping() const noexcept { return loopOn.load(); }
    void setLoopRange (juce::int64 from, juce::int64 to);
    juce::int64 getLoopStart() const noexcept { return loopStart.load(); }
    juce::int64 getLoopEnd() const noexcept   { return loopEnd.load(); }

    // ---- metronome / grid ------------------------------------------------------------------
    void setMetronome (bool on);
    bool isMetronomeOn() const noexcept { return metronomeOn.load(); }
    void setCountIn (bool on) { countIn.store (on); }
    bool isCountInOn() const noexcept { return countIn.load(); }
    void setBpm (double bpm);
    double getBpm() const noexcept { return bpm.load(); }
    void setTimeSignature (int beatsPerBar, int beatUnit);
    int getBeatsPerBar() const noexcept { return sigNum.load(); }
    int getBeatUnit() const noexcept { return sigDen.load(); }
    void setClickLevel (float gain) { clickLevel.store (gain); }
    float getClickLevel() const noexcept { return clickLevel.load(); }
    double samplesPerBeat() const noexcept;
    double samplesPerBar() const noexcept { return samplesPerBeat() * getBeatsPerBar(); }
    double getSampleRate() const noexcept { return sampleRate.load(); }

    // ---- recording (message thread) ---------------------------------------------------------
    TrackPtr getRecordTrack() const { return recTrack; }
    juce::int64 getTakeStart() const noexcept { return takeStart.load(); }
    juce::int64 getTakeLength() const noexcept { return (juce::int64) take.size(); }
    const std::vector<float>& getTakePeaks() const noexcept { return takePeaks; }
    void setRecordLatency (int samples) { recordLatency.store (juce::jmax (0, samples)); }
    void pump();                                 // ~30x/s: collects recorded audio, frees old versions

    // ---- audio thread ------------------------------------------------------------------------
    void prepare (double sampleRate, int maxBlockSize);
    void release();
    // The live rig: processes L/R in place for [offset, offset + n) of the block.
    using RigFn = void (*) (void* context, int offset, int n);
    // di: the clean guitar (mono). 'rig' runs the live pedalboard on L/R - in parallel with the
    // tracks' tones - then track playback is added to L/R and the metronome written to click.
    void process (const float* di, float* L, float* R, float* click, int numSamples, RigFn rig, void* rigContext) noexcept;
    int getNumDspThreads() const noexcept { return workers.getNumHelpers() + 1; }

private:
    enum RecState { idle, countingIn, recording };
    struct Version { Arrangement lanes; };

    void publish (Arrangement next);
    void prepareTrack (Track&);
    void finishTake();
    void addImported (std::shared_ptr<AudioData>, const juce::String& error, const juce::String& name,
                      int laneIndex, juce::int64 start);
    void message (const juce::String& s) { if (onMessage) onMessage (s); }
    void changed();   // listeners see an edit right away (the GUI's copy of the arrangement must not go stale)
    void processBlock (const float* di, float* L, float* R, float* click, int n, RigFn, void*, int offset) noexcept;
    void renderLane (const Lane&, int n) noexcept;
    static void runJob (void* session, int index);
    void renderClick (double beatPos, double beatStep, float* out, int n, float level) noexcept;

    // published arrangement
    mutable std::mutex editMutex;
    std::unique_ptr<Version> live;
    std::atomic<Version*> current { nullptr };
    std::vector<std::pair<std::unique_ptr<Version>, juce::uint64>> retired;
    std::atomic<juce::uint64> blocksDone { 0 };
    std::atomic<bool> audioRunning { false };
    std::vector<Arrangement> undoStack, redoStack;
    bool dirty = false;
    juce::File projectFile;

    // transport
    std::atomic<bool> playing { false }, loopOn { false }, metronomeOn { false }, countIn { false };
    std::atomic<int> recState { idle };
    std::atomic<juce::int64> position { 0 }, seekRequest { -1 }, loopStart { 0 }, loopEnd { 0 };
    std::atomic<double> bpm { 120.0 }, sampleRate { 48000.0 };
    std::atomic<int> sigNum { 4 }, sigDen { 4 };
    std::atomic<float> clickLevel { 0.5f };
    int maxBlock = 512;

    // parallel block rendering (audio thread)
    struct Segment { juce::int64 pos; int offset, length; };
    std::array<Segment, 8> segments {};
    int numSegments = 0;
    std::vector<const Lane*> activeLanes;
    RigFn rigFn = nullptr;
    void* rigContext = nullptr;
    int blockOffset = 0, blockLength = 0;
    WorkerPool workers;

    // metronome synth (audio thread)
    double freeBeatPos = 0;                      // metronome running while the transport is stopped
    double countInPos = 0;
    double clickPhase = 0, clickInc = 0;
    int clickAge = 1 << 30;
    std::atomic<bool> resetFreeClick { false };

    // recording: the audio thread writes the DI into a FIFO, pump() appends it to 'take'
    juce::AbstractFifo fifo { 1 };
    std::vector<float> fifoData;
    std::atomic<bool> takeStarted { false }, fifoOverflow { false };
    std::atomic<juce::int64> takeStart { 0 };
    std::atomic<int> recordLatency { 0 };
    std::vector<float> take, takePeaks;
    float partialPeak = 0; int partialCount = 0;
    TrackPtr recTrack;
    int takeCounter = 0;

    std::atomic<bool> exporting { false }, exportCancel { false };
    std::atomic<float> exportProgress { 0.0f }, importProgress { 0.0f };
    juce::StringArray importNames;

    juce::ThreadPool importPool { juce::ThreadPoolOptions{}.withNumberOfThreads (1) };

    JUCE_DECLARE_WEAK_REFERENCEABLE (Session)
    JUCE_DECLARE_NON_COPYABLE (Session)
};

} // namespace pg::rec
