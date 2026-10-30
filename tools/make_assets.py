#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""ZINZIN: the pictures as C tables for the FM-1.

Every picture is 16 colours (4 bits a pixel, two pixels a byte, the left one in the high nibble) with its own
RGB565 palette; colour 0 is transparent for stickers and sprites. Clips (the cockatoo, Newton's cradle) are
frames of one size with one palette. The tables are const, so they stay in flash.

    python3 tools/make_assets.py            -> zinzin/gen/zz_assets.h
"""
import json
import os
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PNG = os.path.join(ROOT, "assets", "png")
OUT = os.path.join(ROOT, "zinzin", "gen", "zz_assets.h")


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pack4(idx, w, h):
    out = bytearray()
    for y in range(h):
        for x in range(0, w, 2):
            a = idx[y * w + x]
            b = idx[y * w + x + 1] if x + 1 < w else 0
            out.append(((a & 15) << 4) | (b & 15))
    return out


def from_png(name, frames=1, fw=None, transparent=False):
    im = Image.open(os.path.join(PNG, name + ".png"))
    if im.mode == "RGBA":                       # the horse: a silhouette
        a = im.getchannel("A")
        idx = [1 if v > 127 else 0 for v in a.getdata()]
        pal = [0, rgb565(20, 10, 10)] + [0] * 14
        w, h = im.size
        return {"w": w, "h": h, "n": 1, "pal": pal, "px": pack4(idx, w, h), "t": 1}
    assert im.mode == "P", name
    pal_raw = im.getpalette()[:48]
    pal_raw += [0] * (48 - len(pal_raw))
    pal = [rgb565(pal_raw[3 * i], pal_raw[3 * i + 1], pal_raw[3 * i + 2]) for i in range(16)]
    idx = list(im.getdata())
    assert max(idx) < 16, name
    w, h = im.size
    fw = fw or w
    data = bytearray()
    for f in range(frames):
        sub = [idx[y * w + f * fw + x] for y in range(h) for x in range(fw)]
        data += pack4(sub, fw, h)
    return {"w": fw, "h": h, "n": frames, "pal": pal, "px": data, "t": 1 if transparent else 0}


# the pixel sprites, as in the browser proto: ' ' is transparent, each letter a colour of the sprite's own palette
SPRITES = {
    "heart": ({"k": "#5a0f33", "a": "#ff4fa3", "b": "#ffe0ef", "c": "#c22d77"}, [" kkk   kkk ", "kabbk kaaak", "kabaakaaaak", "kaaaaaaaack", "kaaaaaaaack", " kaaaaaack ", "  kaaacck  ", "   kaack   ", "    kck    ", "     k     "]),
    "star": ({"o": "#ff9ad5", "y": "#fff3a0", "w": "#ffffff"}, ["    o    ", "    y    ", "   oyo   ", "  oywyo  ", "oyywwwyyo", "  oywyo  ", "   oyo   ", "    y    ", "    o    "]),
    "smiley": ({"k": "#3b2a00", "y": "#ffe14d", "p": "#ff8fc8", "w": "#fff9d6"}, ["  kkkkkk  ", " kywyyyyk ", "kywyyyyyyk", "kyykyykyyk", "kyykyykyyk", "kpyyyyyypk", "kyykyykyyk", "kyyykkyyyk", " kyyyyyyk ", "  kkkkkk  "]),
    "ghost": ({"k": "#3a2b5c", "w": "#f6f2ff", "b": "#1d1730", "p": "#ff9ad5"}, ["   kkkkk   ", "  kwwwwwk  ", " kwwwwwwwk ", "kwwbwwwbwwk", "kwwbwwwbwwk", "kwpwwwwwpwk", "kwwwwbwwwwk", "kwwwwwwwwwk", "kwwwwwwwwwk", "kwkwwkwwkwk", "kk kk kk kk"]),
    "ufo": ({"k": "#1a1a24", "g": "#9ff3ff", "w": "#ffffff", "s": "#c9ccd6", "d": "#6b7080", "l": "#ff4fa3", "y": "#fff36b"}, ["    kkkkk    ", "   kggwggk   ", "  kggggwggk  ", " kkssssssskk ", "ksslsssyssslk", "kddddddddddk ", " kkkdddddkkk ", "   y  y  y   "]),
    "cd": ({"k": "#6f7390", "a": "#d8f3ff", "b": "#ffc2f0", "c": "#fff3b0", "d": "#b8ffd0", "w": "#ffffff"}, ["   kkkkk   ", "  kaabbbk  ", " kaaabbbck ", "kdaawbbccck", "kddak kccck", "kddk   kcck", "kddak kccck", "kdddaabbcck", " kddaabbck ", "  kddabbk  ", "   kkkkk   "]),
    "floppy": ({"k": "#141414", "b": "#3a5bd9", "s": "#c9ccd6", "w": "#ffffff", "r": "#ff3d8b"}, ["kkkkkkkkkk", "kbbssskbbk", "kbbsksksbk", "kbbssskbbk", "kbbbbbbbbk", "kbwwwwwwbk", "kbwrrrrwbk", "kbwwwwwwbk", "kbwkkkwwbk", "kkkkkkkkkk"]),
    "alien": ({"k": "#071a04", "g": "#7dff5a", "d": "#2f8f1e", "w": "#e6ffe0"}, [" kk     kk ", "  kdgggdk  ", " kgggggggk ", "kgkkgggkkgk", "kgkwgggkwgk", "kgggggggggk", " kgggdgggk ", "  kgggggk  ", "   kkkkk   "]),
    "skull": ({"k": "#120808", "w": "#eae3cf", "s": "#a89c80", "r": "#ff2a1a"}, ["   kkkkk   ", "  kwwwwwk  ", " kwwwwwwwk ", "kwwwwwwwwsk", "kwkkkwkkkwk", "kwkrkwkrksk", "kwwkkwkkwsk", " kwwwkwwsk ", "  kwkwkwk  ", "  kwkwkwk  ", "   kkkkk   "]),
    "flame": ({"k": "#5c0a00", "r": "#e8261c", "o": "#ff8a1c", "y": "#ffe14d", "w": "#fff7d6"}, ["    r    ", "   rr    ", "   ror   ", "  rroor  ", "  roooork", " rooyyork", " roywyyok", "krywwwyok", "kroywwyrk", " kroyyrk ", "  krrrk  ", "   kkk   "]),
    "eye": ({"k": "#000000", "w": "#f4efe8", "r": "#c8102e", "i": "#2a7fff"}, ["   kkkkk   ", " kkwwrwwkk ", "kwwwiiiwwwk", "kwrwikiwrwk", "kwwwiiiwwwk", " kkwwrwwkk ", "   kkkkk   "]),
    "tear": ({"w": "#ffffff", "b": "#8fd8ff", "d": "#3a7fd0"}, ["  b  ", " bwb ", " bwb ", "bwbbb", "bbbbd", "bbbbd", " bdd "]),
    "rain": ({"g": "#9aa3b5", "d": "#5a6170", "w": "#d8dde8", "b": "#6fb0ff"}, ["   ggg     ", "  gwwwg gg ", " gwwwwwgwwg", "gwwwwwwwwwg", "gdddddddddg", " ggggggggg ", "  b  b  b  ", " b  b  b   ", "  b  b  b  "]),
    "broken": ({"k": "#202028", "a": "#a0a0b0", "b": "#e0e0e8", "c": "#707080"}, [" kkk   kkk ", "kabbk kaaak", "kabaakkaaak", "kaaaakaaack", "kaaaaakaack", " kaaakaaak ", "  kaakaak  ", "   kakck   ", "    kck    ", "     k     "]),
}


def from_sprite(pal_map, rows):
    w = max(len(r) for r in rows)
    h = len(rows)
    keys = list(pal_map.keys())
    pal = [0] + [rgb565(int(pal_map[k][1:3], 16), int(pal_map[k][3:5], 16), int(pal_map[k][5:7], 16)) for k in keys]
    pal += [0] * (16 - len(pal))
    idx = []
    for r in rows:
        r = r.ljust(w)
        idx += [0 if ch == " " else keys.index(ch) + 1 for ch in r]
    return {"w": w, "h": h, "n": 1, "pal": pal, "px": pack4(idx, w, h), "t": 1}


def emit(name, a, out):
    out.append("static const uint16_t ZZ_PAL_%s[16] = {%s};" % (name.upper(), ", ".join("0x%04X" % p for p in a["pal"])))
    body = ", ".join(str(b) for b in a["px"])
    out.append("static const uint8_t ZZ_PX_%s[%d] = {%s};" % (name.upper(), len(a["px"]), body))
    out.append("static const zz_img_t ZZ_%s = {%d, %d, %d, %d, ZZ_PAL_%s, ZZ_PX_%s};" % (
        name.upper(), a["w"], a["h"], a["n"], a["t"], name.upper(), name.upper()))
    return len(a["px"]) + 32


def main():
    meta = json.load(open(os.path.join(PNG, "meta.json")))
    imgs = {
        "meadow": from_png("meadow"),
        "space": from_png("space"),
        "cat": from_png("cat", transparent=True),
        "cat2": from_png("cat2", transparent=True),
        "cup": from_png("cup", transparent=True),
        "coin": from_png("coin", transparent=True),
        "moon": from_png("moon", transparent=True),
        "horse": from_png("horse"),
        "bird": from_png("bird", meta["birdN"], meta["birdW"]),
        "cradle": from_png("cradle", meta["cradleN"], meta["cradleW"]),
    }
    for k, (p, rows) in SPRITES.items():
        imgs["s_" + k] = from_sprite(p, rows)
    out = ["/* generated by tools/make_assets.py: do not edit */", "#pragma once", "#include <stdint.h>",
           "typedef struct { uint16_t w, h, n, t; const uint16_t *pal; const uint8_t *px; } zz_img_t;"]
    total = 0
    for k, a in imgs.items():
        total += emit(k, a, out)
    ex = meta["catEyes"]
    out.append("#define ZZ_CAT_EYES {{%d, %d}, {%d, %d}}" % (ex[0][0], ex[0][1], ex[1][0], ex[1][1]))
    out.append("#define ZZ_ASSET_BYTES %du" % total)
    open(OUT, "w").write("\n".join(out) + "\n")
    print("zz_assets.h: %d pictures, %d bytes of flash" % (len(imgs), total))


if __name__ == "__main__":
    main()
