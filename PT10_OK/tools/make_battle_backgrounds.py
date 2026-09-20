#!/usr/bin/env python3
"""Extrait les fonds DP jour/après-midi/nuit et les compresse en RGB565/RLE."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "battle-backgrounds-dp.png"
OUTPUT = ROOT / "battle_backgrounds.h"
PREVIEW = ROOT / "assets" / "battle-backgrounds-preview.png"

# biome 0 prairie, 1 eau/plage, 2 forêt, 3 volcan/caverne, 4 montagne, 5 neige
# Chaque entrée contient les rectangles jour, après-midi et nuit (256x144).
RECTS = [
    [(15,566),(284,566),(555,566)],
    [(17,17),(286,17),(554,17)],
    [(19,840),(287,840),(556,840)],
    [(133,1110),(133,1110),(411,1110)],
    [(16,300),(285,301),(554,301)],
    [(21,1387),(294,1387),(564,1387)],
]

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def encode(image):
    colors = list(dict.fromkeys(image.getdata()))
    if len(colors) > 254:
        image = image.quantize(colors=128, method=Image.Quantize.MEDIANCUT).convert("RGB")
        colors = list(dict.fromkeys(image.getdata()))
    lookup = {color:index for index,color in enumerate(colors)}
    values = [lookup[color] for color in image.getdata()]
    runs=[]
    previous,count=values[0],0
    for value in values:
        if value==previous and count<255: count+=1
        else:
            runs.extend((count,previous)); previous,count=value,1
    runs.extend((count,previous))
    return image,colors,runs

sheet=Image.open(SOURCE).convert("RGB")
if sheet.size!=(1800,1880):
    raise SystemExit(f"Planche attendue 1800x1880, reçue {sheet.size}")

assets=[]
preview=Image.new("RGB",(768,864),(255,255,255))
for biome,row in enumerate(RECTS):
    for phase,(x,y) in enumerate(row):
        image=sheet.crop((x,y,x+256,y+144))
        image,colors,runs=encode(image)
        assets.append((biome,phase,colors,runs))
        preview.paste(image,(phase*256,biome*144))
preview.save(PREVIEW)

lines=["#pragma once","#include <Arduino.h>",
       "static constexpr uint16_t BATTLE_BG_W=256;",
       "static constexpr uint16_t BATTLE_BG_H=144;"]
for biome,phase,colors,runs in assets:
    stem=f"BATTLE_BG_{biome}_{phase}"
    lines += [f"static const uint16_t {stem}_PAL[{len(colors)}] PROGMEM={{",
              "  "+",".join(f"0x{rgb565(c):04X}" for c in colors)+",","};",
              f"static const uint8_t {stem}_RLE[{len(runs)}] PROGMEM={{"]
    for start in range(0,len(runs),24):
        lines.append("  "+",".join(str(v) for v in runs[start:start+24]) + ",")
    lines += ["};",""]
lines += [
    "struct BattleBgRef { const uint16_t *pal; const uint8_t *rle; uint16_t rleSize; };",
    "static const BattleBgRef BATTLE_BG_ASSETS[6][3]={",
]
for biome in range(6):
    refs=[]
    for phase in range(3):
        stem=f"BATTLE_BG_{biome}_{phase}"
        refs.append("{"+stem+"_PAL,"+stem+"_RLE,sizeof("+stem+"_RLE)}")
    lines.append("  {"+",".join(refs)+"},")
lines += ["};",""]
OUTPUT.write_text("\n".join(lines),encoding="utf-8")
print("18 fonds extraits et compressés:",OUTPUT.stat().st_size,"octets")
print("Aperçu:",PREVIEW)
