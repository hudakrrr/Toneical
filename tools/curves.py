#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Plots output level and harmonic distortion vs input level for rendered 'steps.wav' files.
usage: curves.py out.png label=file.wav[:style] ..."""
import sys, numpy as np
sys.path.insert(0, __import__('os').path.dirname(__file__))
from analyze import read, rms_db
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt

SR, SEG = 48000, 24000
LEVELS = np.arange(-60, 1, 3)

def curve(path):
    _, y = read(path)
    out, thd = [], []
    for k in range(len(LEVELS)):
        s = y[k * SEG + SEG // 4: k * SEG + 3 * SEG // 4]
        out.append(rms_db(s))
        F = np.abs(np.fft.rfft(s * np.blackman(len(s)))) ** 2
        fr = np.fft.rfftfreq(len(s), 1 / SR)
        fund = F[(fr > 205) & (fr < 235)].sum()
        harm = F[(fr > 300) & (fr < 20000)].sum()
        thd.append(10 * np.log10(harm / fund + 1e-12))
    return np.array(out), np.array(thd)

if __name__ == '__main__':
    fig, ax = plt.subplots(1, 2, figsize=(14, 5.5))
    for spec in sys.argv[2:]:
        lab, rest = spec.split('=', 1)
        path, _, style = rest.partition(':')
        o, t = curve(path); o -= o.max()
        ax[0].plot(LEVELS, o, style or '-', label=lab); ax[1].plot(LEVELS, t, style or '-', label=lab)
        i = lambda L: list(LEVELS).index(L)
        print(f"{lab:26s} dynamic range {o.max()-o.min():5.1f} dB | THD dB @-45 {t[i(-45)]:6.1f} @-30 {t[i(-30)]:6.1f} @-18 {t[i(-18)]:6.1f} @-9 {t[i(-9)]:6.1f}")
    ax[0].set_title('Output vs input level (0 = loudest)'); ax[0].set_xlabel('input dBFS (220 Hz sine)'); ax[0].grid(alpha=.3); ax[0].legend(fontsize=8)
    ax[1].set_title('Harmonics relative to fundamental'); ax[1].set_xlabel('input dBFS'); ax[1].set_ylabel('dB'); ax[1].grid(alpha=.3)
    plt.tight_layout(); plt.savefig(sys.argv[1], dpi=80)
