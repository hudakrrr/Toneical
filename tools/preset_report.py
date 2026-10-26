#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Renders presets with real playing (DI), power chords and chugs and prints how 'alive'
they are next to real amp captures: loudness, pick-attack punch, soft/hard dynamics,
chug definition, intermodulation, crest factor and fizz.

usage: preset_report.py RENDER LAB_DIR [name-substring ...]
"""
import os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from analyze import read, loudness
from chord_metrics import metrics
import presets_json as P

render, lab = sys.argv[1], sys.argv[2]
want = sys.argv[3:]
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "presets", "factory.json")
env = dict(os.environ, TONEICAL_FACTORY=path)
tmp = tempfile.gettempdir()

def fizz(x, sr):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / sr)
    return 10 * np.log10(X[(f > 5000) & (f < 10000)].sum() / X[(f > 200) & (f < 2000)].sum())

def crest(x):
    return 20 * np.log10(np.abs(x).max() / (np.sqrt(np.mean(x ** 2)) + 1e-12))

print(f"{'preset':40s} {'LUFS':>6s} {'imd':>6s} {'punch':>6s} {'dyn':>6s} {'chug':>6s} {'crest':>6s} {'fizz':>6s}")
refs = [("REAL JCM800 crunch", "pc/real_jcmcr"), ("REAL JCM800 lead", "pc/real_jcmld"),
        ("REAL 5150 crunch", "pc/real_5150cr"), ("REAL 5150 lead", "pc/real_5150ld")]
for name, base in refs:
    if not os.path.exists(os.path.join(lab, base + "_chords.wav")): continue
    m = metrics(os.path.join(lab, base + "_chords.wav"), os.path.join(lab, base + "_e5.wav"))
    sr, x = read(os.path.join(lab, base + "_chords.wav"))
    print(f"{name:40s} {'':6s} " + " ".join(f"{v:6.2f}" for v in m) + f" {crest(x):6.1f} {fizz(x, sr):6.1f}")

for p in P.load(path)["presets"]:
    if want and not any(w.lower() in p["name"].lower() for w in want): continue
    if not any(m["type"] in ("tubeamp", "realamp") for m in p["chain"]): continue
    outs = {}
    for src in ("di", "chords", "e5"):
        o = os.path.join(tmp, f"rep_{src}.wav")
        subprocess.run([render, os.path.join(lab, src + ".wav"), o, "--preset", p["name"]], env=env,
                       check=True, stderr=subprocess.DEVNULL)
        outs[src] = o
    sr, x = read(outs["di"])
    m = metrics(outs["chords"], outs["e5"])
    _, c = read(outs["chords"])
    print(f"{p['name'][:40]:40s} {loudness(x, sr):6.1f} " + " ".join(f"{v:6.2f}" for v in m) + f" {crest(c):6.1f} {fizz(c, sr):6.1f}", flush=True)
