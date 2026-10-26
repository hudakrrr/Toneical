#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Source of the factory presets (writes presets/factory.json).

Rules the presets follow:
  * distortion comes from captures: the Real Amp (captured amps) and the captured pedals;
  * drives in front of a distorted amp are used as boosts (drive low, level up);
  * no compressor in front of a distorted amp;
  * the cleans keep the Valve Head, which already sounded right.
Masters are set to 0 here and levelled afterwards with tools/level_presets.py, which keeps
whatever it measured last time when you re-run this script with --keep-levels.

The "Legends" and "Nu-Metal" boards are *inspired by* well-known players and bands; their
names are deliberately playful sound-alikes, not the real names.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import presets_json as P

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "presets", "factory.json")

def m(t, bypassed=False, **params):
    e = {"type": t, "params": params}
    if bypassed: e["bypassed"] = True
    return e

def valve(ch, gain, bass=5, mid=5, treble=6, presence=5):
    return m("tubeamp", channel=ch, gain=gain, bass=bass, mid=mid, treble=treble, presence=presence, master=0)

# Real Amp channels: 0 US Twin clean, 1 British 2000 crunch, 2 British 2000 lead, 3 lead+ (hotter),
#                    4 AC Top Boost, 5 US Block Letter high gain.  EQ: 5 = flat (as captured).
def real(ch, gain=5, bass=5, mid=5, treble=5, presence=5):
    return m("realamp", channel=ch, gain=gain, bass=bass, mid=mid, treble=treble, presence=presence, master=0)

def cab(n, **kw): return m("cabsim", cab=n, **kw)
def gate(margin=10, release=150): return m("gate", margin=margin, release=release)
def delay(time, fb, mix, **kw): return m("delay", time=time, fb=fb, mix=mix, **kw)
def verb(size, mix, **kw): return m("reverb", size=size, mix=mix, **kw)

presets = []
def add(name, cat, desc, *chain):
    presets.append({"name": name, "category": cat, "description": desc, "chain": list(chain)})

# =============================================================================== CLEAN
add("Clean Sparkle", "Clean", "Compressed, chorused Blackface clean with delay and a roomy reverb.",
    m("comp", thresh=-28, ratio=3, attack=12, level=0, blend=55),
    valve(0, 4.5, 5, 4, 6, 6), cab(3), gate(9, 250),
    m("chorus", rate=0.7, depth=4, mix=35), delay(420, 28, 18), verb(60, 22))
add("Glassy Funk", "Clean", "Snappy compressed clean for funk rhythm: fast compressor, bright small combo, short room.",
    m("comp", thresh=-32, ratio=6, attack=4, release=100, level=2, blend=75),
    valve(0, 4, 4, 5, 7, 6), cab(4), gate(9, 120), verb(25, 10, damp=50))
add("Neo-Soul Clean", "Clean", "Smooth neo-soul clean: the Clone Kentaur at low gain with the treble up, into a big clean amp, a touch of compression and a warm room.",
    m("comp", thresh=-28, ratio=3, attack=15, blend=50),
    m("klon", gain=3, treble=6, level=2),
    valve(0, 5.5, 5, 6, 6, 5), cab(3), gate(9, 250), verb(50, 22, damp=40, predelay=25))
add("Chime & Tremolo", "Clean", "Vox-style chime with a classic amp tremolo.",
    valve(3, 4, 4, 6, 6.5, 5), cab(3), gate(10, 150), m("trem", rate=5.5, depth=55), verb(45, 22))
add("Blues Breakup", "Clean", "Edge-of-breakup clean nudged by the Exotic BB Preamp at low gain: dig in and it growls, pick softly and it cleans up.",
    m("bb", gain=3, tone=5, level=3),
    valve(0, 6.5, 5, 6, 6, 5), cab(4), gate(9, 200), verb(40, 20, damp=45))
add("Chicken Pickin'", "Clean", "Country clean: fast compressor for the snap, a bright small combo and a single 110 ms slapback repeat. Play hybrid picking near the bridge.",
    m("comp", thresh=-30, ratio=6, attack=2, release=90, level=1, blend=80),
    valve(0, 4, 4, 6, 7, 6), cab(4), gate(9, 150), delay(110, 0, 28, tone=5000, mod=0), verb(25, 8))
add("Chimey Edge", "Clean", "A captured AC Top Boost just starting to break up, nudged by the Seemore 805 at low gain - jangly chords that bite when you dig in.",
    m("sd805", gain=3, tone=5, level=3),
    real(4, 4, 5, 5, 5, 5), cab(3), gate(9, 200), verb(40, 16))

# =============================================================================== RHYTHM
add("Classic Crunch", "Rhythm", "A captured British crunch channel into a 4x12 - classic rock rhythm. The Green Screamer is ready (off) as a solo boost.",
    m("ts808", True, drive=1, tone=5, level=6),
    real(1, 5), cab(1), gate(10), m("delay", True, time=360, fb=25, mix=18), verb(40, 12))
add("Brown Sound", "Rhythm", "Late-70s Californian hot-rodded Plexi style: the crunch capture pushed hard, a slow phaser and a plate-ish reverb.",
    real(1, 7, 6, 5, 5.5, 4.5), cab(1), gate(10),
    m("phaser", rate=0.35, depth=6, fb=20, stages=0, mix=45), delay(330, 15, 10), verb(55, 18, damp=30))
add("Hard Rock Rhythm", "Rhythm", "Late-80s hard-rock rhythm: the British crunch capture with the mids up, pushed by a Baby SD-1 at low drive so chords stay tight.",
    m("sd1", drive=3, tone=5, level=4),
    real(1, 5.5, 5, 6, 5, 5), cab(1), gate(11), verb(35, 10))
add("Grunge Distortion", "Rhythm", "Early-90s grunge: a Baby DS-1 into a captured clean Twin, mids up - raw and loud. Chorus is ready (off) for the verses.",
    m("ds1", dist=7, tone=4, level=0),
    real(0, 5, 5, 6, 5, 5), cab(1), gate(11), m("chorus", True, rate=0.9, depth=7, mix=45), verb(30, 8))
add("Thrash Rhythm", "Rhythm", "Mid-80s thrash: Green Screamer as a tight boost into the captured US high-gain head, mids scooped with the EQ (low up, 750 Hz down, 2.2 kHz up).",
    m("ts808", drive=0, tone=6, level=8),
    real(5, 4.5, 6, 4, 5.5, 5), cab(1), gate(14, 60),
    m("eq", low=2, lomid=-3, himid=2, hpf=60), verb(25, 6))
add("Tight Metal", "Rhythm", "Modern metal: the Precise Drive (drive at 0, level up - the classic metal boost) into the captured high-gain head, tight 4x12 and a fast gate.",
    m("precision", drive=0, bright=6, level=8),
    real(5, 5.5, 5, 4.5, 5, 5.5), cab(2), gate(14, 60), m("eq", lomid=-2, hpf=70), verb(30, 8))
add("Modern Djent", "Rhythm", "Palm-muted, very tight modern metal: Fortune Grind boost into the high-gain capture, low end cleaned up before it gets fuzzy.",
    m("grind", grind=6, level=6),
    real(5, 6, 4.5, 5, 5.5, 6), cab(2), gate(16, 40), m("eq", lomid=-3, hpf=85, high=1))
add("Swedish Chainsaw", "Rhythm", "Old-school Swedish death metal: the Baby HM-2 with every knob on 10 into a crunch amp - the famous buzzsaw.",
    m("hm2", setting=0, dist=5, level=0),
    real(1, 3.5, 5, 5, 5, 5), cab(1), gate(14, 70), verb(30, 8))
add("Zone Wall", "Rhythm", "A Baby MT-2 (the 90s metal-distortion box) into a clean amp - thick, compressed, scooped: love it or hate it.",
    m("mt2", dist=5, tone=5, level=0),
    real(0, 5, 5, 5, 4.5, 5), cab(2), gate(14, 60), verb(25, 6))
add("Garage Rock", "Rhythm", "A Baby OS-2 at mid drive into a captured AC Top Boost - trashy, chimey garage-rock chords.",
    m("os2", drive=6, tone=5, level=0),
    real(4, 4.5, 5, 5, 5, 5), cab(3), gate(11), verb(30, 10))
add("Rodent Punk", "Rhythm", "The Rodent (RAT-style distortion) with the filter at 10 o'clock into a lightly crunching amp - the classic punk and indie wall, open and loud.",
    m("rat", dist=5, filter=3.5, level=0),
    real(1, 2.5, 5, 5, 5, 5), cab(1), gate(12), verb(30, 8))
add("Fuzz Wall", "Rhythm", "The Little Big Fluff (Muff-style fuzz) into a clean amp - a wall of sustain.",
    m("muff", sustain=6, tone=5, level=0),
    real(0, 5, 5, 5, 5, 5), cab(3), gate(11), verb(50, 16))
add("Stoner Doom", "Rhythm", "The Little Big Fluff, dark and dimed, into a low-gain crunch with heavy bass - slow and huge.",
    m("muff", sustain=8, tone=3, level=0),
    real(1, 3, 7, 5, 4.5, 4), cab(0, thump=2), gate(12, 150), verb(45, 12, damp=60))

# =============================================================================== LEAD
add("Hot Lead", "Lead", "Singing high-gain lead: Green Screamer boost (low drive, level up) into the captured British lead channel with the mids forward, a 4x12, gentle gate and delay.",
    m("ts808", drive=1.5, tone=5, level=8),
    real(2, 5, 4.5, 6, 5, 5), cab(1), gate(9, 250), delay(400, 30, 20, tone=3000, mod=2), verb(50, 14, damp=45))
add("Texas Blues Lead", "Lead", "Texas blues: Green Screamer as a boost (low drive, level high) into a loud captured Twin, bright and punchy.",
    m("ts808", drive=2, tone=6, level=9),
    real(0, 7, 6, 5.5, 5.5, 5), cab(4), gate(9, 250), verb(40, 20, damp=35))
add("Purple Fuzz Lead", "Lead", "Late-60s psychedelic lead: the Little Big Fluff into a crunching British amp with the mids up. Roll back your guitar volume to clean it up.",
    m("muff", sustain=6, tone=6, level=0),
    real(1, 4.5, 5, 6, 5, 6), cab(1), gate(9, 250), verb(35, 12))
add("Floating Fuzz Solo", "Lead", "Long, floating 70s prog solos: the Little Big Fluff into a loud, clean, mid-focused amp, slow chorus, long delay and hall.",
    m("muff", sustain=6, tone=5, level=0),
    real(0, 6, 5, 6, 5, 5), cab(3), gate(9, 300),
    m("chorus", rate=0.5, depth=3, mix=25), delay(440, 35, 24, tone=3500), verb(75, 25, damp=40, predelay=30))
add("Hard Rock Lead", "Lead", "Late-80s hard-rock lead: a Baby SD-1 pushing the captured British lead channel with the mids up, plus a touch of delay.",
    m("sd1", drive=4, tone=5, level=5),
    real(2, 5.5, 5, 6, 5, 5), cab(1), gate(9, 250), delay(380, 22, 16), verb(40, 12))
add("Transparent Lead", "Lead", "The Clone Kentaur at higher gain stacked into the crunch capture: more of the amp, smooth and touch-sensitive.",
    m("klon", gain=6, treble=5.5, level=4),
    real(1, 5, 5, 5, 5, 5), cab(3), gate(9, 250), delay(420, 28, 18), verb(45, 14))
add("BB Preamp Lead", "Lead", "The Exotic BB Preamp as the main drive into an amp that's only just breaking up - open, raw blues-rock that follows your picking.",
    m("bb", gain=6.5, tone=5.5, level=2),
    real(1, 3.5, 5, 5, 5, 5), cab(3), gate(9, 250), delay(380, 22, 14, tone=3000), verb(45, 14))
add("Proteen Lead", "Lead", "The green (more gain) side of the Proteen Dual Drive into a crunch amp - creamy, mid-rich boutique lead.",
    m("protein", setting=1, drive=6, tone=5, level=2),
    real(1, 4, 5, 5.5, 5, 5), cab(1), gate(9, 250), delay(420, 26, 18), verb(45, 14))
add("Bump & Grind Lead", "Lead", "The MXD Bad Mod 77 with the bass bump on, pushing the hot British lead capture - thick, aggressive solos.",
    m("m77", setting=1, gain=3, tone=5, level=6),
    real(3, 5, 5, 5.5, 5, 5), cab(1), gate(10, 200), delay(400, 25, 16), verb(45, 12))
add("Metal Lead", "Lead", "Boosted high-gain lead with the treble up, delay and hall - for cutting metal solos.",
    m("ts808", drive=1, tone=6, level=9),
    real(5, 5.5, 5, 5.5, 6, 6), cab(0), gate(10, 200), delay(450, 30, 22), verb(55, 16))
add("Crazy Trane Lead", "Lead", "Early-80s metal lead in the spirit of a famous train-themed solo: the XD '78 Distortion into a British crunch, plus delay.",
    m("mxr78", dist=6, level=3),
    real(1, 5.5, 5, 6, 5.5, 6), cab(1), gate(9, 250), delay(400, 25, 18), verb(45, 14))

# =============================================================================== LEGENDS (sound-alike names)
add("Gary Moor - Still Got the Bluez", "Legends", "Inspired by a famous Irish blues-rock player: Les Paul neck pickup into a British lead channel (gain 7, bass 6, middle 7, treble 6 as a start), smoothed by a Green Screamer, with delay. Lots of finger vibrato.",
    m("ts808", drive=2.5, tone=5, level=5),
    real(2, 5.5, 5.5, 6, 5, 5), m("eq", lpf=7000), cab(1), gate(8, 400),
    delay(420, 22, 16, tone=3000), verb(55, 18, damp=40))
add("Gary Moor - Parisian Walkways", "Legends", "Inspired by the same player's endless sustained note: high amp gain and finger vibrato, a light studio-style compressor, a plate-like reverb.",
    m("comp", thresh=-30, ratio=3, attack=30, release=300, blend=45),
    real(3, 5, 5.5, 6, 5, 5), m("eq", lpf=7500), cab(1), gate(7, 500),
    delay(500, 28, 16, tone=3200), verb(70, 26, damp=35, predelay=20))
add("Santanna - Singing Sustain", "Legends", "Inspired by a Latin-rock legend: a mid-heavy singing lead (bass 5, mid 6+, treble 6 as a start), neck pickup.",
    m("comp", thresh=-28, ratio=2.5, attack=25, blend=40),
    real(2, 5, 5, 7, 5, 4.5), cab(3), gate(8, 400), delay(380, 20, 12), verb(50, 22))
add("Brian Mae - Treble Booster", "Legends", "Inspired by a famous queenly guitarist: a treble booster into a cranked AC Top Boost, booster as the main drive, short slapback delay.",
    m("boost", gain=12, lowcut=600, bright=1),
    real(4, 5.5, 5.5, 5.5, 5.5, 6), cab(3), gate(9, 300), delay(300, 20, 18), verb(40, 12))
add("Knopflair - Sultans Clean", "Legends", "Inspired by a famous fingerpicking Strat player: position 4 into a clean Fender (volume 4, bass 5, middle 6, treble 7), light compression, a little reverb. Play it with your fingers.",
    m("comp", thresh=-26, ratio=3, attack=10, blend=55),
    valve(0, 4, 5, 6, 7, 6), cab(4), gate(9, 200), verb(30, 12))
add("B.B. Kling - Lucy", "Legends", "Inspired by the king of the blues: a mid-forward, slightly gritty clean with plenty of reverb. Neck pickup, sparse notes, wide vibrato.",
    valve(0, 7, 4, 8, 6, 4), cab(4), gate(9, 250), verb(45, 28, damp=45))
add("Satchriani - Surfing", "Legends", "Inspired by an instrumental rock virtuoso: a Baby DS-1 into a British amp with low EQ settings, short low-repeat delay, no reverb.",
    m("ds1", dist=6, tone=6, level=3),
    real(1, 4.5, 4, 4, 4.5, 6), cab(1), gate(10, 200), delay(280, 15, 15))
add("Zack Wild - Pinch Harmonics", "Legends", "Inspired by a bearded metal guitarist: the XD '78 Distortion into a British lead channel with the mids pushed. Dig in for squealing pinch harmonics.",
    m("mxr78", dist=6, level=5),
    real(2, 4.5, 5.5, 6, 5, 6), m("eq", himid=2, hpf=60), cab(1), gate(13, 120), verb(35, 8))
add("Morrello - Raging Riffs", "Legends", "Inspired by a famous rap-metal riff maker: British lead channel, bright, no reverb. An EQ pedal set flat with a little more level is the solo boost - switch it on for solos.",
    real(2, 4.5, 4.5, 4.5, 7, 5), cab(1), gate(12, 90), m("eq", True, level=4))
add("Dimebagg - Cowboys", "Legends", "Inspired by a legendary groove-metal guitarist: high gain with a smile-shaped EQ (low up, 500-800 Hz down, top up) and a boost in front. Tight gate.",
    m("ts808", drive=0, tone=7, level=8),
    real(5, 6, 6, 4, 5.5, 6), m("eq", low=3, lomid=-4, high=2, hpf=50), cab(2), gate(15, 50))
add("Angus Jung - Back in Blak", "Legends", "Inspired by a schoolboy-uniform rock legend: no pedals, a Plexi-style amp just past breakup. Turn your guitar volume down for the cleaner parts.",
    real(1, 4.5, 5, 5.5, 5.5, 6), cab(1), gate(10), verb(35, 10))
add("Jimmy Paige - Whole Lotta", "Legends", "Inspired by a 70s hard-rock pioneer: Les Paul into a cranked British amp with a short, wobbly tape-style delay.",
    real(1, 5.5, 5, 5.5, 5, 5), cab(1), gate(10), delay(260, 25, 15, tone=2500, mod=5), verb(40, 14))
add("Claptone - Woman Tone", "Legends", "Inspired by the late-60s 'woman tone': neck humbucker with the tone rolled off into a cranked British amp. The EQ does the roll-off, so it works with any guitar.",
    real(2, 5.5, 5.5, 6, 4, 4), m("eq", lpf=2200, lomid=2), cab(1), gate(9, 300), verb(40, 12))
add("Erik Jonson - Violin Lead", "Legends", "Inspired by a Texan tone perfectionist: a Green Screamer into a British crunch plus long, warm echoes - smooth and vocal.",
    m("ts808", drive=4, tone=4, level=3),
    real(1, 4.5, 5, 5.5, 5, 5), m("eq", lpf=6500), cab(3), gate(8, 400), delay(380, 30, 22, tone=3000), verb(55, 18))
add("Kurt Kobane - Teen Spirits", "Legends", "Inspired by early-90s grunge: chorus/distortion switching - a Baby DS-1 into a clean amp, with chorus for the verses (switch the DS-1 off and the chorus on).",
    m("ds1", dist=6.5, tone=4, level=0),
    real(0, 5, 5.5, 6, 5, 5), cab(3), gate(11), m("chorus", True, rate=1.0, depth=6, mix=45), verb(30, 8))

# =============================================================================== NU-METAL (sound-alike names)
add("Linkin Bark - Hybrid Wall", "Nu-Metal", "Inspired by early-2000s nu-metal walls of guitar: a boosted high-gain head into a big 4x12, tight gate. Play drop-tuned or a 7-string.",
    m("ts808", drive=0, tone=6, level=7),
    real(5, 5.5, 6, 4.5, 5, 5.5), cab(0, thump=2), gate(15, 50), m("eq", low=2, lomid=-3, hpf=45))
add("Limp Biscuit - Bounce Riffs", "Nu-Metal", "Inspired by bouncy late-90s nu-metal: drop C, scooped mids, bright top, a Baby SD-1 as the boost.",
    m("sd1", drive=1.5, tone=5, level=6),
    real(5, 4.5, 4.5, 4, 6, 6), cab(2), gate(15, 50), m("eq", low=3, hpf=40))
add("System of a Crown - Toxic Riffs", "Nu-Metal", "Inspired by Armenian-American alt-metal: mids present rather than scooped, no reverb, a fast gate, drop C.",
    real(5, 5, 5.5, 5.5, 5, 5), cab(1), gate(14, 60))
add("Kornn - Freak Riffs", "Nu-Metal", "Inspired by 7-string nu-metal pioneers: a Baby MT-2 stacked into the high-gain head, very low tuning, scooped and heavy.",
    m("mt2", dist=3, tone=4, level=4),
    real(5, 4.5, 6, 4, 5, 5), cab(0, thump=2), gate(15, 50), m("eq", lomid=-4, hpf=40))

# =============================================================================== AMBIENT & FX
add("Dotted Eighths", "Ambient & FX", "Inspired by a famous Irish delay-driven guitarist: a chimey AC amp into two delays - 350 ms (dotted eighth at ~129 BPM) and 525 ms - with only 1-2 repeats.",
    valve(3, 5, 4, 6, 7, 6), cab(3), gate(9, 200),
    delay(350, 18, 40, tone=4000, mod=2), delay(525, 10, 25, tone=3500, mod=1), verb(45, 15))
add("Ambient Swells", "Ambient & FX", "Volume-swell pad: compressor, chorus, long ping-pong delay and a huge reverb.",
    m("comp", thresh=-30, attack=20), m("tubeamp", channel=0, gain=5, treble=6, master=0), cab(4), gate(9, 300),
    m("chorus", rate=0.3, depth=6, mix=45), delay(620, 60, 40, mod=5, pingpong=1), verb(92, 55, damp=35, predelay=60))
add("Shoegaze Wash", "Ambient & FX", "The Little Big Fluff into chorus, long modulated delay and a big hall - a wall of sound.",
    m("muff", sustain=6, tone=3, level=0),
    valve(0, 5, 5, 5, 6, 5), cab(3), m("chorus", rate=0.4, depth=8, mix=50),
    delay(480, 55, 35, mod=6, pingpong=1), verb(95, 50, damp=30))
add("Slow Phase Clean", "Ambient & FX", "Clean amp with a slow, deep phaser and a spacious reverb.",
    valve(0, 5, 5, 5, 6, 5), cab(3), gate(9, 250),
    m("phaser", rate=0.2, depth=8, fb=40, stages=1, mix=50), verb(65, 25))

# =============================================================================== UTILITY
add("NAM Rig (load your captures)", "Utility", "Empty NAM Capture and IR Loader slots - load a .nam capture and a cabinet IR.",
    m("boost", True, gain=6), m("nam"), m("ir"), m("gate"), m("delay", True, mix=18), m("reverb", mix=15))
add("Empty Board", "Utility", "Nothing on the board - build your own rig.")

if __name__ == "__main__":
    keep = "--keep-levels" in sys.argv
    if keep and os.path.exists(OUT):
        old = {p["name"]: p for p in P.load(OUT)["presets"]}
        for p in presets:
            o = old.get(p["name"])
            if not o: continue
            amps_o = [x for x in o["chain"] if x["type"] in ("tubeamp", "realamp")]
            amps_n = [x for x in p["chain"] if x["type"] in ("tubeamp", "realamp")]
            if amps_o and amps_n and amps_o[-1]["type"] == amps_n[-1]["type"]:
                amps_n[-1]["params"]["master"] = amps_o[-1]["params"].get("master", 0)
    data = {"format": "toneical-factory", "version": 1,
            "categories": ["Clean", "Rhythm", "Lead", "Legends", "Nu-Metal", "Ambient & FX", "Utility"],
            "presets": presets}
    P.save(data, OUT)
    print(len(presets), "presets ->", OUT)
