#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Power-chord metrics for chords.wav / e5.wav renders (see tools/README):
  imd    - intermodulation 'mud' on an E5 chord: energy at non-harmonic products (k*41.2 Hz, k coprime to 6) vs harmonics, dB
  punch  - pick attack: peak of the first 40 ms vs the level 250-500 ms later, dB (bigger = more alive)
  dyn    - loudness difference between soft and hard strums, dB (bigger = more touch-sensitive)
  chug   - palm-mute definition: dip between chugs vs chug peaks, dB (more negative = tighter)"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from analyze import read, loudness, db

SR = 48000
CH = 1.6; MU = 0.25
def half_starts(h): return h * (4 * CH + 6 * MU + 0.4)

def env(x, ms):
    n = max(1, int(ms * SR / 1000)); m = len(x) // n * n
    return np.sqrt((x[:m].reshape(-1, n) ** 2).mean(1))

def metrics(chords_path, e5_path):
    _, x = read(chords_path); _, e = read(e5_path)
    # imd
    s = e[int(0.2 * SR):int(1.2 * SR)]
    F = np.abs(np.fft.rfft(s * np.blackman(len(s)))) ** 2; fr = np.fft.rfftfreq(len(s), 1 / SR)
    base = 82.41 / 2; harm = 0; imd = 0
    for k in range(1, int(4000 / base)):
        band = F[np.abs(fr - k * base) < 4].sum()
        if k % 2 == 0 or k % 3 == 0: harm += band
        else: imd += band
    imd_db = 10 * np.log10(imd / harm)
    # punch on the 4 hard chords
    p = []
    for c in range(4):
        t0 = half_starts(1) + c * CH
        seg = x[int(t0 * SR):int((t0 + 0.6) * SR)]
        e5 = env(seg, 5)
        p.append(db(e5[:8].max()) - db(e5[50:100].mean()))
    # dynamics soft vs hard (chord sections)
    soft = x[int(0):int(4 * CH * SR)]; hard = x[int(half_starts(1) * SR):int((half_starts(1) + 4 * CH) * SR)]
    dyn = loudness(hard, SR) - loudness(soft, SR)
    # chugs (hard)
    t0 = half_starts(1) + 4 * CH
    seg = x[int(t0 * SR):int((t0 + 6 * MU) * SR)]; e2 = env(seg, 5)
    per = int(MU * 1000 / 5); dips = []
    for k in range(6):
        w = e2[k * per:(k + 1) * per]
        dips.append(db(w[-6:].min()) - db(w[:10].max()))
    return imd_db, float(np.mean(p)), dyn, float(np.mean(dips))

if __name__ == "__main__":
    print(f"{'':30s} {'imd':>7s} {'punch':>7s} {'dyn':>7s} {'chug':>7s}")
    for spec in sys.argv[1:]:
        lab, base = spec.split("=")
        m = metrics(base + "_chords.wav", base + "_e5.wav")
        print(f"{lab:30s} " + " ".join(f"{v:7.1f}" for v in m))
