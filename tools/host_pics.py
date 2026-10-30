#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Yoann
"""ZINZIN: the test bench's screens as pictures. Every build/host_out/*.ppm becomes a PNG; the clips' frames become
animated GIFs (12.5 frames a second, as on the FM-1); and a contact sheet of the stills, with their names.

    python3 tools/host_pics.py build/host_out [scale]
"""
import glob
import os
import re
import sys
from PIL import Image, ImageDraw


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/host_out"
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 2
    stills, clips = [], {}
    for p in sorted(glob.glob(os.path.join(out, "*.ppm"))):
        name = os.path.basename(p)[:-4]
        im = Image.open(p).convert("RGB")
        m = re.match(r"clip_(.+)_(\d+)$", name)
        if m:
            clips.setdefault(m.group(1), []).append(im)
        else:
            im.resize((240 * scale, 240 * scale), Image.NEAREST).save(os.path.join(out, name + ".png"))
            stills.append((name, im))
        os.remove(p)
    for name, frames in clips.items():
        big = [f.resize((240 * scale, 240 * scale), Image.NEAREST) for f in frames]
        big[0].save(os.path.join(out, "clip_%s.gif" % name), save_all=True, append_images=big[1:], duration=80, loop=0,
                    optimize=False)
    if stills:
        cols = 5
        rows = (len(stills) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * 250 + 10, rows * 266 + 10), (24, 20, 36))
        d = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(stills):
            x, y = 10 + (i % cols) * 250, 10 + (i // cols) * 266
            sheet.paste(im, (x, y))
            d.text((x, y + 243), name, fill=(255, 243, 107))
        sheet.save(os.path.join(out, "planche.png"))
    print("%d stills, %d clips" % (len(stills), len(clips)))


if __name__ == "__main__":
    main()
