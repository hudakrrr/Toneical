#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""How 'alive' is a distorted sound? Compares renders of the same DI performance
(e.g. a real-amp NAM capture vs Toneical) on how the OUTPUT follows the PLAYING:

  slope   - d(output level)/d(input level) over 30 ms windows while notes ring
            (1 = follows your picking exactly, 0 = brick-wall compressed)
  spread  - 10..90 % range of the short-term output level (dB) while playing
  attack  - output peak in the first 25 ms of each pick vs the level 80-200 ms later (dB)
  decay   - how fast notes fade (dB/s) at the output vs at the input, as a ratio

usage: dynamics_compare.py DI.wav  NAME=render.wav [NAME=render.wav ...]
"""
import sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from analyze import read

def env_db(x, sr, win=0.03):
    n = int(sr * win); m = len(x) // n
    e = np.sqrt(np.mean(x[: m * n].reshape(m, n) ** 2, axis=1) + 1e-12)
    return 20 * np.log10(e)

def onsets(xdb, hop, thresh=9.0):
    # rises of > thresh dB within 2 windows, at least 150 ms apart
    o = []; last = -1e9
    for i in range(2, len(xdb)):
        if xdb[i] - min(xdb[i - 2], xdb[i - 1]) > thresh and xdb[i] > xdb.max() - 40 and (i - last) * hop > 0.15:
            o.append(i); last = i
    return o

def analyse(di, y, sr):
    hop = 0.01
    a = env_db(di, sr, hop); b = env_db(y, sr, hop)
    k = min(len(a), len(b)); a, b = a[:k], b[:k]
    playing = a > a.max() - 35
    # slope: regression of 30 ms output level on input level, local (detrended per note) to avoid
    # mixing different notes - use differences between consecutive windows 30 ms apart
    da = a[3:] - a[:-3]; db = b[3:] - b[:-3]
    m = playing[3:] & playing[:-3] & (np.abs(da) > 0.5)
    slope = np.polyfit(da[m], db[m], 1)[0] if m.sum() > 20 else np.nan
    spread = np.percentile(b[playing], 90) - np.percentile(b[playing], 10)
    on = onsets(a, hop)
    att = []; dec_in = []; dec_out = []
    for i in on:
        if i + 20 >= k: continue
        pk = b[i:i + 3].max(); later = b[i + 8:i + 20].mean()
        att.append(pk - later)
        # decay from 100 ms to 400 ms after the pick (if no new pick)
        if i + 40 < k and not any(i < j < i + 40 for j in on):
            dec_in.append((a[i + 40] - a[i + 10]) / 0.3); dec_out.append((b[i + 40] - b[i + 10]) / 0.3)
    dr = np.median(np.array(dec_out)) / np.median(np.array(dec_in)) if dec_in else np.nan
    return slope, spread, np.median(att) if att else np.nan, dr, len(on)

di_path = sys.argv[1]
sr, di = read(di_path); di = di if di.ndim == 1 else di[:, 0]
print(f"{'':28s} {'slope':>6s} {'spread':>7s} {'attack':>7s} {'decay':>6s}  picks")
for arg in sys.argv[2:]:
    name, path = arg.split("=", 1)
    _, y = read(path); y = y if y.ndim == 1 else y.mean(axis=1)
    s, sp, at, dr, n = analyse(di, y, sr)
    print(f"{name:28s} {s:6.2f} {sp:7.1f} {at:7.1f} {dr:6.2f}  {n}")
