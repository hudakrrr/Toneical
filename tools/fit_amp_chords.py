#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Third-stage amp fit: on top of the level/distortion curves and the harmonic balance,
match how the real amp reacts to POWER CHORDS through the same cabinet IR:
pick-attack punch, palm-mute chug definition, soft/hard dynamics and intermodulation.
(Steady test tones can't tell preamp from power-amp distortion; chords can - power-amp
clipping flattens the pick attack.)

usage: fit_amp_chords.py RENDER LAB CH REF_STEPS.wav REF_CHORD_BASE IR.wav "FIXED" x0 width
  x0 = centreDb,powerDb,firstShare,bias,powerBias,sag,hpOct
"""
import os, subprocess, sys, tempfile
import numpy as np
from scipy.optimize import minimize
sys.path.insert(0, os.path.dirname(__file__))
from curves import curve, LEVELS
from analyze import read
from chord_metrics import metrics

render, lab, ch, ref_steps, ref_chords, ir, fixed = sys.argv[1:8]
ch = int(ch)
x0 = [float(v) for v in sys.argv[8].split(",")]
width = float(sys.argv[9])
SR, SEG = 48000, 24000
tmp = os.path.join(tempfile.gettempdir(), f"fitc_{ch}")
HP = {k: float(v) for k, v in (kv.split("=") for kv in fixed.split(",")) if k.startswith("hp")}

def harmonics(path):
    _, y = read(path); out = []
    for L in (-36, -24, -12):
        i = list(LEVELS).index(L); s = y[i * SEG + SEG // 4: i * SEG + 3 * SEG // 4]
        F = np.abs(np.fft.rfft(s * np.blackman(len(s)))) ** 2; fr = np.fft.rfftfreq(len(s), 1 / SR)
        h = [10 * np.log10(F[np.abs(fr - 220 * k) < 12].sum() + 1e-30) for k in range(1, 6)]
        out.append(np.maximum(np.array(h[1:]) - h[0], -60))
    return np.array(out)

ro, rt = curve(ref_steps); ro -= ro.max(); rt = np.maximum(rt, -70); rh = harmonics(ref_steps)
rm = np.array(metrics(ref_chords + "_chords.wav", ref_chords + "_e5.wav"))
base_fixed = ",".join(kv for kv in fixed.split(",") if not kv.startswith("hp"))

def override(p):
    c, pw, fs, b, pb, sg, hpo = p
    hp = ",".join(f"{k}={v * 2 ** hpo:.2f}" for k, v in HP.items())
    return (f"{ch}:{base_fixed},{hp},gain0Db={c - width/2:.3f},gain10Db={c + width/2:.3f},powerDb={pw:.3f},"
            f"firstShare={np.clip(fs, .1, .9):.3f},bias={np.clip(b, 0, .8):.3f},powerBias={np.clip(pb, -.6, .6):.3f},"
            f"sag={np.clip(sg, 0, 1):.3f}")

def run(src, dst, env, extra):
    subprocess.run([render, src, dst, "--module", "tubeamp", f"channel={ch}"] + extra, env=env, check=True, stderr=subprocess.DEVNULL)

FIX_HPO = os.environ.get("FIX_HPO")
def cost(p, verbose=False):
    if FIX_HPO is not None:
        p = list(p); p[6] = float(FIX_HPO)
    env = dict(os.environ, TONEICAL_AMP=override(p))
    run(os.path.join(lab, "steps.wav"), tmp + "_s.wav", env, [])
    run(os.path.join(lab, "chords.wav"), tmp + "_chords.wav", env, ["--ir", ir])
    run(os.path.join(lab, "e5.wav"), tmp + "_e5.wav", env, ["--ir", ir])
    o, t = curve(tmp + "_s.wav"); o -= o.max(); t = np.maximum(t, -70); h = harmonics(tmp + "_s.wav")
    m = np.array(metrics(tmp + "_chords.wav", tmp + "_e5.wav"))
    w = np.array([0.3, 4.0, 2.0, 4.0])       # imd, punch, dyn, chug
    e = np.mean((o - ro) ** 2) + 0.35 * np.mean((t - rt) ** 2) + 0.4 * np.mean((h - rh) ** 2) + np.sum(w * (m - rm) ** 2)
    if verbose: print("   metrics", np.round(m, 2), "target", np.round(rm, 2))
    return e

print(f"channel {ch}: start cost {cost(x0, True):.2f}", flush=True)
if int(os.environ.get("FIT_EVALS", 170)) == 0:
    print("OVERRIDE " + override(x0)); sys.exit(0)
simplex = np.vstack([x0, np.array(x0) + np.diag([4, 5, .15, .15, .15, .2, .7])])
best = minimize(cost, x0, method="Nelder-Mead", options=dict(maxfev=int(os.environ.get("FIT_EVALS", 170)), xatol=.01, fatol=.02, initial_simplex=simplex))
print(f"channel {ch}: cost {best.fun:.2f}")
cost(best.x, True)
bx = list(best.x)
if FIX_HPO is not None: bx[6] = float(FIX_HPO)
print("OVERRIDE " + override(bx))
