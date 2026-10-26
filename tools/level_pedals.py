#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Unity level for the drive pedals: Level 0 dB at the default settings should be as loud
as bypass. Prints trimDb (and, for captures without a Drive input, compPerDb = how much
louder each extra dB of drive makes it) for the table in Source/modules/CapturedPedals.cpp.

usage: level_pedals.py RENDER DI.wav
"""
import os, subprocess, sys, tempfile
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from analyze import read, loudness

render, di = sys.argv[1], sys.argv[2]
out = os.path.join(tempfile.gettempdir(), "lp.wav")
PEDALS = {  # typeId: (drive param, default, dbPerStep or None for learned-knob captures, variants)
    "ts808": ("drive", 3, None, 1), "klon": ("gain", 5, 1.5, 1), "sd1": ("drive", 5, 1.5, 1),
    "ds1": ("dist", 6, 1.5, 1), "os2": ("drive", 5, 1.5, 1), "mt2": ("dist", 5, 1.2, 1),
    "hm2": ("dist", 5, 1.5, 4), "rat": ("dist", 5, 1.2, 1), "muff": ("sustain", 5, 1.2, 1),
    "mxr78": ("dist", 5, None, 1), "m77": ("gain", 2, 1.2, 2), "bb": ("gain", 5, None, 1),
    "protein": ("drive", 5, None, 2), "precision": ("drive", 0, 1.2, 1), "grind": ("grind", 5, 1.2, 1),
    "sd805": ("gain", 5, 1.5, 1),
}

def lufs(args):
    subprocess.run([render, di, out] + args, check=True, stderr=subprocess.DEVNULL)
    sr, x = read(out)
    return loudness(x, sr)

ref = lufs(["--gain", "0"])
print(f"bypass {ref:.1f} LUFS")
for t, (p, dflt, step, nv) in PEDALS.items():
    trims = []
    for v in range(nv):
        extra = [f"setting={v}"] if nv > 1 else []
        L = lufs(["--module", t] + extra)
        trims.append(ref - L)
        if v == 0 and step:
            lo = lufs(["--module", t, f"{p}={max(0, dflt - 3)}"] + extra)
            hi = lufs(["--module", t, f"{p}={min(10, dflt + 3)}"] + extra)
            slope = (hi - lo) / ((min(10, dflt + 3) - max(0, dflt - 3)) * step)
        elif v == 0:
            lo = lufs(["--module", t, f"{p}=0"]); hi = lufs(["--module", t, f"{p}=10"])
            slope = None
    s = f"comp {slope:.2f}" if slope is not None else f"drive 0..10 changes loudness {lo - L:+.1f} .. {hi - L:+.1f} dB"
    print(f"{t:10s} trim " + " / ".join(f"{x:+.1f}" for x in trims) + f"   {s}")
