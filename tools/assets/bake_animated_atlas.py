#!/usr/bin/env python3
"""Pack the model-generated web animation sheets for SDL targets; create no artwork."""

from pathlib import Path
import colorsys
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT.parent / 'meu-game' / 'public' / 'assets' / 'animated'
OUT = ROOT / 'assets'

# SpriteId order for animated world entities (projectiles and pickups stay in native_atlas).
SHEETS = [
    'player', 'player2', 'player3', 'player4',
    'slime', 'bat', 'brute', 'eye', 'mushroom', 'beetle', 'skeleton', 'wraith',
    'imp', 'scorpion', 'spore', 'revenant', 'sentinel', 'seer', 'voidling',
    'voidscarab', 'treant', 'lich', 'demon', 'bogwarden', 'archon', 'umbra',
    'developer', 'aurora', 'god',
]
CLEAN = {'player', 'aurora', 'archon', 'scorpion'}
VARIANTS = {'bat': (105, 1.2, 1.0), 'brute': (-18, 1.6, 1.08)}


def recolor(image, hue, saturation, lightness):
    data = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = data[x, y]
            if not a:
                continue
            h, l, s = colorsys.rgb_to_hls(r / 255, g / 255, b / 255)
            rr, gg, bb = colorsys.hls_to_rgb((h + hue / 360) % 1, min(1, l * lightness), min(1, s * saturation))
            data[x, y] = (round(rr * 255), round(gg * 255), round(bb * 255), a)
    return image


def bake(cell):
    per_row = 8 if cell == 64 else 4
    per_page = 32 if cell == 64 else 12
    pages = [Image.new('RGBA', (2048, 2048) if cell == 64 else (512, 512))
             for _ in range((len(SHEETS) + per_page - 1) // per_page)]
    for index, name in enumerate(SHEETS):
        source = Image.open(SOURCE / f'{name}.webp').convert('RGBA')
        if source.size != (512, 640):
            raise ValueError(f'{name}: expected 512x640, got {source.size}')
        page = pages[index // per_page]
        x0 = (index % per_page % per_row) * 4 * cell
        y0 = (index % per_page // per_row) * 5 * cell
        inset = 0 if name in CLEAN else 6
        for row in range(5):
            for frame in range(4):
                x = frame * 128
                y = row * 128
                tile = source.crop((x + inset, y + inset, x + 128 - inset, y + 128 - inset))
                if name in VARIANTS:
                    tile = recolor(tile, *VARIANTS[name])
                tile = tile.resize((cell, cell), Image.Resampling.LANCZOS)
                page.alpha_composite(tile, (x0 + frame * cell, y0 + row * cell))
    for page_index, page in enumerate(pages):
        suffix = '' if cell == 64 else f'_{page_index}'
        path = OUT / f'native_animations_{cell}{suffix}.png'
        page.save(path, optimize=True)
        print(f'{path}: {page.width}x{page.height}')


for frame_cell in (64, 32):
    bake(frame_cell)
