# Toneical 🎸

A standalone guitar rig for Linux: plug your guitar into an audio interface and play
through a pedalboard of drives, amps, cabinets and effects, including real-amp
**Neural Amp Modeler (NAM)** captures and cabinet **impulse responses (IRs)**.
It also builds as a **VST3** and **LV2** plugin for Reaper, Ardour, Bitwig, Carla...

```
guitar ──► interface input ──► [Gate] → [Drive] → [Amp or NAM] → [Cab or IR] → [Mod] → [Delay] → [Reverb] ──► headphones
```

---

## 1. Install

```bash
cd toneical
./install.sh
```

Run it as your normal user, not with `sudo`. The installer:

1. checks for the build tools and libraries, and offers to install missing ones with `sudo`
   (Fedora `dnf`; `apt` and `pacman` work too),
2. builds the app and plugins. The first build downloads JUCE, Eigen and
   NeuralAmpModelerCore, so it takes a few minutes,
3. copies everything into **`~/Toneical`**:

```
~/Toneical/
  bin/Toneical            the app
  bin/toneical            launcher (PipeWire/JACK, 48 kHz, low latency)
  plugins/                Toneical.vst3, Toneical.lv2
  Factory/factory.json    the factory presets (read at startup - see "Updates" below)
  Presets/                your saved boards (.toneical)
  Captures/               put .nam captures here (the NAM pedal opens this folder)
  IRs/                    put cabinet .wav IRs here (the IR Loader opens this folder)
  icon/                   the app icon
  uninstall.sh
```

4. adds an app-menu entry **Toneical** with the Toneical pedal icon, a `toneical` terminal command,
   and links the plugins into `~/.vst3` and `~/.lv2` so DAWs find them.

Useful options (`./install.sh --help` lists all of them):

| Option | What it does |
|---|---|
| `--dir PATH` | install somewhere other than `~/Toneical` |
| `--logo-font FILE` | use your own logo font, e.g. a licensed **Cattalague** `.ttf/.otf` |
| `--generic-cpu` | portable build without `-march=native` (to copy to another PC) |
| `--test` | also build and run the offline render test |
| `--skip-build` | reinstall the existing build without compiling |
| `--rebuild` / `--clean` | force a build / throw away the cached build and start over |
| `-y` | don't ask questions |

### Updates (fast)
Unpack the new version anywhere and run its `./install.sh`. You don't wait for a full
rebuild every time:

* **Presets are data, not code.** They live in `presets/factory.json` and get copied to
  `~/Toneical/Factory/factory.json`, which the app reads at startup. An update that only
  changes presets, docs or icons **skips compiling completely** and takes a few seconds.
  The installer fingerprints every file that ends up in the binaries and compares it with
  the last build.
* **The build is kept** in `~/.cache/toneical` between versions. The sources are mirrored
  there, and a file is only copied when its content really changed, so the compiler
  redoes only the touched files (a typical DSP tweak: under a minute instead of several).
  JUCE and the other libraries are downloaded once and reused.
* **ccache** (installed with the other build tools) caches compiled files, and link-time
  optimisation is off by default (`-DTONEICAL_LTO=ON` brings it back), so linking is quick too.

Your presets, captures and IRs are never touched by an update.
**Remove:** `~/Toneical/uninstall.sh`. It asks before deleting your own files.

### Run it

```bash
toneical                         # PipeWire/JACK, 48 kHz, 64-sample buffer (1.3 ms)
toneical 32                      # 0.67 ms
toneical 16                      # 0.33 ms - what AmpliTube calls "16 samples"
toneical --preset "Hot Lead"
toneical --size 1280x800         # open the window at a given size
```
or just click **Toneical** in the app menu.

## 2. First run with the M-Audio M-Track Solo

The M-Track Solo is **USB class compliant**, so Linux needs no driver, and it runs at
**48 kHz**, which is exactly what NAM captures expect.

1. Plug the guitar into the **1/4" instrument input** (the Line/Instrument switch on
   *Instrument*, which gives the 1 MΩ input a passive pickup wants). Turn the interface's
   gain up until the signal LED flickers green on hard picking, never red.
2. Click **Audio...** (top right). Choose the **JACK** device type (that is PipeWire when
   you start through `toneical` or the app menu), set **48000 Hz**, and make sure both input channels
   are enabled.
3. Pick **Input 1** or **Input 2** in the header: the input meter (and the tuner) only move
   when you've picked the channel your guitar arrives on.
4. Wear headphones, or keep the monitors down: the app un-mutes the input for you because a
   guitar rig has to hear its input, which also means a microphone near speakers can feed back.
5. Don't use the Solo's **direct monitor** while playing through Toneical, otherwise you hear the
   dry guitar on top of the processed one.
6. Click **Auto** next to *Input Gain* and play **hard** (full strums, all strings) for 6
   seconds. Toneical sets its input gain so your hardest picking peaks at -6 dBFS, which is
   the level the amps were tuned for (see "How the amp was tuned"). Do this again whenever
   you change guitars or touch the interface's gain knob. If it says the interface is
   clipping, turn the hardware gain down a little and calibrate again.

**Buffer size (latency).** The **Buffer** menu under *Audio...* goes down to **16 samples**
(0.33 ms at 48 kHz), like AmpliTube's buffer setting. You can also start with `toneical 16`.
* With **JACK** (PipeWire, the default) Toneical asks PipeWire for that buffer (the "quantum").
  PipeWire normally refuses anything below 32, so Toneical lowers PipeWire's minimum while
  it runs and puts the old setting back when you close it.
* With the **ALSA** device type (in *Audio...*) the buffer goes straight to the interface.
  This is the closest thing to ASIO on Windows, but other programs can't play sound while
  Toneical holds the device.

At 48 kHz one sample is 0.0208 ms, so 16 samples = 0.33 ms and 64 = 1.3 ms. On top of the
buffers come the USB converters (2-4 ms on most class-compliant interfaces), so even at
16 samples the round trip doesn't go to zero. Anything under ~10 ms feels "instant": that's
like standing 3 m from your amp, since sound travels ~34 cm per ms.

Small buffers need a CPU that never hesitates. A typical distorted board (drive + Toneical amp +
cab + delay + reverb) now uses about 15 % of one core. If you hear crackles, take the next
bigger buffer (32 or 64), which is still far below what you can feel.

## 3. What's on the board

The window has three parts, like a modern amp-sim:

* **Bottom: the pedalboard.** Your whole chain, from the guitar input plate on the left to
  the output plate on the right. The pedals are connected with patch cables (right-angle
  "pancake" plugs). Click a pedal to open it in the big view, click its little footswitch
  to bypass it, drag it to reorder, right-click for more.
* **Top: the selected gear, big.** This is where you tweak it. Every type of gear has its
  own body: compact pedals with a rubber treadle (the "Baby" pedals), classic stomp boxes,
  a wedge-shaped box for the Rodent and the Tremolo, a round fuzz, a wide box with faders
  for the EQ, an amp head with chicken-head knobs and a power switch, a speaker cab, and
  digital boxes with a screen for NAM and IRs. Each pedal has the same controls as the
  unit it's modelled on (e.g. Gain/Treble/Output on the Clone Kentaur, Level/Low/High/
  Distortion on the Baby HM-2). The designs are Toneical's own, not copies of the originals.
* **Right: the gear browser.** Search box and category tabs (Drive, Amp, Cab,
  Modulation...). **Drag** anything onto the pedalboard to insert it exactly where you drop
  it, or **double-click** to add it after the selected pedal.

Double-click any knob to reset it. Click the footswitch (or the power switch on the amp
and cab) to bypass. The red LED shows it's on.

| Pedal | What it is | Based on |
|---|---|---|
| **Noise Gate** | Silences hum/hiss between notes. It listens to the *clean guitar signal* but mutes wherever it sits, so put it **after the amp & cab**: it then removes the amp's hiss too. *Auto Threshold* measures your noise floor continuously (shown on the pedal) | ISP Decimator-style "key from the input" gate |
| **Squeeze Comp** | Evens out picking, adds sustain. Soft knee, parallel "Blend" | studio feed-forward compressor |
| **Clean Boost** | Pushes the next pedal/amp harder | MXR Micro Amp idea |
| **Green Screamer** | Mid-hump overdrive, the classic boost in front of a high-gain amp. **Captured, including the real Drive knob** | Ibanez **TS9** (GuitarML) |
| **Clone Kentaur** | Transparent overdrive. Captured at gain 6 and full gain; the Gain knob moves between them | **Klon Centaur** clone (Keith B) |
| **Exotic BB Preamp** | Open, amp-like overdrive. **Captured with its Gain knob** | **Xotic BB Preamp** (GuitarML) |
| **Proteen Dual Drive** | Boutique dual overdrive, Blue (lower gain) and Green side. **Captured with the Drive knobs** | **Browne Protein** (GuitarML) |
| **Seemore 805** | Mid-focused TS-family overdrive with a wide EQ | **Seymour Duncan 805** (Tim R) |
| **MXD Bad Mod 77** | Overdrive with a bass-bump switch (Bump off/on) | **MXR M77 Custom Badass Modified O.D.** (Tudor N) |
| **Baby SD-1** | TS-style but asymmetric clipping: looser, brighter | **Boss SD-1** (Phillipe P) |
| **Baby OS-2** | Overdrive/distortion blend | **Boss OS-2** (Helga B) |
| **Precise Drive** | The modern metal boost: drive at 0, tightens and focuses a high-gain amp | **Horizon Devices Precision Drive** (Phillipe P) |
| **Fortune Grind** | One-knob metal boost/tightener | **Fortin Grind** (Tudor N) |
| **Baby DS-1** | Hard-clipping distortion with a mid-scoop tone. Captured at dist 6 and max | **Boss DS-1** (Keith B) |
| **Rodent** | Gritty op-amp distortion; *Filter* turns darker to the right like the original | **ProCo RAT** (GuitarML) |
| **XD '78 Distortion** | Classic US distortion. **Captured with its Distortion knob** | **MXR '78 Custom Badass Distortion** (GuitarML) |
| **Baby MT-2** | 90s metal distortion: thick, compressed, scooped | **Boss MT-2 Metal Zone** (GuitarML) |
| **Baby HM-2** | The Swedish "chainsaw". Four captured settings, from everything-on-10 to milder | **Boss HM-2** (Peter N) |
| **Little Big Fluff** | Muff-style fuzz: a wall of sustain | **Electro-Harmonix Little Big Muff** (GuitarML) |
| **Toneical** (amp) | **Neural captures of real amps** played by NeuralAmpModelerCore: the capture does the distortion exactly like the real amp. Gain drives it harder/softer (level-compensated), Bass/Middle/Treble/Presence EQ after it, 6 amps | Fender Twin Reverb, Marshall JCM2000 crunch / lead (gain 6 and 8), Vox AC15 Top Boost, Peavey 5150 Block Letter: captures by Tim R, Phillipe P, Helga B (GPL v3, see `assets/nam/README.txt`) |
| **Valve Head** | Classic DSP tube amp: 2-4 triode stages, real passive Bass/Mid/Treble tone stack, power amp with sag & presence. Every part is tweakable. Great for cleans and edge-of-breakup; for heavy distortion use the Toneical amp | Fender / Marshall / Vox; tone stack from Yeh & Smith (DAFx 2006) |
| **NAM Capture** | Loads any `.nam` file: amp, full rig or pedal | NeuralAmpModelerCore (MIT) |
| **Cab Sim** | 5 built-in **real cabinet impulse responses** (no files needed) + Bright/Thump EQ | 4x12 Modern / British / Tight, Boutique 2x12, Small Combo |
| **IR Loader** | Real cabinet (or room) via impulse response `.wav` | convolution |
| **Studio EQ** | Bass / Lo-Mid / Hi-Mid / Treble + low & high cut | RBJ cookbook filters |
| **Stereo Chorus** | Two modulated delays, LFOs 90° apart → wide stereo | Boss CE-2 style |
| **Phase Shifter** | 4/6/8 all-pass stages swept by an LFO, feedback | MXR Phase 90 idea |
| **Tremolo** | Volume or stereo-pan tremolo, 3 shapes | Fender-style |
| **Echo** | Dark repeats, tape wow, ping-pong; time changes glide like an analog delay | analog/tape delay |
| **Hall Reverb** | Freeverb with pre-delay & low-cut on the wet signal | Jezar's Freeverb (public domain) |

**All drive, distortion and fuzz pedals are neural captures of the real pedals** (GPL v3
community captures; who captured what is in `assets/pedals/README.txt`). A capture is one
setting of the real pedal, so the knobs work like this:
* **learned knob**: some captures were trained with the pedal's Drive/Gain knob as an input,
  so turning it does exactly what the real knob does (Green Screamer, Exotic BB, Proteen,
  XD '78);
* **several settings**: where the real pedal was captured at a few settings (Clone Kentaur,
  Baby DS-1), the Drive knob switches to the nearest one;
* **otherwise**: Drive sets how hard you hit the capture, like a hotter or weaker pickup,
  with automatic level compensation;
* **Tone, Treble, Filter, Bright** are filters after the capture, set so that 5 = as captured.

Pedals that nobody has captured under an open licence were removed: the Fuzz Mate (Fuzz Face),
Baby BD-2 and XD Distortion++ (Distortion+). The Valve Head, Clean Boost and the
modulation/time effects are still DSP models, because they already behave like the originals.

Every drive pedal's **Level 0 dB = the same loudness as bypassed**, so +6 dB really is a 6 dB
boost into the amp. All factory presets are equally loud (-16 LUFS before the master), so
switching from a clean to a distorted preset doesn't jump in volume. The master output has a
**limiter** at -1 dBFS, so loud patches never hit the interface's harsh digital clipping.

### Why the distortion sounded compressed and lifeless, and how real plugins do it
The Valve Head builds distortion the "textbook" way: filters plus a few **static saturation
curves** (one `tanh` per tube stage). Fitting it to real amps made its loudness, its amount
of distortion and even its pick-attack statistics match. But when the *waveforms* were compared
on real playing, the output was still far from the real amp. The error-to-signal ratio
against the captured Marshall was 0.5 for crunch and 0.9 for lead (a good capture is under 0.01).
A static curve squashes every cycle the same way, regardless of what just happened. Real tube
stages don't: grid current charges the coupling capacitors and shifts each tube's bias after
every hard pick, cathode capacitors make the gain depend on frequency, and the power amp,
output transformer and speaker push against each other. That constantly moving behaviour is
what you hear as "alive". Without it, high gain sounds flat and compressed.

Good commercial plugins get there in one of two ways:
* **circuit simulation** (AmpliTube, Helix and others): every resistor, capacitor and tube is
  simulated, including those memory effects;
* **learning the real amp** (Neural Amp Modeler, ToneX, Neural DSP): a neural network is
  trained on recordings of the amp until its output matches sample by sample.

Toneical now does the second, inside the board: the **Toneical** amp head plays NAM captures of
real amps (GPL-licensed community captures) with the same NeuralAmpModelerCore that the
official NAM plugin uses. Against the raw captures, its error-to-signal ratio is 0.008. All the
distorted factory presets now use it. The Valve Head stays for cleans, which already sounded
right, and for anyone who wants to tweak an amp's insides. The drive and fuzz pedals had the
same problem in a milder form. On test tones they measured close to their captures, but on
real playing they sounded flat, so they are captures now too (see section 3).

Two other things changed on the way. The NAM "fast tanh" (also used by the official plugin)
halves the CPU cost of captures. The Valve Head uses the same approximation, which cut its
CPU use from 24 % to 9 % of a core with no audible difference (error-to-signal 0.00002).

### How the amp was tuned (and why the old version sounded wrong)
The first version guessed its gain settings, and measurement showed they were way off: the
crunch channel had about 35 dB too much gain. It was fully distorted even at the softest
picking (it behaved like a fuzz, with no touch response) and turned the guitar's hum and hiss
up to almost the same level as the notes.

So the amp was rebuilt and **fitted to Neural Amp Modeler captures of real amps**: a Fender
Twin Reverb (clean), a Marshall JCM2000 (crunch and lead) and a Vox AC15 (chime). A test tone
was stepped from -60 to 0 dBFS through both the real capture and Toneical, and an optimiser
(`tools/fit_amp.py`) adjusted each channel's gain staging, power-amp drive, asymmetry and
filters until three things matched: output level vs picking strength (touch sensitivity),
distortion vs picking strength (how it breaks up) and the spectrum on a real DI guitar
recording.

| Channel | Dynamic range over 60 dB of picking | Noise below playing (no gate) |
|---|---|---|
| Crunch: real JCM2000 / old / **new** | 33.6 / 1.8 / **32.7 dB** | 42.5 / 9.7 / **42.6 dB** |
| Lead: real JCM2000 / old / **new** | 12.3 / 0.2 / **11.0 dB** | 27.0 / 0.7 / **26.0 dB** |

**Power chords (October 2026 rework).** Steady test tones can't tell *where* an amp distorts,
but power chords can. Clipping in the power amp flattens every pick attack, and that is what
made the distorted presets sound compressed and "mashed". So each channel was fitted a third
time with real chords and palm-muted chugs (`tools/fit_amp_chords.py`, `tools/chord_metrics.py`)
against the same real-amp captures through the same cabinet IR, measuring **pick-attack punch**
(attack vs sustain), **chug definition** (how fast a muted note dies), **soft/hard dynamics**
and **intermodulation** (how much two notes smear into each other). The fitted amp now
distorts mostly in the preamp, as real high-gain amps do, with less power-amp squash, a
tighter bass cut between stages and less fizz above 5 kHz (the tone balance is within
~1 dB of the real amp from 125 Hz to 8 kHz).


### Fun fact: why the Tube Screamer is everyone's favourite boost
Its clipping path has a high-pass at 720 Hz, so the bass never gets distorted. It just passes
through clean. That's why players put it in front of a high-gain amp: it tightens the low end
and pushes the mids instead of adding mud.

## 4. Getting amps, pedals and cabinets (free, open)

* **[TONE3000](https://www.tone3000.com)**: the largest library of free NAM captures (amps,
  full rigs *and* pedals) and cabinet IRs. Download a pack, unzip, then click **Load .nam...** in a
  **NAM Capture** pedal.
  * "**Amp head**" captures need a cab after them: add an **IR Loader** (or **Cab Sim**).
  * "**Full rig**" / "combo" captures already include the speaker, so don't add a cab.
  * "**Pedal**" captures (Klon, DS-1, Big Muff, ...) go *before* an amp capture. Use as many NAM
    pedals as your CPU allows.
* Most captures are 48 kHz. If the NAM pedal shows a red warning, set the device to the
  sample rate it names.
* **Normalize** (on by default) evens out the loudness between captures, so swapping a quiet
  clean capture for a loud metal one doesn't blow your ears.

Load the **"NAM Rig (load your captures)"** preset for a ready-made empty slot layout.

## 5. Presets

56 factory boards in seven groups (the preset menu is sorted by group). All of them are
level-matched (-16 LUFS), and picking one shows its description in the status bar.

| Group | Presets |
|---|---|
| **Clean** | Clean Sparkle, Glassy Funk, Neo-Soul Clean, Chime & Tremolo, Blues Breakup, Chicken Pickin', Chimey Edge |
| **Rhythm** | Classic Crunch, Brown Sound, Hard Rock Rhythm, Grunge Distortion, Thrash Rhythm, Tight Metal, Modern Djent, Swedish Chainsaw, Zone Wall, Garage Rock, Rodent Punk, Fuzz Wall, Stoner Doom |
| **Lead** | Hot Lead, Texas Blues Lead, Purple Fuzz Lead, Floating Fuzz Solo, Hard Rock Lead, Transparent Lead, BB Preamp Lead, Proteen Lead, Bump & Grind Lead, Metal Lead, Crazy Trane Lead |
| **Legends** | Gary Moor - Still Got the Bluez, Gary Moor - Parisian Walkways, Santanna - Singing Sustain, Brian Mae - Treble Booster, Knopflair - Sultans Clean, B.B. Kling - Lucy, Satchriani - Surfing, Zack Wild - Pinch Harmonics, Morrello - Raging Riffs, Dimebagg - Cowboys, Angus Jung - Back in Blak, Jimmy Paige - Whole Lotta, Claptone - Woman Tone, Erik Jonson - Violin Lead, Kurt Kobane - Teen Spirits |
| **Nu-Metal** | Linkin Bark - Hybrid Wall, Limp Biscuit - Bounce Riffs, System of a Crown - Toxic Riffs, Kornn - Freak Riffs |
| **Ambient & FX** | Dotted Eighths, Ambient Swells, Shoegaze Wash, Slow Phase Clean |
| **Utility** | NAM Rig (load your captures), Empty Board |

The Legends and Nu-Metal boards are *inspired by* well-known players and bands; their names
are deliberate sound-alikes, and they make no claim to be anyone's actual rig. Their starting
settings came from published "how to get this tone" guides (GuitarChalk, KillerRig, RiffHard,
GuitarMasterclass, Mixdown, Vintage Digital, Guitar World), converted to Toneical's gear.

**How the boards are built.**
* Distortion comes from captures: the **Toneical** amp and the captured pedals.
* A drive in front of a distorted amp is used as a *boost*: drive 0-2, level up (Green Screamer,
  Baby SD-1, Precise Drive, Fortune Grind). This tightens the low end instead of adding fuzz.
* No compressor in front of a distorted amp (Parisian Walkways keeps a light one, at 45 % blend).
* The cleans keep the Valve Head, which already sounded right.

**Editing presets.** The factory boards are plain data in `presets/factory.json` (one pedal per
line), generated from `tools/build_presets.py`. Change a knob there and run `./install.sh`: it
only copies the file, with no rebuild. `tools/level_presets.py` re-levels them and
`tools/preset_report.py` measures them.

Knob positions given as clock positions were converted to 0-10 (7 o'clock = 0, 12 = 5, 5 o'clock = 10).
* **Save** writes a `.toneical` file (plain XML) to `~/Toneical/Presets/`. It stores
  every knob plus the *paths* of the NAM/IR files, so keep your captures in a stable folder.
* The last board is restored automatically when you start the app.

## 6. Project layout

```
Source/
  PluginProcessor.*      input select, gains, meters, tuner feed, state save/restore
  PluginEditor.*         header (presets, input, tuner, master), pedalboard, status bar
  Chain.*                ordered module list; lock-free hand-over to the audio thread
  Presets.*              factory boards + .toneical files
  Tuner.h                YIN pitch detector (de Cheveigné & Kawahara, 2002)
  dsp/DspHelpers.h       biquads, one-poles, delay line, tone stack, waveshapers, fast tanh
  dsp/LstmModel.h        the LSTM network of GuitarML (Proteus) captures
  dsp/RateBridge.h       runs 44.1 kHz captures inside a 48 kHz session (windowed sinc, any block size)
  modules/
    Module.*             base class: params, click-free bypass, state
    Dynamics.h           gate, compressor, boost
    Drives.h             oversampling base class
    CapturedPedals.*     all drive / distortion / fuzz pedals (neural captures)
    CapturedAmp.*        the Toneical amp (neural captures of real amps)
    Amp.h                Valve Head amp, Cab Sim, IR Loader
    Effects.h            EQ, chorus, phaser, tremolo, delay, reverb
    NamModule.*          Neural Amp Modeler slot
    ModuleFactory.*      registry used by the "Add pedal" menu
  gui/
    PedalComponent.*     the big, editable view of the selected gear
    PedalArt.*           enclosure shapes, finishes, knobs, patch cables (shared by all views)
    ChainStrip.*         the pedalboard at the bottom (drag & drop, cables)
    GearBrowser.*        the gear browser on the right
    LookAndFeel.h        colours, knob/fader/toggle drawing
presets/factory.json     the factory presets (data - edit and re-run install.sh, no rebuild)
tests/RenderTest.cpp     offline test: every pedal & preset, NAM/IR loading, threading stress
tests/RenderCli.cpp      ToneicalRender: run any WAV through presets / pedals / NAM captures
tools/                   measurement & fitting scripts (analyze, curves, spectra, fit_amp*,
                         chord_metrics, preset_report, level_presets)
assets/irs/              built-in cabinet IRs (GPL, see README there)
assets/nam/              Toneical amp captures (GPL v3, see README there)
assets/pedals/           pedal captures (GPL v3, see README there)
assets/fonts/            logo font (Arizonia, SIL Open Font License)
assets/icon/             app icon (pedal artwork, PNG 16-512 px + source)
install.sh               installer (see section 1)
```

### How audio stays glitch-free
The audio thread must never wait for a lock or allocate memory, because a few ms of delay
would be an audible click. So when you add/move/remove a pedal, the GUI builds a *new* list
and swaps one atomic pointer. The old list is deleted later, only after the audio thread has
finished a full block with the new one. NAM models load and pre-warm on a background thread
and are swapped in the same way.

### Add your own pedal (example: a simple "Octave Down" is ~30 lines)
1. Subclass `pg::Module` (or `OversampledModule` if it clips) in `Source/modules/`.
2. Describe the knobs in the constructor with `ParamSpec::knob/choice/toggle`.
3. Implement `prepareDsp()` (allocate here) and `processDsp()` (no allocation!).
4. Add one `add (&make<YourPedal>);` line in `ModuleFactory.cpp`. The GUI builds the
   pedal (knobs, footswitch, colour) automatically.

### Run the test
```bash
cmake --build build --target ToneicalRenderTest
./build/ToneicalRenderTest_artefacts/Release/ToneicalRenderTest /tmp/toneical-render some_capture.nam
```
It renders a synthetic plucked guitar through every pedal and preset (WAVs in the folder),
checks for NaN/clipping/silence, times the CPU cost and edits the chain 3000 times while
audio runs.

## 7. More open-source DSP worth borrowing next

* **Airwindows** (MIT): hundreds of tiny, well-known effects (consoles, tape, reverbs). The
  `processReplacing` loops port almost 1:1 into a `processDsp()`.
* **ChowDSP BYOD** (GPL-3): circuit-accurate pedals (Klon-style Centaur, Muff variations).
  It's GPL, which is fine as long as Toneical stays open source.
* **Guitarix** (GPL): huge Faust-based amp/pedal collection; its LV2 plugins also run in Carla.

## Licence

Toneical is free and open-source software, licensed under the **GNU Affero General Public
License v3.0 or later** (`LICENSE`). You may use, study, change and share it; if you distribute
a modified version (or run it as a network service), you must share its source under the same
licence. The AGPL is required because Toneical is built on the open-source edition of JUCE.

The bundled amp and pedal captures and cabinet IRs are GPL, the logo font is OFL; every
third-party component and its licence is listed in `THIRD_PARTY.md`. Each `.nam`/IR file you
load yourself is licensed by whoever captured it.

## Contributing

Bug reports, captures, presets and pull requests are welcome. Presets are plain data in
`presets/factory.json` (generated by `tools/build_presets.py`); run `ToneicalRenderTest`
before sending a change to the DSP.
