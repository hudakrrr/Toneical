#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Level-matches the factory presets: renders each one with a DI recording and adjusts
the amp Master in presets/factory.json so every preset hits the same loudness.
No rebuild needed - ToneicalRender reads the JSON through TONEICAL_FACTORY.

usage: level_presets.py RENDER_BINARY DI.wav [target_LUFS] [only-these-names,...]
"""
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(__file__))
from analyze import read, loudness
import presets_json as P

render, di = sys.argv[1], sys.argv[2]
target = float(sys.argv[3]) if len(sys.argv) > 3 else -16.0
only = set(sys.argv[4].split(",")) if len(sys.argv) > 4 else None
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "presets", "factory.json")
env = dict(os.environ, TONEICAL_FACTORY=path)
out = os.path.join(tempfile.gettempdir(), "lvl.wav")

def measure(name):
    r = subprocess.run([render, di, out, "--preset", name], env=env, stderr=subprocess.PIPE, text=True)
    if "PRESET ERROR" in r.stderr or r.returncode: print(r.stderr); sys.exit(1)
    sr, x = read(out)
    return loudness(x, sr)

for it in range(3):
    data = P.load(path)
    worst = 0.0
    print(f"pass {it + 1}")
    for p in data["presets"]:
        if only and p["name"] not in only: continue
        amps = [m for m in p["chain"] if m["type"] in ("tubeamp", "realamp")]
        if not amps: continue
        L = measure(p["name"])
        err = target - L
        worst = max(worst, abs(err))
        amp = amps[-1]["params"]
        amp["master"] = round(max(-30.0, min(12.0, amp.get("master", 0.0) + err)), 1)
        print(f"   {p['name']:38s} {L:6.1f} LUFS -> master {amp['master']:+.1f}")
        P.save(data, path)
    if worst < 0.3: break
