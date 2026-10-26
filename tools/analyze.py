#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Measurement helpers for tuning Toneical's DSP (used with ToneicalRender).

loudness(x, sr)  -> integrated loudness in LUFS (ITU-R BS.1770-4, K-weighted, gated)
ltas(x, sr)      -> (freqs, dB) long-term average spectrum, 1/6-octave smoothed
fizz(x, sr)      -> dB of energy above 6 kHz relative to 150 Hz - 5 kHz
"""
import numpy as np
import scipy.signal as sig
import scipy.io.wavfile as wav


def read(path):
    sr, x = wav.read(path)
    x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return sr, x


def write(path, sr, x):
    wav.write(path, sr, np.asarray(x, dtype=np.float32))


def db(v):
    return 20 * np.log10(np.maximum(v, 1e-12))


def rms_db(x):
    return db(np.sqrt(np.mean(np.square(x))))


def _kweight(x, sr):
    # BS.1770 pre-filter (high shelf) + RLB high-pass, designed via bilinear transform
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = np.tan(np.pi * f0 / sr); Vh = 10 ** (G / 20); Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = np.tan(np.pi * f0 / sr)
    a2 = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    b2 = [1, -2, 1]
    return sig.lfilter(b2, a2, sig.lfilter(b1, a1, x))


def loudness(x, sr):
    y = _kweight(x, sr)
    n, hop = int(0.4 * sr), int(0.1 * sr)
    if len(y) < n:
        return -70.0
    ms = np.array([np.mean(y[i:i + n] ** 2) for i in range(0, len(y) - n, hop)])
    lk = -0.691 + 10 * np.log10(np.maximum(ms, 1e-20))
    ms = ms[lk > -70]
    if len(ms) == 0:
        return -70.0
    rel = -0.691 + 10 * np.log10(ms.mean()) - 10
    lk = -0.691 + 10 * np.log10(ms)
    ms = ms[lk > rel]
    return float(-0.691 + 10 * np.log10(ms.mean()))


def ltas(x, sr, smooth_oct=1 / 6):
    f, p = sig.welch(x, sr, nperseg=8192)
    out = np.empty_like(p)
    for i, fc in enumerate(f):
        lo, hi = fc * 2 ** (-smooth_oct / 2), fc * 2 ** (smooth_oct / 2)
        m = (f >= lo) & (f <= hi)
        out[i] = p[m].mean() if m.any() else p[i]
    return f, 10 * np.log10(np.maximum(out, 1e-20))


def band_energy(x, sr, lo, hi):
    f, p = sig.welch(x, sr, nperseg=8192)
    m = (f >= lo) & (f < hi)
    return p[m].sum()


def fizz(x, sr):
    return 10 * np.log10(band_energy(x, sr, 6000, 20000) / band_energy(x, sr, 150, 5000))


def summary(name, x, sr):
    return (f"{name:34s} LUFS {loudness(x, sr):6.1f}   peak {db(np.abs(x).max()):6.1f} dBFS"
            f"   fizz {fizz(x, sr):6.1f} dB")
