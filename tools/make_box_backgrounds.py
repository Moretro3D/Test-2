#!/usr/bin/env python3
"""Découpe la planche 4x4 et génère 16 fonds pixel-art RLE pour la Boîte."""
from pathlib import Path
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
SOURCE=Path(r"C:\Users\morga\Pictures\fond\Fond.png")
OUT=ROOT/"assets"/"box_backgrounds"
HEADER=ROOT/"box_backgrounds.h"
X=[3,171,339,507]; Y=[2,153,306,457]
W,H=156,116
TARGET=(150,108)

def rgb565(rgb):
    r,g,b=rgb[:3]
    return (r>>3)<<11|(g>>2)<<5|(b>>3)

im=Image.open(SOURCE).convert("RGB")
OUT.mkdir(parents=True,exist_ok=True)
chunks=[]
for row,y in enumerate(Y):
    for col,x in enumerate(X):
        # Retire le cadre coloré extérieur, conserve uniquement le décor.
        tile=im.crop((x+5,y+4,x+W-4,y+H-4)).resize(TARGET,Image.Resampling.NEAREST)
        tile.save(OUT/f"box-{row*4+col+1:02d}.png",optimize=True)
        q=tile.quantize(colors=32,method=Image.Quantize.MEDIANCUT,dither=Image.Dither.NONE)
        palraw=q.getpalette() or []; pal=[]
        for i in range(32):
            rgb=palraw[i*3:i*3+3]
            pal.append(rgb565(rgb) if len(rgb)==3 else 0)
        data=list(q.getdata()); rle=[]; i=0
        while i<len(data):
            v=data[i]; n=1
            while i+n<len(data) and data[i+n]==v and n<255: n+=1
            rle.extend((n,v)); i+=n
        chunks.append((pal,rle))

lines=["#pragma once","#include <Arduino.h>",
       f"static constexpr uint8_t BOX_BG_COUNT={len(chunks)};",
       f"static constexpr uint8_t BOX_BG_W={TARGET[0]}, BOX_BG_H={TARGET[1]};",
       "struct BoxBgRef { const uint16_t *pal; const uint8_t *rle; uint16_t rleSize; };" ]
for i,(pal,rle) in enumerate(chunks):
    lines.append(f"static const uint16_t BOX_BG_{i}_PAL[32] PROGMEM={{"+",".join(f"0x{x:04X}" for x in pal)+"};")
    lines.append(f"static const uint8_t BOX_BG_{i}_RLE[{len(rle)}] PROGMEM={{"+",".join(map(str,rle))+"};")
lines.append("static const BoxBgRef BOX_BG_ASSETS[BOX_BG_COUNT]={")
for i,(_,rle) in enumerate(chunks): lines.append(f"  {{BOX_BG_{i}_PAL,BOX_BG_{i}_RLE,{len(rle)}}},")
lines.append("};")
HEADER.write_text("\n".join(lines)+"\n",encoding="utf-8")
print(f"BOX BACKGROUNDS OK: {len(chunks)} fonds -> {HEADER.name}")
