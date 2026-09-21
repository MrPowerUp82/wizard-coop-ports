#!/usr/bin/env python3
from pathlib import Path
import sys
import colorsys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / 'assets'
# Usage: bake_native_atlas.py [cell]. 256 keeps the master atlas; consoles use 128 (sprites are
# drawn at 26-195 px, so a 768x768 atlas avoids heavy minification and saves texture bandwidth).
CELL = int(sys.argv[1]) if len(sys.argv) > 1 else 256
OUT = ASSETS / ('native_atlas.png' if CELL == 256 else f'native_atlas_{CELL}.png')
COLS = 6

# Order must match arcana::native::SpriteId (excluding Unknown/Count).
SPRITES = [
    ('player0','sprites',(0,0,313.5,313.5),None),
    ('player1','sprites',(313.5,0,313.5,313.5),None),
    ('player2','sprites',(627,0,313.5,313.5),None),
    ('player3','sprites',(940.5,0,313.5,313.5),None),
    ('slime','sprites',(0,313.5,313.5,313.5),None),
    ('bat','sprites',(313.5,313.5,313.5,313.5),(105,1.2,1.0)),
    ('brute','sprites',(627,313.5,313.5,313.5),(-18,1.6,1.08)),
    ('eye','sprites',(940.5,313.5,313.5,313.5),None),
    ('mushroom','phases',(0,0,418,442),None),
    ('beetle','phases',(418,0,408,442),None),
    ('skeleton','phases',(0,442,418,410),None),
    ('wraith','phases',(418,442,408,410),None),
    ('imp','phases',(0,852,418,402),None),
    ('scorpion','phases',(418,852,408,402),None),
    ('spore','phases2',(0,0,418,418),None),
    ('revenant','phases2',(418,0,418,418),None),
    ('sentinel','phases2',(0,418,418,418),None),
    ('seer','phases2',(418,418,418,418),None),
    ('voidling','phases2',(0,836,418,418),None),
    ('voidscarab','phases2',(418,836,418,418),None),
    ('treant','phases',(826,0,428,440),None),
    ('lich','phases',(826,440,428,408),None),
    ('demon','phases',(826,848,428,406),None),
    ('bogwarden','phases2',(836,0,418,418),None),
    ('archon','phases2',(836,418,418,418),None),
    ('umbra','phases2',(836,836,418,418),None),
    ('bolt','sprites',(0,627,313.5,313.5),None),
    ('fire','sprites',(313.5,627,313.5,313.5),None),
    ('thorn','sprites',(940.5,627,313.5,313.5),None),
    ('blade','sprites',(627,627,313.5,313.5),(55,1.0,1.0)),
    ('gem','sprites',(0,940.5,313.5,313.5),None),
    ('greenGem','sprites',(313.5,940.5,313.5,313.5),None),
    ('coin','sprites',(627,940.5,313.5,313.5),None),
    ('heart','sprites',(940.5,940.5,313.5,313.5),None),
    ('gemRare','sprites',(0,940.5,313.5,313.5),(70,1.0,1.0)),
    ('gemEpic','sprites',(0,940.5,313.5,313.5),(170,1.2,1.0)),
]

sources = {
    'sprites': Image.open(ASSETS/'sprites.webp').convert('RGBA'),
    'phases': Image.open(ASSETS/'phases.webp').convert('RGBA'),
    'phases2': Image.open(ASSETS/'phases2.webp').convert('RGBA'),
}

def recolor(img, spec):
    if not spec:
        return img
    hue, sat_mul, light_mul = spec
    pix = img.load()
    for y in range(img.height):
        for x in range(img.width):
            r,g,b,a = pix[x,y]
            if a == 0:
                continue
            h,l,s = colorsys.rgb_to_hls(r/255.0,g/255.0,b/255.0)
            h=(h+hue/360.0)%1.0
            s=min(1.0,s*sat_mul)
            l=min(1.0,l*light_mul)
            rr,gg,bb=colorsys.hls_to_rgb(h,l,s)
            pix[x,y]=(round(rr*255),round(gg*255),round(bb*255),a)
    return img

rows=(len(SPRITES)+COLS-1)//COLS
atlas=Image.new('RGBA',(COLS*CELL,rows*CELL),(0,0,0,0))
resample=Image.Resampling.LANCZOS
for i,(name,src_name,bounds,variant) in enumerate(SPRITES):
    sx,sy,sw,sh=bounds
    box=(round(sx),round(sy),round(sx+sw),round(sy+sh))
    tile=sources[src_name].crop(box).resize((CELL,CELL),resample)
    tile=recolor(tile,variant)
    atlas.alpha_composite(tile,((i%COLS)*CELL,(i//COLS)*CELL))

atlas.save(OUT,optimize=True)
print(f'wrote {OUT} {atlas.width}x{atlas.height} sprites={len(SPRITES)}')
