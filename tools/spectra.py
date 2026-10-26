#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Overlay loudness-matched long-term spectra.  usage: spectra.py out.png label=file[:style] ..."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from analyze import read, ltas, loudness, fizz
import numpy as np
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
plt.figure(figsize=(11, 5))
for spec in sys.argv[2:]:
    lab, rest = spec.split('=', 1); path, _, style = rest.partition(':')
    sr, x = read(path); x = x * 10 ** ((-23 - loudness(x, sr)) / 20)
    f, p = ltas(x, sr)
    plt.semilogx(f[1:], p[1:], style or '-', label=f"{lab}  (fizz {fizz(x, sr):.1f} dB)")
plt.xlim(40, 20000); plt.ylim(-130, -50); plt.grid(alpha=.3, which='both')
plt.xlabel('Hz'); plt.ylabel('dB (loudness-matched)'); plt.legend(fontsize=8); plt.tight_layout()
plt.savefig(sys.argv[1], dpi=80)
