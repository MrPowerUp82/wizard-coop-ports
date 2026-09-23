#!/usr/bin/env python3
"""XMB art for the PSP EBOOT: icon0.png (144x82) and pic1.png (480x272), made from the game's own
sprites and floor tiles so the menu entry looks like the game."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'assets'
OUT = ROOT / 'platforms' / 'psp' / 'sdl'
atlas = Image.open(ASSETS / 'native_atlas_128.png').convert('RGBA')
tiles = Image.open(ASSETS / 'terrain_tiles.png').convert('RGBA')
font_path = ASSETS / 'fonts' / 'DejaVuSans-Bold.ttf'
CELL, COLS = 128, 7  # arcana::native::kAtlasColumns
GOLD = (255, 214, 110, 255)


def sprite(index, size):
    x, y = (index % COLS) * CELL, (index // COLS) * CELL
    return atlas.crop((x, y, x + CELL, y + CELL)).resize((size, size), Image.Resampling.LANCZOS)


def floor(w, h, phase, darken=0.55):
    tile = tiles.crop((phase * 256, 0, phase * 256 + 256, 256))
    img = Image.new('RGBA', (w, h))
    for y in range(0, h, 256):
        for x in range(0, w, 256):
            img.paste(tile, (x, y))
    shade = Image.new('RGBA', (w, h), (3, 8, 13, int(255 * darken)))
    return Image.alpha_composite(img, shade)


def glow_text(img, xy, text, size, fill, anchor='mm'):
    font = ImageFont.truetype(str(font_path), size)
    layer = Image.new('RGBA', img.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    d.text(xy, text, font=font, fill=(255, 170, 60, 200), anchor=anchor)
    layer = layer.filter(ImageFilter.GaussianBlur(size / 6))
    img.alpha_composite(layer)
    ImageDraw.Draw(img).text(xy, text, font=font, fill=fill, anchor=anchor, stroke_width=max(1, size // 14), stroke_fill=(20, 12, 8, 255))


# icon0.png: the four arcanists over the forest floor, title on top.
icon = floor(144, 82, 0, 0.35)
for i, x in enumerate((6, 38, 70, 102)):
    icon.alpha_composite(sprite(i, 40), (x, 36))
glow_text(icon, (72, 20), 'ARCANA', 22, GOLD)
icon.save(OUT / 'icon0.png')

# pic1.png: a boss and a crowd on the void floor, dimmed so the XMB text stays readable.
pic = floor(480, 272, 5, 0.45)
for index, x, y, size in ((25, 300, 40, 190), (18, 40, 150, 70), (19, 120, 190, 64), (18, 200, 170, 58), (19, 420, 190, 70),
                          (18, 90, 60, 50), (19, 250, 205, 54)):
    pic.alpha_composite(sprite(index, size), (x, y))
for i, x in enumerate((150, 190, 230)):
    pic.alpha_composite(sprite(i, 60), (x, 110))
pic.alpha_composite(Image.new('RGBA', pic.size, (0, 0, 0, 70)))
pic.save(OUT / 'pic1.png')
print('wrote', OUT / 'icon0.png', OUT / 'pic1.png')
