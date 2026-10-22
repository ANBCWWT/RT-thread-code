#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Resample applications/button.wav 44.1kHz stereo -> 24kHz stereo (anti-alias FFT).
Keeps WAV PCM header (44 bytes). Run from this dir: python resample_button_24k.py
"""
import os, wave, numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.normpath(os.path.join(HERE, '..', 'button.wav'))
FS_IN, FS_OUT = 44100, 24000

def fft_resample(x, fs_in, fs_out):
    n = len(x)
    m = int(round(n * fs_out / fs_in))
    X = np.fft.rfft(x)
    f = np.fft.rfftfreq(n, 1.0 / fs_in)
    cutoff = min(fs_in, fs_out) / 2 * 0.92   # anti-alias low-pass
    X[f > cutoff] = 0
    k = min(len(X), m // 2 + 1)
    Y = np.zeros(m // 2 + 1, dtype=complex)
    Y[:k] = X[:k] * (m / n)
    return np.fft.irfft(Y, m)

def read_wav(p):
    with wave.open(p, 'rb') as w:
        assert w.getsampwidth() == 2 and w.getnchannels() == 2
        ch = w.getnchannels()
        raw = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
    return raw.reshape(-1, ch), w.getframerate(), w.getparams()

def write_wav(p, data, params):
    with wave.open(p, 'wb') as w:
        w.setnchannels(params.nchannels)
        w.setsampwidth(params.sampwidth)
        w.setframerate(FS_OUT)
        w.writeframes(data.astype(np.int16).tobytes())

raw, rate, params = read_wav(SRC)
print('input : {} Hz {}ch {} frames {} bytes'.format(rate, params.nchannels, len(raw), raw.nbytes))
left = fft_resample(raw[:, 0].astype(np.float64), rate, FS_OUT)
right = fft_resample(raw[:, 1].astype(np.float64), rate, FS_OUT)
out = np.stack([left, right], axis=1)
# clip to int16 range
out = np.clip(out, -32768, 32767)
write_wav(SRC, out, params)
print('output: {} Hz {}ch {} frames {} bytes'.format(FS_OUT, params.nchannels, len(out), out.nbytes))
