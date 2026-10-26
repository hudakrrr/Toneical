#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Second-stage amp fit: keeps a channel's tone filters fixed and adjusts gain staging and
asymmetry so that, besides the level/distortion curves, the individual harmonics (H2..H5)
match the real amp at soft, medium and hard picking. Even harmonics (H2, H4) are what
make distortion sound warm and "singing" rather than buzzy.

usage: fit_amp_harm.py RENDER LAB CH REF_STEPS.wav "FIXED_OVERRIDES" x0(centreDb,powerDb,firstShare,bias,powerBias) width
"""
import os, subprocess, sys, tempfile
import numpy as np
from scipy.optimize import minimize
sys.path.insert(0, os.path.dirname(__file__))
from curves import curve, LEVELS
from analyze import read

render, lab, ch, ref, fixed = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
x0 = [float(v) for v in sys.argv[6].split(",")]
width = float(sys.argv[7])
SR, SEG = 48000, 24000
tmp = os.path.join(tempfile.gettempdir(), f"fith_{ch}.wav")

def harmonics(path):
    _, y = read(path); out = []
    for L in (-36, -24, -12):
        i = list(LEVELS).index(L); s = y[i * SEG + SEG // 4: i * SEG + 3 * SEG // 4]
        F = np.abs(np.fft.rfft(s * np.blackman(len(s)))) ** 2; fr = np.fft.rfftfreq(len(s), 1 / SR)
        h = [10 * np.log10(F[np.abs(fr - 220 * k) < 12].sum() + 1e-30) for k in range(1, 6)]
        out.append(np.maximum(np.array(h[1:]) - h[0], -60))
    return np.array(out)

ro, rt = curve(ref); ro -= ro.max(); rt = np.maximum(rt, -70); rh = harmonics(ref)

def override(p):
    c, pw, fs, b, pb = p
    return (f"{ch}:{fixed},gain0Db={c - width/2:.3f},gain10Db={c + width/2:.3f},powerDb={pw:.3f},"
            f"firstShare={np.clip(fs, .1, .9):.3f},bias={np.clip(b, 0, .8):.3f},powerBias={np.clip(pb, -.6, .6):.3f}")

def cost(p):
    env = dict(os.environ, TONEICAL_AMP=override(p))
    subprocess.run([render, os.path.join(lab, "steps.wav"), tmp, "--module", "tubeamp", f"channel={ch}"],
                   env=env, check=True, stderr=subprocess.DEVNULL)
    o, t = curve(tmp); o -= o.max(); t = np.maximum(t, -70); h = harmonics(tmp)
    return np.mean((o - ro) ** 2) + 0.35 * np.mean((t - rt) ** 2) + 0.4 * np.mean((h - rh) ** 2)

print(f"channel {ch}: start cost {cost(x0):.2f}", flush=True)
simplex = np.vstack([x0, np.array(x0) + np.diag([4, 4, .15, .15, .15])])
best = minimize(cost, x0, method="Nelder-Mead", options=dict(maxfev=int(os.environ.get("FIT_EVALS", 160)), xatol=.01, fatol=.02, initial_simplex=simplex))
print(f"channel {ch}: cost {best.fun:.2f}")
print("OVERRIDE " + override(best.x))
h = harmonics(tmp)
