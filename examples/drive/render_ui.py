#!/usr/bin/env python3
"""Render the lines printed by test_ui.c to a PNG (needs Pillow): render_ui.py lines.txt out.png
Each line: x0 y0 x1 y1 width r g b, coordinates and width in 22.10 fixed point (1 px = 1024), screen 320 x 160."""
import sys
from PIL import Image, ImageDraw

SC = 4                                                  # draw 4x larger, then downscale (anti-aliasing)
img = Image.new('RGB', (320 * SC, 160 * SC), 'black')
d = ImageDraw.Draw(img)
for line in open(sys.argv[1]):
    x0, y0, x1, y1, w, r, g, b = map(int, line.split())
    width = max(1, round(w / 1024 * SC))
    p0, p1 = (x0 / 1024 * SC, y0 / 1024 * SC), (x1 / 1024 * SC, y1 / 1024 * SC)
    d.line([p0, p1], fill=(r, g, b), width=width)
    if width > 2:                                       # round caps, like the firmware's lines
        for x, y in (p0, p1):
            d.ellipse([x - width / 2, y - width / 2, x + width / 2, y + width / 2], fill=(r, g, b))
img.resize((640, 320), Image.LANCZOS).save(sys.argv[2])
