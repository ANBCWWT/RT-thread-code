#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Subset xiaozhi_font.c (embedded TTF) to characters actually used by the
kitchen UI, then regenerate xiaozhi_font.c in place.

Usage: python subset_font.py
- reads   xiaozhi_font.c
- writes  xiaozhi_font_full.ttf   (original font, backup)
- writes  xiaozhi_font_subset.ttf (subset font, for inspection)
- writes  xiaozhi_font.c          (regenerated array, same symbol names)

Code (kitchen_ui.c) is unchanged: it still uses lv_tiny_ttf_create_data(
xiaozhi_font, xiaozhi_font_size, size) with arbitrary pixel sizes.
"""
import os, re, io, glob
from fontTools.ttLib import TTFont
from fontTools.subset import Subsetter, Options

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, 'xiaozhi_font.c')


def parse_c_array(path):
    txt = io.open(path, encoding='utf-8', errors='ignore').read()
    m = re.search(r'const\s+unsigned\s+char\s+xiaozhi_font\[\d*\]\s*=\s*\{(.*?)\};', txt, re.S)
    assert m, 'xiaozhi_font array not found in ' + path
    body = m.group(1)
    nums = [int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})', body)]
    return bytes(nums)


def build_charset():
    chars = set()
    # all visible ASCII (digits, units, latin)
    chars.update(chr(c) for c in range(0x20, 0x7F))
    # every string literal in the kitchen app + main.c (CJK and symbols)
    root = os.path.normpath(os.path.join(HERE, '..', '..', 'kitchen'))
    main_c = os.path.normpath(os.path.join(HERE, '..', '..', '..', 'main.c'))
    for pat in [os.path.join(root, '**', '*.c'),
                os.path.join(root, '**', '*.h'),
                main_c]:
        for p in glob.glob(pat, recursive=True):
            try:
                s = io.open(p, encoding='utf-8', errors='ignore').read()
            except Exception:
                continue
            for m in re.findall(r'"([^"]*)"', s):
                chars.update(m)
    # common CJK punctuation & units (keep generous, cheap to include)
    chars.update('℃，。！？、；：""\'\'（）《》【】…—～·°％年月日时分秒％×÷＋－=<>#@&*_^|\\~`')
    chars.discard('\n')
    chars.discard('\t')
    chars.discard('\r')
    return chars


def bin2c(data):
    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        lines.append('\t' + ', '.join('0x{:02x}'.format(b) for b in chunk) + ',')
    return '\n'.join(lines)


def main():
    full = parse_c_array(SRC)
    with open(os.path.join(HERE, 'xiaozhi_font_full.ttf'), 'wb') as f:
        f.write(full)

    charset = build_charset()
    font = TTFont(io.BytesIO(full))
    opts = Options()
    opts.hinting = False          # stb_truetype ignores hinting; smaller output
    opts.layout_features = []     # no GSUB/GPOS needed for flat text
    opts.name_IDs = [1, 2, 3, 4, 6]
    opts.recalc_bounds = True
    ss = Subsetter(options=opts)
    ss.populate(text=''.join(sorted(charset)))
    ss.subset(font)
    buf = io.BytesIO()
    font.save(buf)
    subset = buf.getvalue()
    with open(os.path.join(HERE, 'xiaozhi_font_subset.ttf'), 'wb') as f:
        f.write(subset)

    # verify required glyphs present
    t2 = TTFont(io.BytesIO(subset))
    cmap = t2.getBestCmap()
    missing = [ch for ch in '厨房灯油烟机洗碗机烤箱温湿度已开关℃%0123456789.' if ord(ch) not in cmap]
    if missing:
        print('WARNING missing glyphs:', missing)
    else:
        print('all key glyphs present')

    c = ('/* Auto-generated from xiaozhi_font_full.ttf (subset to kitchen UI chars) - DO NOT EDIT BY HAND */\n'
         'const unsigned char xiaozhi_font[{}] = {{\n{}\n}};\n\n\n'
         'const int xiaozhi_font_size = sizeof(xiaozhi_font);\n').format(len(subset), bin2c(subset))
    with open(SRC, 'w', newline='\n', encoding='utf-8') as f:
        f.write(c)

    print('charset      :', len(charset), 'unique chars')
    print('original     :', len(full), 'bytes')
    print('subset       :', len(subset), 'bytes  (%.1f%% reduction)' % (100 - 100.0 * len(subset) / len(full)))
    print('xiaozhi_font.c regenerated OK')


if __name__ == '__main__':
    main()
