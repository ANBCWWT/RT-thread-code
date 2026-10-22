#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Convert applications/*.wav into kitchen C arrays (auto-generated, do not edit by hand).

Usage: python gen_wav_c.py
Input : ../open.wav  ../close.wav   (relative to this script's dir -> applications/)
Output: open_wav.c/.h close_wav.c/.h (same format as button_wav.c)

To change prompt voices later: replace ../open.wav / ../close.wav, then rerun.
"""

HDR_C = """/* Auto-generated from applications/{wav} - DO NOT EDIT BY HAND */
#include <rtthread.h>

const rt_uint8_t {name}_data[] = {{
{body}
}};
const unsigned int {name}_len = {length};
"""

HDR_H = """/*
 * {wav} -> C array declaration (auto-generated; see gen_wav_c.py)
 */
#ifndef KITCHEN_{upper}_WAV_H
#define KITCHEN_{upper}_WAV_H

#include <rtthread.h>

#ifdef __cplusplus
extern "C" {{
#endif

extern const rt_uint8_t {name}_data[];
extern const unsigned int {name}_len;

#ifdef __cplusplus
}}
#endif

#endif /* KITCHEN_{upper}_WAV_H */
"""


def bin2c(data):
    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append('0x' + ',0x'.join('{:02x}'.format(b) for b in chunk) + ',')
    return '\n'.join(lines).rstrip(',')


def main():
    import os
    here = os.path.dirname(os.path.abspath(__file__))
    jobs = [('open.wav', 'open_wav', 'OPEN'),
            ('close.wav', 'close_wav', 'CLOSE'),
            ('button.wav', 'button_wav', 'BUTTON')]
    for wav, name, upper in jobs:
        src = os.path.normpath(os.path.join(here, '..', wav))
        if not os.path.exists(src):
            print('MISSING:', src)
            continue
        with open(src, 'rb') as f:
            data = f.read()
        body = bin2c(data)
        with open(os.path.join(here, name + '.c'), 'w', newline='\n') as f:
            f.write(HDR_C.format(wav=wav, name=name, body=body, length=len(data)))
        with open(os.path.join(here, name + '.h'), 'w', newline='\n') as f:
            f.write(HDR_H.format(wav=wav, upper=upper, name=name))
        print('generated', name + '.c/.h', len(data), 'bytes')


if __name__ == '__main__':
    main()
