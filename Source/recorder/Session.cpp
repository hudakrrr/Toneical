// Toneical - guitar rig for Linux
// Copyright (C) 2026 hudakrrr and contributors
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Session.h"
#include "Presets.h"
#include <thread>

namespace pg::rec
{
namespace
{
    const juce::uint32 trackColours[] = { 0xfff2a33a, 0xff3fb6c8, 0xff9b7be0, 0xffe0605a,
                                          0xff6cc36a, 0xff5a8fe0, 0xffe07fb8, 0xffc8c04a };

    using Progress = std::function<void (float)>;

    // Sample-rate conversion for imported files: windowed sinc (32 taps, Kaiser window),
    // zero latency, both channels at once. About 6x less work than juce::WindowedSincInterpolator
    // (200 taps), and the clip lands exactly where it was dropped.
    juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double ratio /* in rate / out rate */,
                                       const Progress& progress)
    {
        constexpr int half = 16, taps = 2 * half, phases = 1024;
        const double pi = juce::MathConstants<double>::pi;
        const double cutoff = 0.95 * std::min (1.0, 1.0 / ratio);   // of the input's Nyquist: no aliasing when going down
        auto bessel0 = [] (double x)
        {
            double sum = 1.0, term = 1.0;
            for (int k = 1; k < 32; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); sum += term; }
            return sum;
        };
        const double beta = 8.0, norm = bessel0 (beta);
        std::vector<float> table ((size_t) (phases + 1) * taps);
        for (int p = 0; p <= phases; ++p)
        {
            float* h = table.data() + (size_t) p * taps;
            double sum = 0.0;
            for (int k = 0; k < taps; ++k)
            {
                const double x = (double) (k - half + 1) - (double) p / phases;   // tap distance from the wanted point
                const double sinc = std::abs (x) < 1e-9 ? 1.0 : std::sin (pi * cutoff * x) / (pi * cutoff * x);
                const double r = x / half;
                h[k] = (float) (std::abs (r) >= 1.0 ? 0.0 : sinc * bessel0 (beta * std::sqrt (1.0 - r * r)) / norm);
                sum += h[k];
            }
            for (int k = 0; k < taps; ++k) h[k] = (float) (h[k] / sum);
        }

        const int inLen = in.getNumSamples();
        const int outLen = (int) std::floor ((double) (inLen - 1) / ratio) + 1;
        juce::AudioBuffer<float> out (in.getNumChannels(), outLen);
        auto convert = [&] (int ch)
        {
            std::vector<float> x ((size_t) inLen + 2 * taps, 0.0f);   // zero padding on both sides
            std::copy_n (in.getReadPointer (ch), inLen, x.data() + taps);
            float* y = out.getWritePointer (ch);
            for (int j = 0; j < outLen; ++j)
            {
                const double t = (double) j * ratio;
                const auto i0 = (juce::int64) t;
                const float* h = table.data() + (size_t) (int) ((t - (double) i0) * phases + 0.5) * taps;
                const float* src = x.data() + taps + i0 - half + 1;
                float acc = 0.0f;
                for (int k = 0; k < taps; ++k) acc += src[k] * h[k];
                y[j] = acc;
                if (ch == 0 && (j & 0x7fff) == 0) progress ((float) j / (float) outLen);
            }
        };
        std::thread second;
        if (in.getNumChannels() > 1) second = std::thread (convert, 1);
        convert (0);
        if (second.joinable()) second.join();
        return out;
    }

    // Reads (and converts to the session rate) any file JUCE can decode. Anything else
    // (m4a, aac, opus, wma, video files...) goes through ffmpeg when it is installed.
    std::shared_ptr<AudioData> readAudio (juce::AudioFormatManager& fm, const juce::File& file, double targetRate,
                                          juce::String& error, const Progress& progress = [] (float) {})
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        juce::TemporaryFile converted (".wav");
        if (reader == nullptr && file.existsAsFile())
        {
            if (const auto ffmpeg = Session::findProgram ("ffmpeg"); ffmpeg != juce::File())
            {
                juce::ChildProcess p;
                if (p.start (juce::StringArray { ffmpeg.getFullPathName(), "-y", "-loglevel", "error", "-i", file.getFullPathName(),
                                                 "-vn", "-c:a", "pcm_f32le", converted.getFile().getFullPathName() })
                    && p.waitForProcessToFinish (10 * 60 * 1000) && p.getExitCode() == 0)
                    reader.reset (fm.createReaderFor (converted.getFile()));
            }
            if (reader == nullptr)
            {
                error = "Can't read " + file.getFileName() + " - supported: wav, flac, ogg, mp3, aiff"
                        + (Session::findProgram ("ffmpeg") == juce::File() ? juce::String (" (install ffmpeg for m4a, aac, opus, wma...)")
                                                                           : juce::String (" (ffmpeg couldn't convert it either)"));
                return nullptr;
            }
        }
        if (reader == nullptr) { error = "Can't open " + file.getFullPathName(); return nullptr; }
        if (reader->lengthInSamples <= 0) { error = file.getFileName() + " is empty"; return nullptr; }
        if ((double) reader->lengthInSamples > reader->sampleRate * 60.0 * 30.0)
        {
            error = file.getFileName() + " is longer than 30 minutes";
            return nullptr;
        }
        const int ch = juce::jmin (2, (int) reader->numChannels);
        const int len = (int) reader->lengthInSamples;
        const bool convert = std::abs (reader->sampleRate - targetRate) > 0.5;
        const float decodeShare = convert ? 0.6f : 0.95f;   // rough share of the work, for the progress bar

        juce::AudioBuffer<float> raw (ch, len);
        constexpr int chunk = 1 << 16;
        for (int pos = 0; pos < len; pos += chunk)
        {
            reader->read (&raw, pos, std::min (chunk, len - pos), pos, true, ch > 1);
            progress (decodeShare * (float) pos / (float) len);
        }

        auto data = std::make_shared<AudioData>();
        if (convert)
            data->buffer = resample (raw, reader->sampleRate / targetRate,
                                     [&] (float f) { progress (decodeShare + (0.95f - decodeShare) * f); });
        else
            data->buffer = std::move (raw);
        data->computePeaks();
        progress (1.0f);
        return data;
    }

    bool writeWav (const AudioData& d, const juce::File& f, double rate)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream());
        if (os == nullptr) return false;
        juce::WavAudioFormat wav;
        const auto opts = juce::AudioFormatWriterOptions{}
                              .withSampleRate (rate)
                              .withNumChannels (d.buffer.getNumChannels())
                              .withBitsPerSample (32)
                              .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = wav.createWriterFor (os, opts);
        return writer != nullptr && writer->writeFromAudioSampleBuffer (d.buffer, 0, d.buffer.getNumSamples());
    }
}

//==============================================================================
void AudioData::computePeaks()
{
    const int n = buffer.getNumSamples();
    peaks.assign ((size_t) (n + kPeakBlock - 1) / kPeakBlock, 0.0f);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* x = buffer.getReadPointer (ch);
        for (int i = 0; i < n; ++i)
        {
            auto& p = peaks[(size_t) (i / kPeakBlock)];
            p = std::max (p, std::abs (x[i]));
        }
    }
}

juce::String AudioData::makeId (const juce::String& baseName)
{
    auto base = juce::File::createLegalFileName (baseName).replaceCharacter (' ', '_');
    if (base.isEmpty()) base = "audio";
    return base.substring (0, 40) + "-" + juce::Uuid().toString().substring (0, 8) + ".wav";
}

//==============================================================================
void Lane::clearRange (juce::int64 from, juce::int64 to)
{
    if (to <= from) return;
    std::vector<Clip> out;
    for (auto& c : clips)
    {
        if (c.end() <= from || c.start >= to) { out.push_back (c); continue; }
        if (c.start < from)
        {
            auto left = c;
            left.length = from - c.start;
            out.push_back (left);
        }
        if (c.end() > to)
        {
            auto right = c;
            right.offset += to - c.start;
            right.start = to;
            right.length = c.end() - to;
            out.push_back (right);
        }
    }
    clips = std::move (out);
}

void Lane::insert (Clip c)
{
    if (c.length <= 0 || c.audio == nullptr) return;
    clearRange (c.start, c.end());
    clips.push_back (std::move (c));
    std::sort (clips.begin(), clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
}

bool Lane::splitAt (juce::int64 t)
{
    const int i = clipAt (t);
    if (i < 0 || clips[(size_t) i].start == t) return false;
    auto right = clips[(size_t) i];
    auto& left = clips[(size_t) i];
    right.offset += t - left.start;
    right.length = left.end() - t;
    right.start = t;
    left.length = t - left.start;
    clips.insert (clips.begin() + i + 1, right);
    return true;
}

void Lane::read (juce::int64 pos, float* L, float* R, int n) const noexcept
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    for (auto& c : clips)
    {
        if (c.end() <= pos || c.start >= pos + n) continue;
        const auto s0 = std::max (pos, c.start), s1 = std::min (pos + n, c.end());
        const int dst = (int) (s0 - pos), cnt = (int) (s1 - s0);
        const int src = (int) (c.offset + (s0 - c.start));
        const auto& buf = c.audio->buffer;
        std::copy_n (buf.getReadPointer (0, src), cnt, L + dst);
        std::copy_n (buf.getReadPointer (buf.getNumChannels() > 1 ? 1 : 0, src), cnt, R + dst);
    }
}

int Lane::clipAt (juce::int64 t) const
{
    for (size_t i = 0; i < clips.size(); ++i)
        if (t >= clips[i].start && t < clips[i].end()) return (int) i;
    return -1;
}

//==============================================================================
Session::Session()
{
    activeLanes.reserve (512);
    live = std::make_unique<Version>();
    current.store (live.get());
    newProject();
}

Session::~Session()
{
    exportCancel.store (true);
    importPool.removeAllJobs (true, 10000);
    masterReference.clear();
    current.store (nullptr);
}

void Session::changed()
{
    if (juce::MessageManager::existsAndIsCurrentThread()) sendSynchronousChangeMessage();
    else sendChangeMessage();
}

Arrangement Session::getArrangement() const
{
    const std::lock_guard<std::mutex> lock (editMutex);
    return live->lanes;
}

void Session::prepareTrack (Track& t)
{
    t.tone.prepare (sampleRate.load(), maxBlock);
    t.outL.assign ((size_t) maxBlock, 0.0f);
    t.outR.assign ((size_t) maxBlock, 0.0f);
    t.preparedRate = sampleRate.load();
    t.preparedBlock = maxBlock;
}

void Session::publish (Arrangement next)
{
    const std::lock_guard<std::mutex> lock (editMutex);
    for (auto& lane : next)
        if (std::abs (lane.track->preparedRate - sampleRate.load()) > 0.5 || lane.track->preparedBlock != maxBlock)
            prepareTrack (*lane.track);   // not in the published version yet: safe to prepare

    auto v = std::make_unique<Version>();
    v->lanes = std::move (next);
    current.store (v.get(), std::memory_order_release);
    retired.emplace_back (std::move (live), blocksDone.load());
    live = std::move (v);
}

void Session::commit (Arrangement next)
{
    undoStack.push_back (getArrangement());
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
    publish (std::move (next));
    dirty = true;
    changed();
}

void Session::touch()
{
    dirty = true;
    sendChangeMessage();
}

void Session::undo()
{
    if (undoStack.empty() || isRecording()) return;
    redoStack.push_back (getArrangement());
    publish (std::move (undoStack.back()));
    undoStack.pop_back();
    dirty = true;
    changed();
}

void Session::redo()
{
    if (redoStack.empty() || isRecording()) return;
    undoStack.push_back (getArrangement());
    publish (std::move (redoStack.back()));
    redoStack.pop_back();
    dirty = true;
    changed();
}

TrackPtr Session::makeTrack (const juce::String& name)
{
    const auto lanes = getArrangement();
    auto t = std::make_shared<Track>();
    t->name = name.isNotEmpty() ? name : "Track " + juce::String ((int) lanes.size() + 1);
    t->colour = juce::Colour (trackColours[lanes.size() % std::size (trackColours)]);
    prepareTrack (*t);
    return t;
}

TrackPtr Session::addTrack (const juce::String& name)
{
    auto t = makeTrack (name);
    auto arr = getArrangement();
    arr.push_back ({ t, {} });
    commit (std::move (arr));
    return t;
}

void Session::removeTrack (const TrackPtr& t)
{
    if (t == recTrack && isRecording()) stop();
    auto arr = getArrangement();
    arr.erase (std::remove_if (arr.begin(), arr.end(), [&] (const Lane& l) { return l.track == t; }), arr.end());
    commit (std::move (arr));
}

juce::int64 Session::getLength() const
{
    juce::int64 len = 0;
    for (auto& lane : getArrangement())
        for (auto& c : lane.clips) len = std::max (len, c.end());
    return len;
}

//==============================================================================
bool Session::isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

juce::String Session::audioWildcard()
{
    return findProgram ("ffmpeg") != juce::File() ? "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3;*.m4a;*.aac;*.opus;*.wma;*.mp4;*.webm;*.mkv"
                                                  : "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
}

juce::String Session::getImportName() const
{
    return importNames.isEmpty() ? juce::String() : importNames[0];
}

void Session::importFile (const juce::File& file, int laneIndex, juce::int64 start)
{
    const auto name = file.getFileNameWithoutExtension();
    importNames.add (name);
    sendChangeMessage();
    const double rate = sampleRate.load();
    juce::WeakReference<Session> weak (this);
    importPool.addJob ([this, file, name, laneIndex, start, rate, weak]
    {
        importProgress.store (0.0f);
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        juce::String error;
        auto data = readAudio (fm, file, rate, error, [this] (float f) { importProgress.store (f); });
        if (data != nullptr) data->id = AudioData::makeId (name);
        juce::MessageManager::callAsync ([weak, data, error, name, laneIndex, start]
        {
            if (auto* s = weak.get()) s->addImported (data, error, name, laneIndex, start);
        });
    });
}

void Session::addImported (std::shared_ptr<AudioData> data, const juce::String& error, const juce::String& name,
                           int laneIndex, juce::int64 start)
{
    importNames.remove (importNames.indexOf (name));
    if (data == nullptr) { message (error); sendChangeMessage(); return; }
    auto arr = getArrangement();
    if (laneIndex < 0 || laneIndex >= (int) arr.size())
    {
        // an empty track that was never touched takes the file instead of a new track
        if (arr.size() == 1 && arr[0].clips.empty() && ! arr[0].track->useTone.load() && undoStack.empty())
        {
            arr[0].track->name = name;
            laneIndex = 0;
        }
        else
        {
            arr.push_back ({ makeTrack (name), {} });
            laneIndex = (int) arr.size() - 1;
        }
    }
    Clip c;
    c.audio = data;
    c.start = std::max<juce::int64> (0, start);
    c.length = data->numSamples();
    c.name = name;
    arr[(size_t) laneIndex].insert (c);
    commit (std::move (arr));
    message ("Added " + name + " (" + juce::String ((double) c.length / sampleRate.load(), 1) + " s) to "
             + getArrangement()[(size_t) laneIndex].track->name);
}

//==============================================================================
juce::File Session::projectsFolder()
{
    auto dir = Presets::homeFolder().getChildFile ("Projects");
    dir.createDirectory();
    return dir;
}

juce::String Session::getProjectName() const
{
    return projectFile == juce::File() ? juce::String ("Untitled") : projectFile.getFileNameWithoutExtension();
}

void Session::newProject()
{
    stop();
    Arrangement arr;
    auto first = makeTrack ("Track 1");
    first->colour = juce::Colour (trackColours[0]);
    arr.push_back ({ first, {} });
    publish (std::move (arr));
    undoStack.clear();
    redoStack.clear();
    projectFile = juce::File();
    position.store (0);
    loopOn.store (false);
    loopStart.store (0);
    loopEnd.store (0);
    takeCounter = 0;
    dirty = false;
    changed();
}

bool Session::saveProject (const juce::File& file, juce::String& error)
{
    if (isRecording()) stop();
    const auto audioDir = file.getParentDirectory().getChildFile ("audio");
    if (! audioDir.createDirectory())
    {
        error = "Can't create " + audioDir.getFullPathName();
        return false;
    }

    const double rate = sampleRate.load();
    juce::ValueTree root ("ToneicalProject");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("sampleRate", rate, nullptr);
    root.setProperty ("bpm", bpm.load(), nullptr);
    root.setProperty ("beatsPerBar", sigNum.load(), nullptr);
    root.setProperty ("beatUnit", sigDen.load(), nullptr);
    root.setProperty ("loop", loopOn.load(), nullptr);
    root.setProperty ("loopStart", loopStart.load(), nullptr);
    root.setProperty ("loopEnd", loopEnd.load(), nullptr);
    root.setProperty ("metronome", metronomeOn.load(), nullptr);
    root.setProperty ("countIn", countIn.load(), nullptr);
    root.setProperty ("clickLevel", clickLevel.load(), nullptr);
    root.setProperty ("takeCounter", takeCounter, nullptr);

    for (auto& lane : getArrangement())
    {
        auto& t = *lane.track;
        juce::ValueTree tv ("Track");
        tv.setProperty ("name", t.name, nullptr);
        tv.setProperty ("colour", t.colour.toString(), nullptr);
        tv.setProperty ("volume", t.volume.load(), nullptr);
        tv.setProperty ("pan", t.pan.load(), nullptr);
        tv.setProperty ("mute", t.mute.load(), nullptr);
        tv.setProperty ("solo", t.solo.load(), nullptr);
        tv.setProperty ("useTone", t.useTone.load(), nullptr);
        tv.setProperty ("toneName", t.toneName, nullptr);
        tv.appendChild (t.tone.toValueTree(), nullptr);
        for (auto& c : lane.clips)
        {
            const auto wav = audioDir.getChildFile (c.audio->id);
            if (! wav.existsAsFile() && ! writeWav (*c.audio, wav, rate))
            {
                error = "Could not write " + wav.getFullPathName();
                return false;
            }
            juce::ValueTree cv ("Clip");
            cv.setProperty ("file", c.audio->id, nullptr);
            cv.setProperty ("start", c.start, nullptr);
            cv.setProperty ("offset", c.offset, nullptr);
            cv.setProperty ("length", c.length, nullptr);
            cv.setProperty ("name", c.name, nullptr);
            tv.appendChild (cv, nullptr);
        }
        root.appendChild (tv, nullptr);
    }

    auto xml = root.createXml();
    if (xml == nullptr || ! xml->writeTo (file))
    {
        error = "Could not write " + file.getFullPathName();
        return false;
    }
    projectFile = file;
    dirty = false;
    sendChangeMessage();
    return true;
}

bool Session::loadProject (const juce::File& file, juce::String& error)
{
    auto xml = juce::XmlDocument::parse (file);
    const auto root = xml != nullptr ? juce::ValueTree::fromXml (*xml) : juce::ValueTree();
    if (! root.hasType ("ToneicalProject")) { error = "Not a Toneical project"; return false; }

    stop();
    const double rate = sampleRate.load();
    // positions are stored in samples at the project's rate; the audio is resampled on load, so are they
    const double fileRate = (double) root.getProperty ("sampleRate", rate);
    const double scale = fileRate > 0.0 ? rate / fileRate : 1.0;
    auto at = [scale] (const juce::var& v) { return (juce::int64) std::llround ((double) (juce::int64) v * scale); };
    const auto audioDir = file.getParentDirectory().getChildFile ("audio");
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::map<juce::String, AudioPtr> cache;
    int missing = 0;

    Arrangement arr;
    for (auto tv : root)
    {
        if (! tv.hasType ("Track")) continue;
        auto t = makeTrack (tv.getProperty ("name", "Track").toString());
        t->colour = juce::Colour::fromString (tv.getProperty ("colour", t->colour.toString()).toString());
        t->volume.store ((float) tv.getProperty ("volume", 1.0f));
        t->pan.store ((float) tv.getProperty ("pan", 0.0f));
        t->mute.store ((bool) tv.getProperty ("mute", false));
        t->solo.store ((bool) tv.getProperty ("solo", false));
        t->useTone.store ((bool) tv.getProperty ("useTone", false));
        t->toneName = tv.getProperty ("toneName", "Dry").toString();
        if (auto chainTree = tv.getChildWithName ("Chain"); chainTree.isValid())
            t->tone.fromValueTree (chainTree);

        Lane lane { t, {} };
        for (auto cv : tv)
        {
            if (! cv.hasType ("Clip")) continue;
            const auto id = cv.getProperty ("file").toString();
            auto& audio = cache[id];
            if (audio == nullptr)
            {
                juce::String err;
                if (auto d = readAudio (fm, audioDir.getChildFile (id), rate, err))
                {
                    d->id = id;
                    audio = d;
                }
            }
            if (audio == nullptr) { ++missing; continue; }
            Clip c;
            c.audio = audio;
            c.start  = at (cv.getProperty ("start", 0));
            c.offset = juce::jlimit<juce::int64> (0, audio->numSamples() - 1, at (cv.getProperty ("offset", 0)));
            c.length = juce::jlimit<juce::int64> (0, audio->numSamples() - c.offset, at (cv.getProperty ("length", 0)));
            c.name   = cv.getProperty ("name").toString();
            if (c.length > 0) lane.clips.push_back (c);
        }
        std::sort (lane.clips.begin(), lane.clips.end(), [] (const Clip& a, const Clip& b) { return a.start < b.start; });
        arr.push_back (std::move (lane));
    }

    publish (std::move (arr));
    undoStack.clear();
    redoStack.clear();
    setBpm ((double) root.getProperty ("bpm", 120.0));
    setTimeSignature ((int) root.getProperty ("beatsPerBar", 4), (int) root.getProperty ("beatUnit", 4));
    loopOn.store ((bool) root.getProperty ("loop", false));
    setLoopRange (at (root.getProperty ("loopStart", 0)), at (root.getProperty ("loopEnd", 0)));
    metronomeOn.store ((bool) root.getProperty ("metronome", false));
    countIn.store ((bool) root.getProperty ("countIn", false));
    clickLevel.store ((float) root.getProperty ("clickLevel", 0.5f));
    takeCounter = (int) root.getProperty ("takeCounter", 0);
    position.store (0);
    projectFile = file;
    dirty = false;
    changed();
    if (missing > 0)
        message (juce::String (missing) + " clip(s) could not be loaded - audio files missing from " + audioDir.getFullPathName());
    return true;
}

//==============================================================================
juce::File Session::findProgram (const juce::String& name)
{
   #if JUCE_WINDOWS
    const char* separator = ";";
    const juce::String exe = name + ".exe";
   #else
    const char* separator = ":";
    const juce::String exe = name;
   #endif
    auto dirs = juce::StringArray::fromTokens (juce::SystemStats::getEnvironmentVariable ("PATH", "/usr/bin"), separator, "\"");
    // also next to Toneical itself, so e.g. lame.exe / ffmpeg.exe can simply be dropped there
    dirs.insert (0, juce::File::getSpecialLocation (juce::File::currentApplicationFile).getParentDirectory().getFullPathName());
    for (auto& dir : dirs)
        if (const auto d = dir.unquoted(); d.isNotEmpty() && juce::File::isAbsolutePath (d))
            if (auto f = juce::File (d).getChildFile (exe); f.existsAsFile())
                return f;
    return {};
}

juce::File Session::findMp3Encoder()
{
    if (auto lame = findProgram ("lame"); lame != juce::File()) return lame;
    return findProgram ("ffmpeg");
}

bool Session::exportMix (const juce::File& target, const ExportOptions& opts, juce::String& error)
{
    if (exporting.load()) { error = "An export is already running"; return false; }
    const auto encoder = findMp3Encoder();
    if (opts.format == ExportOptions::Format::mp3 && encoder == juce::File())
    {
       #if JUCE_WINDOWS
        error = "MP3 export needs ffmpeg or lame - install one (e.g. \"winget install ffmpeg\" in a terminal) "
                "or copy lame.exe next to Toneical.exe, or export a WAV";
       #else
        error = "MP3 export needs the 'lame' or 'ffmpeg' program - install one of them (e.g. sudo dnf install lame), "
                "or export a WAV";
       #endif
        return false;
    }

    juce::int64 from = 0, to = getLength();
    if (opts.loopRangeOnly && loopEnd.load() > loopStart.load()) { from = loopStart.load(); to = loopEnd.load(); }
    if (to <= from) { error = "There is nothing to export - the project has no audio"; return false; }

    // Snapshot everything the render needs; the tones are rebuilt as private copies so the
    // live audio thread keeps its own.
    struct Job { Lane lane; juce::ValueTree tone; float volume, pan; };
    std::vector<Job> jobs;
    const auto lanes = getArrangement();
    const bool anySolo = std::any_of (lanes.begin(), lanes.end(), [] (const Lane& l) { return l.track->solo.load(); });
    for (auto& l : lanes)
    {
        auto& t = *l.track;
        if (t.mute.load() || (anySolo && ! t.solo.load()) || l.clips.empty()) continue;
        jobs.push_back ({ l, t.useTone.load() ? t.tone.toValueTree() : juce::ValueTree(), t.volume.load(), t.pan.load() });
    }
    if (jobs.empty()) { error = "Every track with audio is muted"; return false; }

    exporting.store (true);
    exportCancel.store (false);
    exportProgress.store (0.0f);
    message ("Exporting " + target.getFileName() + "...");

    const double rate = sampleRate.load();
    juce::WeakReference<Session> weak (this);
    importPool.addJob ([this, jobs, from, to, rate, opts, target, encoder, weak]
    {
        constexpr int block = 4096;
        juce::String result;
        bool ok = false;
        {
            std::vector<std::unique_ptr<Chain>> tones;
            bool anyTone = false;
            for (auto& j : jobs)
            {
                std::unique_ptr<Chain> c;
                if (j.tone.isValid())
                {
                    c = std::make_unique<Chain>();
                    c->prepare (rate, block);
                    c->fromValueTree (j.tone);
                    anyTone = true;
                }
                tones.push_back (std::move (c));
            }

            // effects (delay, reverb) ring on after the last clip
            const juce::int64 tail = anyTone && ! opts.loopRangeOnly ? (juce::int64) (rate * 3.0) : 0;
            const auto total = to - from + tail;
            juce::AudioBuffer<float> mix (2, (int) total);
            mix.clear();
            // every track renders its chunk on its own core, then the chunks are summed
            struct Render
            {
                const std::vector<Job>* jobs;
                std::vector<std::unique_ptr<Chain>>* tones;
                std::vector<juce::AudioBuffer<float>> out;
                juce::int64 pos = 0;
                int n = 0;
            } render { &jobs, &tones, {}, 0, 0 };
            for (size_t i = 0; i < jobs.size(); ++i) render.out.emplace_back (2, block);
            WorkerPool pool (juce::jmin ((int) jobs.size(), juce::SystemStats::getNumCpus()) - 1);
            for (juce::int64 done = 0; done < total && ! exportCancel.load(); done += block)
            {
                render.pos = from + done;
                render.n = (int) std::min<juce::int64> (block, total - done);
                pool.run ((int) jobs.size(), [] (void* ctx, int i)
                {
                    auto& r = *static_cast<Render*> (ctx);
                    auto& buf = r.out[(size_t) i];
                    (*r.jobs)[(size_t) i].lane.read (r.pos, buf.getWritePointer (0), buf.getWritePointer (1), r.n);
                    if (auto& tone = (*r.tones)[(size_t) i]; tone != nullptr)
                        tone->process (buf.getWritePointer (0), buf.getWritePointer (1), r.n);
                }, &render);
                for (size_t i = 0; i < jobs.size(); ++i)
                {
                    const float vol = jobs[i].volume, pan = jobs[i].pan;
                    mix.addFrom (0, (int) done, render.out[i], 0, 0, render.n, vol * std::min (1.0f, 1.0f - pan));
                    mix.addFrom (1, (int) done, render.out[i], 1, 0, render.n, vol * std::min (1.0f, 1.0f + pan));
                }
                exportProgress.store (0.9f * (float) done / (float) total);
            }

            if (exportCancel.load())
                result = "Export cancelled";
            else
            {
                // drop the silent part of the tail
                int len = (int) (to - from);
                for (int i = (int) total - 1; i >= len; --i)
                    if (std::abs (mix.getSample (0, i)) > 3.0e-5f || std::abs (mix.getSample (1, i)) > 3.0e-5f) { len = i + 1; break; }
                mix.setSize (2, len, true);

                const float peak = mix.getMagnitude (0, len);
                if (opts.normalize && peak > 1.0e-6f) mix.applyGain (0.891f / peak);   // -1 dBFS
                else if (peak > 1.0f) result = "  (peaks at +" + juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS - it clips; use Normalize)";

                const bool mp3 = opts.format == ExportOptions::Format::mp3;
                const auto wavFile = mp3 ? juce::File::createTempFile (".wav") : target;
                wavFile.deleteFile();
                std::unique_ptr<juce::OutputStream> os (wavFile.createOutputStream());
                juce::WavAudioFormat wav;
                const bool isFloat = opts.format == ExportOptions::Format::wav32;
                auto writer = os == nullptr ? nullptr
                    : wav.createWriterFor (os, juce::AudioFormatWriterOptions{}
                                                   .withSampleRate (rate)
                                                   .withNumChannels (2)
                                                   .withBitsPerSample (isFloat ? 32 : 24)
                                                   .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                                              : juce::AudioFormatWriterOptions::SampleFormat::integral));
                ok = writer != nullptr && writer->writeFromAudioSampleBuffer (mix, 0, len);
                writer.reset();
                if (! ok) result = "Could not write " + wavFile.getFullPathName();

                if (ok && mp3)
                {
                    exportProgress.store (0.92f);
                    const auto kbps = juce::String (opts.mp3Kbps);
                    juce::StringArray args { encoder.getFullPathName() };
                    if (encoder.getFileName() == "lame")
                        args.addArray ({ "--quiet", "-b", kbps, "-q", "2", wavFile.getFullPathName(), target.getFullPathName() });
                    else
                        args.addArray ({ "-y", "-loglevel", "error", "-i", wavFile.getFullPathName(),
                                         "-codec:a", "libmp3lame", "-b:a", kbps + "k", target.getFullPathName() });
                    juce::ChildProcess p;
                    ok = p.start (args, juce::ChildProcess::wantStdErr) && p.waitForProcessToFinish (15 * 60 * 1000)
                         && p.getExitCode() == 0 && target.existsAsFile();
                    if (! ok) result = "MP3 encoding with " + encoder.getFileName() + " failed: " + p.readAllProcessOutput().trim();
                    wavFile.deleteFile();
                }
                if (ok)
                    result = "Exported " + target.getFullPathName() + " (" + juce::String ((double) len / rate, 1) + " s)" + result;
            }
        }
        exportProgress.store (1.0f);
        exporting.store (false);
        juce::MessageManager::callAsync ([weak, result] { if (auto* s = weak.get()) s->message (result); });
    });
    return true;
}

//==============================================================================
void Session::play()
{
    if (recState.load() == countingIn) return;
    playing.store (true);
    sendChangeMessage();
}

void Session::stop()
{
    const bool wasRecording = isRecording();
    recState.store (idle);
    playing.store (false);
    if (wasRecording) finishTake();
    sendChangeMessage();
}

void Session::setPosition (juce::int64 s)
{
    s = std::max<juce::int64> (0, s);
    position.store (s);
    seekRequest.store (s);
    sendChangeMessage();
}

void Session::setLoopRange (juce::int64 from, juce::int64 to)
{
    if (to < from) std::swap (from, to);
    loopStart.store (std::max<juce::int64> (0, from));
    loopEnd.store (std::max<juce::int64> (0, to));
}

void Session::setMetronome (bool on)
{
    if (on && ! metronomeOn.load()) resetFreeClick.store (true);
    metronomeOn.store (on);
}

void Session::setBpm (double b) { bpm.store (juce::jlimit (30.0, 300.0, b)); sendChangeMessage(); }

void Session::setTimeSignature (int num, int den)
{
    sigNum.store (juce::jlimit (1, 16, num));
    sigDen.store (den == 2 || den == 8 || den == 16 ? den : 4);
    sendChangeMessage();
}

double Session::samplesPerBeat() const noexcept
{
    return sampleRate.load() * 60.0 / bpm.load() * 4.0 / (double) sigDen.load();
}

void Session::startRecording (const juce::ValueTree& rigChain, const juce::String& rigName)
{
    if (isRecording()) return;

    auto arr = getArrangement();
    TrackPtr target;
    for (auto& lane : arr)
        if (lane.track->armed.load()) { target = lane.track; break; }
    if (target == nullptr)
    {
        target = makeTrack();
        target->armed.store (true);
        arr.push_back ({ target, {} });
        commit (std::move (arr));
    }

    // what you hear while recording is what the take will sound like
    target->tone.fromValueTree (rigChain);
    target->toneName = rigName;
    target->useTone.store (true);

    recTrack = target;
    take.clear();
    take.reserve ((size_t) (sampleRate.load() * 120.0));
    takePeaks.clear();
    partialPeak = 0; partialCount = 0;
    takeStarted.store (false);
    fifoOverflow.store (false);
    fifo.reset();

    if (playing.load())          recState.store (recording);   // punch in
    else if (countIn.load())     { countInPos = 0; recState.store (countingIn); }
    else                         { recState.store (recording); playing.store (true); }
    dirty = true;
    sendChangeMessage();
}

void Session::stopRecording()
{
    if (! isRecording()) return;
    if (recState.load() == countingIn) { stop(); return; }
    recState.store (idle);
    finishTake();
    sendChangeMessage();
}

void Session::finishTake()
{
    pump();   // collect what is still in the FIFO
    auto track = std::move (recTrack);
    recTrack = nullptr;
    if (take.empty() || track == nullptr || ! takeStarted.load()) return;

    auto data = std::make_shared<AudioData>();
    data->id = AudioData::makeId ("Take " + juce::String (++takeCounter));
    data->buffer.setSize (1, (int) take.size());
    std::copy (take.begin(), take.end(), data->buffer.getWritePointer (0));
    data->computePeaks();
    take = {};
    takePeaks.clear();

    Clip c;
    c.audio = data;
    c.start = takeStart.load() - recordLatency.load();   // the interface delivers the guitar late
    if (c.start < 0) { c.offset = -c.start; c.start = 0; }
    c.length = data->numSamples() - c.offset;
    c.name = "Take " + juce::String (takeCounter);
    if (c.length <= 0) return;

    auto arr = getArrangement();
    for (auto& lane : arr)
        if (lane.track == track)
        {
            lane.insert (c);
            commit (std::move (arr));
            message ("Recorded " + c.name + " on " + track->name + " (" + juce::String ((double) c.length / sampleRate.load(), 1) + " s)"
                     + (fifoOverflow.load() ? "  -  WARNING: the computer was too busy, some audio was lost" : ""));
            return;
        }
    message ("The take was discarded: its track was deleted while recording");
}

void Session::pump()
{
    // recorded audio: FIFO -> take
    for (;;)
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        if (n1 + n2 == 0) break;
        for (auto [s, n] : { std::pair { s1, n1 }, std::pair { s2, n2 } })
            for (int i = 0; i < n; ++i)
            {
                const float x = fifoData[(size_t) (s + i)];
                take.push_back (x);
                partialPeak = std::max (partialPeak, std::abs (x));
                if (++partialCount == AudioData::kPeakBlock)
                {
                    takePeaks.push_back (partialPeak);
                    partialPeak = 0;
                    partialCount = 0;
                }
            }
        fifo.finishedRead (n1 + n2);
    }

    // free versions the audio thread can no longer be using
    std::vector<std::unique_ptr<Version>> toFree;
    Arrangement lanes;
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        const auto done = blocksDone.load();
        const bool running = audioRunning.load();
        for (auto it = retired.begin(); it != retired.end();)
        {
            if (! running || done >= it->second + 2)
            {
                toFree.push_back (std::move (it->first));
                it = retired.erase (it);
            }
            else ++it;
        }
        lanes = live->lanes;
    }
    toFree.clear();
    for (auto& lane : lanes) lane.track->tone.collectGarbage();
}

//==============================================================================
void Session::prepare (double sr, int maxBlockSize)
{
    sampleRate.store (sr);
    maxBlock = std::max (16, maxBlockSize);
    fifoData.assign ((size_t) (sr * 8.0), 0.0f);
    fifo.setTotalSize ((int) fifoData.size());
    {
        const std::lock_guard<std::mutex> lock (editMutex);
        for (auto& lane : live->lanes)
            prepareTrack (*lane.track);
    }
    audioRunning.store (true);
}

void Session::release()
{
    audioRunning.store (false);
}

void Session::renderClick (double beatPos, double beatStep, float* out, int n, float level) noexcept
{
    const double sr = sampleRate.load (std::memory_order_relaxed);
    const int clickLen = (int) (0.04 * sr);
    const float attack = 1.0f / (float) (0.0006 * sr), decay = 1.0f / (float) (0.010 * sr);
    const int beatsPerBar = sigNum.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const double b = beatPos + beatStep * i;
        if ((juce::int64) std::floor (b) != (juce::int64) std::floor (b - beatStep))      // a beat starts on this sample
        {
            const auto beat = (juce::int64) std::floor (b);
            const bool downbeat = beat % beatsPerBar == 0;
            clickAge = 0;
            clickPhase = 0;
            clickInc = juce::MathConstants<double>::twoPi * (downbeat ? 1760.0 : 1175.0) / sr;
        }
        if (clickAge < clickLen)
        {
            const float env = std::min (1.0f, (float) clickAge * attack) * std::exp (-(float) clickAge * decay);
            out[i] += level * env * (float) std::sin (clickPhase);
            clickPhase += clickInc;
            ++clickAge;
        }
    }
}

// One playing track for one block, on whichever thread picks it up: its clips (DI or backing
// audio) through its own tone, then volume / pan, into the track's own buffers.
void Session::renderLane (const Lane& lane, int n) noexcept
{
    auto& t = *lane.track;
    float* a = t.outL.data();
    float* b = t.outR.data();
    for (int i = 0; i < numSegments; ++i)
        lane.read (segments[(size_t) i].pos, a + segments[(size_t) i].offset, b + segments[(size_t) i].offset, segments[(size_t) i].length);
    const int filled = numSegments > 0 ? segments[(size_t) numSegments - 1].offset + segments[(size_t) numSegments - 1].length : 0;
    std::fill (a + filled, a + n, 0.0f);
    std::fill (b + filled, b + n, 0.0f);
    if (t.useTone.load (std::memory_order_relaxed))
        t.tone.process (a, b, n);   // the DI (left) through the track's own rig

    const float vol = t.volume.load (std::memory_order_relaxed), pan = t.pan.load (std::memory_order_relaxed);
    const float gl = vol * std::min (1.0f, 1.0f - pan), gr = vol * std::min (1.0f, 1.0f + pan);
    const float stepL = (gl - t.lastGainL) / (float) n, stepR = (gr - t.lastGainR) / (float) n;
    float peak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        a[i] *= t.lastGainL + stepL * (float) (i + 1);
        b[i] *= t.lastGainR + stepR * (float) (i + 1);
        peak = std::max (peak, std::max (std::abs (a[i]), std::abs (b[i])));
    }
    t.lastGainL = gl;
    t.lastGainR = gr;
    if (peak > t.meter.load (std::memory_order_relaxed)) t.meter.store (peak, std::memory_order_relaxed);
}

void Session::runJob (void* context, int index)
{
    auto& s = *static_cast<Session*> (context);
    if (index == 0)
        s.rigFn (s.rigContext, s.blockOffset, s.blockLength);
    else
        s.renderLane (*s.activeLanes[(size_t) index - 1], s.blockLength);
}

void Session::process (const float* di, float* L, float* R, float* click, int numSamples, RigFn rig, void* rigCtx) noexcept
{
    // the processor never sends more than maxBlock, but stay safe
    for (int offset = 0; offset < numSamples; offset += maxBlock)
        processBlock (di + offset, L + offset, R + offset, click + offset, std::min (maxBlock, numSamples - offset),
                      rig, rigCtx, offset);
}

void Session::processBlock (const float* di, float* L, float* R, float* click, int n, RigFn rig, void* rigCtx, int offset) noexcept
{
    std::fill (click, click + n, 0.0f);
    const double step = 1.0 / samplesPerBeat();
    const float level = clickLevel.load (std::memory_order_relaxed);
    auto* v = current.load (std::memory_order_acquire);

    if (const auto s = seekRequest.exchange (-1); s >= 0)
        position.store (s);

    const int rs = recState.load();
    if (rs == countingIn || ! playing.load())
    {
        rig (rigCtx, offset, n);
        if (rs == countingIn)
        {
            // one bar of clicks, then the transport starts and the recording with it
            renderClick (countInPos, step, click, n, level);
            countInPos += step * n;
            if (countInPos >= (double) sigNum.load())
            {
                playing.store (true);
                recState.store (recording);
            }
        }
        else
        {
            // a stopped transport doesn't stop the practice metronome
            if (resetFreeClick.exchange (false)) freeBeatPos = 0;
            if (metronomeOn.load (std::memory_order_relaxed))
            {
                renderClick (freeBeatPos, step, click, n, level);
                freeBeatPos += step * n;
            }
        }
        blocksDone.fetch_add (1, std::memory_order_release);
        return;
    }

    // where on the timeline this block plays (two or more pieces when it crosses the loop end)
    const bool rec = rs == recording;
    const auto ls = loopStart.load(), le = loopEnd.load();
    const bool loop = loopOn.load() && ! rec && le > ls;
    auto pos = position.load();
    numSegments = 0;
    for (int done = 0; done < n && numSegments < (int) segments.size();)
    {
        int len = n - done;
        bool wrap = false;
        if (loop && pos < le && pos + len >= le) { len = (int) (le - pos); wrap = true; }
        if (len > 0)
        {
            segments[(size_t) numSegments++] = { pos, done, len };
            pos += len;
            done += len;
        }
        if (wrap) pos = ls;
    }

    // the live rig and every playing track are independent: run them in parallel
    activeLanes.clear();
    if (v != nullptr)
    {
        const bool anySolo = std::any_of (v->lanes.begin(), v->lanes.end(),
                                          [] (const Lane& l) { return l.track->solo.load (std::memory_order_relaxed); });
        for (auto& lane : v->lanes)
        {
            auto& t = *lane.track;
            const bool audible = ! t.mute.load (std::memory_order_relaxed) && (! anySolo || t.solo.load (std::memory_order_relaxed));
            if (audible && ! lane.clips.empty() && activeLanes.size() < activeLanes.capacity())
                activeLanes.push_back (&lane);
        }
    }
    rigFn = rig;
    rigContext = rigCtx;
    blockOffset = offset;
    blockLength = n;
    workers.run (1 + (int) activeLanes.size(), &Session::runJob, this);

    for (auto* lane : activeLanes)
    {
        juce::FloatVectorOperations::add (L, lane->track->outL.data(), n);
        juce::FloatVectorOperations::add (R, lane->track->outR.data(), n);
    }

    for (int i = 0; i < numSegments; ++i)
    {
        const auto& seg = segments[(size_t) i];
        if (metronomeOn.load (std::memory_order_relaxed))
            renderClick ((double) seg.pos * step, step, click + seg.offset, seg.length, level);
        if (rec)
        {
            if (! takeStarted.load (std::memory_order_relaxed))
            {
                takeStart.store (seg.pos);
                takeStarted.store (true);
            }
            int s1, n1, s2, n2;
            fifo.prepareToWrite (seg.length, s1, n1, s2, n2);
            std::copy_n (di + seg.offset, n1, fifoData.data() + s1);
            std::copy_n (di + seg.offset + n1, n2, fifoData.data() + s2);
            fifo.finishedWrite (n1 + n2);
            if (n1 + n2 < seg.length) fifoOverflow.store (true, std::memory_order_relaxed);
        }
    }
    position.store (pos);
    blocksDone.fetch_add (1, std::memory_order_release);
}

} // namespace pg::rec
