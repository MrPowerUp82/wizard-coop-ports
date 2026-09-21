#!/usr/bin/env python3
"""Bakes the six 256 px floor tiles of the web client (src/terrain.js) into assets/terrain_tiles.png.

Same palettes, same LCG seeds and the same drawing recipe per phase, so the native floor matches the
browser. Consoles then draw the floor as ~24 textured quads per frame from the shared sheet.
"""
from pathlib import Path
import math
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'assets' / 'terrain_tiles.png'
T = 256
PALETTES = [
    ['#101f1a', '#172b20', '#213b29', '#395439'],
    ['#111e2b', '#1b2f40', '#2a4355', '#547687'],
    ['#211719', '#302123', '#442c29', '#b95328'],
    ['#101e1c', '#1c3024', '#29442e', '#789153'],
    ['#131c30', '#202d43', '#2c3b51', '#a98b50'],
    ['#171020', '#281d37', '#382849', '#8c51b6'],
]


def rgb(h, a=255):
    return (int(h[1:3], 16), int(h[3:5], 16), int(h[5:7], 16), a)


class Lcg:
    def __init__(self, seed):
        self.seed = seed

    def __call__(self):
        self.seed = (self.seed * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.seed / 4294967296


def overlay(tile, draw_fn, alpha):
    """Canvas globalAlpha: draw opaque on a layer, then composite it at `alpha`."""
    layer = Image.new('RGBA', tile.size, (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(layer))
    if alpha < 1:
        a = layer.getchannel('A').point(lambda v: int(v * alpha))
        layer.putalpha(a)
    tile.alpha_composite(layer)


def bake(phase):
    colors = PALETTES[phase]
    tile = Image.new('RGBA', (T, T), rgb(colors[0]))
    d = ImageDraw.Draw(tile)
    random = Lcg(391 + phase)
    if phase == 0:
        for n in range(70):
            x, y, size = random() * T, random() * T, 8 + random() * 24
            c = rgb(colors[1 + n % 2])
            d.rectangle([x, y, x + size, y + size / 2], fill=c)
            d.rectangle([x + 4, y - 4, x + 4 + size / 2, y - 4 + size], fill=c)
    elif phase == 1:
        for y in range(0, T, 32):
            for x in range(-64, T, 64):
                offset = 32 if y % 64 else 0
                c = rgb(colors[1 + math.floor(random() * 2)])
                d.rectangle([x + offset + 1, y + 1, x + offset + 61, y + 29], fill=c)
    elif phase in (2, 5):
        seam = rgb('#984323' if phase == 2 else '#654080')
        for y in range(0, T, 64):
            for x in range(0, T, 64):
                c = rgb(colors[1 + math.floor(random() * 2)])
                pts = [(x + 8, y + 4), (x + 49, y + 2), (x + 62, y + 28), (x + 48, y + 59), (x + 10, y + 62), (x + 2, y + 30)]
                d.polygon(pts, fill=c, outline=seam)
    elif phase == 3:
        for _ in range(18):
            x, y = random() * T, random() * T
            rx, ry = 15 + random() * 24, 9 + random() * 12
            d.ellipse([x - rx, y - ry, x + rx, y + ry], fill=rgb(colors[1]), outline=rgb(colors[2]), width=2)
            d.line([(x + 18, y + 6), (x + 15, y - 3)], fill=rgb(colors[3]), width=1)
            d.line([(x + 20, y + 6), (x + 22, y - 6)], fill=rgb(colors[3]), width=1)
    elif phase == 4:
        for y in range(0, T, 64):
            for x in range(0, T, 64):
                c = rgb(colors[1 + math.floor(random() * 2)])
                d.rectangle([x + 2, y + 2, x + 61, y + 61], fill=c)

                def inlay(ld, x=x, y=y):
                    ld.rectangle([x + 7, y + 7, x + 57, y + 57], outline=rgb(colors[3]), width=1)
                    ld.polygon([(x + 32, y + 18), (x + 42, y + 32), (x + 32, y + 46), (x + 22, y + 32)], outline=rgb(colors[3]))
                overlay(tile, inlay, 0.4)
        d = ImageDraw.Draw(tile)
    for n in range(110):
        x, y = math.floor(random() * T), math.floor(random() * T)
        w = 2 + math.floor(random() * 5)
        d.rectangle([x, y, x + w - 1, y + (5 if phase == 0 else 2) - 1], fill=rgb(colors[n % 4]))
    cracks = []
    for _ in range(5):
        x, y = random() * 230, random() * 210
        cracks.append([(x, y), (x + 13, y + 10), (x + 8, y + 22), (x + 25, y + 36)])
    overlay(tile, lambda ld: [ld.line(c, fill=rgb(colors[3]), width=2 if phase == 2 else 1) for c in cracks], 0.55 if phase == 2 else 0.25)
    return tile


sheet = Image.new('RGBA', (T * 6, T))
for phase in range(6):
    sheet.paste(bake(phase), (phase * T, 0))
sheet.save(OUT, optimize=True)
print(f'wrote {OUT} {sheet.width}x{sheet.height}')
