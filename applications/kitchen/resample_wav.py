#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Resample prompt wavs to 16kHz mono to keep firmware small.

Usage: python resample_wav.py
Input : ../open.wav ../close.wav (44.1k stereo TTS output, overwritten in place)
Output: same files as 16kHz / 16bit / mono PCM wav.

Rationale: embedded as C arrays into firmware; 16k mono is ~1/5 the size,
and wavplayer (ES8388) supports 8k-48k, so playback is unaffected.
"""
import wave
import array
import os


def resample(wav_in, out_rate=16000):
    w = wave.open(wav_in, 'rb')
    ch = w.getnchannels()
    sw = w.getsampwidth()
    rate = w.getframerate()
    frames = w.readframes(w.getnframes())
    w.close()

    if sw != 2:
        raise ValueError('%s: expected 16bit PCM, got %d bytes' % (wav_in, sw))

    a = array.array('h')
    a.frombytes(frames)

    if ch == 2:
        mono = [(a[i] + a[i + 1]) // 2 for i in range(0, len(a) - 1, 2)]
    else:
        mono = list(a)

    n_out = int(len(mono) * out_rate / rate)
    out = array.array('h')
    for i in range(n_out):
        pos = i * rate / out_rate
        i0 = int(pos)
        frac = pos - i0
        i1 = i0 + 1 if i0 + 1 < len(mono) else i0
        v = mono[i0] * (1 - frac) + mono[i1] * frac
        out.append(int(v))

    tmp = wav_in + '.tmp'
    w2 = wave.open(tmp, 'wb')
    w2.setnchannels(1)
    w2.setsampwidth(2)
    w2.setframerate(out_rate)
    w2.writeframes(out.tobytes())
    w2.close()
    os.replace(tmp, wav_in)
    print('resampled %s -> %dHz mono, %d bytes' % (wav_in, out_rate, len(out) * 2))


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    for wav in ('open.wav', 'close.wav'):
        src = os.path.normpath(os.path.join(here, '..', wav))
        if not os.path.exists(src):
            print('MISSING:', src)
            continue
        resample(src)


if __name__ == '__main__':
    main()
