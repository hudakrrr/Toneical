#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Fits a Valve Head channel voicing to a real-amp NAM capture.

Matches three things at once:
  1. output level vs input level   (220 Hz sine stepped -60..0 dBFS)  -> touch sensitivity
  2. harmonic distortion vs level                                       -> how it breaks up
  3. long-term spectrum on a real DI guitar recording (loudness-matched) -> tone

usage: fit_amp.py RENDER_BIN LAB_DIR CH REF_STEPS.wav REF_DI.wav [width_dB]
Prints the TONEICAL_AMP override string and the C++ table values.
"""
import os, subprocess, sys, tempfile
import numpy as np
from scipy.optimize import minimize
sys.path.insert(0, os.path.dirname(__file__))
from curves import curve
from analyze import read, ltas, loudness

render, lab, ch, ref_steps, ref_di = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4], sys.argv[5]
width = float(sys.argv[6]) if len(sys.argv) > 6 else 30.0
DI = os.path.join(lab, "di10.wav")

# defaults mirrored from TubeAmp::defaultVoice
HP = {0: [30, 40, 40, 40], 1: [25, 90, 140, 140], 2: [40, 180, 260, 320], 3: [30, 70, 90, 90]}[ch]
LP = {0: [14000, 11000, 9000, 9000], 1: [12000, 8000, 6500, 6500], 2: [10000, 7000, 5500, 5000], 3: [14000, 10000, 9000, 9000]}[ch]

def spectrum(path):
    sr, x = read(path)
    x = x * 10 ** ((-23 - loudness(x, sr)) / 20)
    f, p = ltas(x, sr)
    grid = np.geomspace(70, 12000, 48)
    return np.interp(grid, f, p)

ro, rt = curve(ref_steps); ro -= ro.max(); rt = np.maximum(rt, -70)
rs = spectrum(ref_di)
tmp = os.path.join(tempfile.gettempdir(), f"fit_{ch}")
NAMES = ["centreDb", "powerDb", "firstShare", "bias", "lpOct", "hpOct", "postLpOct", "brightDb"]

def params(p):
    c, pw, fs, b, lpo, hpo, plo, br = p
    d = dict(gain0Db=c - width / 2, gain10Db=c + width / 2, powerDb=pw,
             firstShare=float(np.clip(fs, .1, .9)), bias=float(np.clip(b, 0, .6)),
             postLp=float(np.clip(9000 * 2 ** plo, 2500, 20000)), brightDb=float(np.clip(br, -3, 9)))
    for i in range(4):
        d[f"lp{i}"] = float(np.clip(LP[i] * 2 ** lpo, 1500, 22000))
        d[f"hp{i}"] = float(np.clip(HP[i] * 2 ** hpo, 10, 900))
    return d

def spec(p):
    return f"{ch}:" + ",".join(f"{k}={v:.3f}" for k, v in params(p).items())

def run(src, dst, p):
    env = dict(os.environ, TONEICAL_AMP=spec(p))
    subprocess.run([render, src, dst, "--module", "tubeamp", f"channel={ch}"], env=env, check=True, stderr=subprocess.DEVNULL)

def cost(p):
    run(os.path.join(lab, "steps.wav"), tmp + "_s.wav", p)
    run(DI, tmp + "_d.wav", p)
    o, t = curve(tmp + "_s.wav"); o -= o.max(); t = np.maximum(t, -70)
    s = spectrum(tmp + "_d.wav")
    ds = s - rs; ds -= ds.mean()
    return np.mean((o - ro) ** 2) + 0.35 * np.mean((t - rt) ** 2) + 0.5 * np.mean(ds ** 2)

x0 = [float(v) for v in os.environ.get("FIT_START", "").split(",")] if os.environ.get("FIT_START") else \
     {0: [-14.5, -6.8, .74, .14], 1: [32.8, -5.5, .1, .6], 2: [43.3, 9.6, .29, 0], 3: [37.2, 1.1, .23, .3]}[ch] + [0.0, 0.0, 0.0, 2.0]
simplex = np.vstack([x0, np.array(x0) + np.diag([4, 3, .12, .1, .5, .7, .5, 3])])
best = minimize(cost, x0, method="Nelder-Mead",
                options=dict(maxfev=int(os.environ.get("FIT_EVALS", 200)), xatol=0.02, fatol=0.02, initial_simplex=simplex))
print(f"channel {ch}: cost {best.fun:.2f}")
print("OVERRIDE " + spec(best.x))
print("PARAMS " + ", ".join(f"{n}={v:.3f}" for n, v in zip(NAMES, best.x)))
