#!/usr/bin/env python3
"""Recolore la planche fournie et genere les sprites embarques 56x56."""
from collections import deque
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r"C:\Users\morga\Pictures\images\Champion kanto.png")
OUT = ROOT / "assets" / "design" / "champions-kanto-recolores.png"
HEADER = ROOT / "kanto_leader_sprites.h"
W, H = 64, 56

PALETTES = [
    [(35,30,25),(91,69,50),(132,102,62),(232,181,138),(215,202,174)],
    [(42,31,28),(229,88,35),(35,139,211),(239,184,143),(255,211,70)],
    [(39,35,24),(214,174,34),(95,112,58),(236,184,137),(238,218,133)],
    [(38,29,30),(47,42,38),(187,67,88),(235,183,143),(242,190,198)],
    [(29,26,44),(48,42,83),(143,44,122),(231,178,139),(186,146,202)],
    [(37,24,36),(48,39,48),(193,47,105),(236,183,143),(244,214,229)],
    [(38,34,31),(210,210,205),(190,48,39),(231,177,132),(250,246,224)],
    [(24,24,27),(48,48,54),(120,26,31),(229,177,136),(216,216,218)],
]

def rgb565(rgb):
    r,g,b=rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def main():
    src=Image.open(SOURCE).convert("RGBA")
    if src.size != (500,55):
        raise SystemExit(f"Planche inattendue: {src.size}")
    pix=src.load(); bg=pix[0,0][:3]
    outside=set(); q=deque()
    for x in range(src.width): q.extend(((x,0),(x,src.height-1)))
    for y in range(src.height): q.extend(((0,y),(src.width-1,y)))
    while q:
        x,y=q.popleft()
        if (x,y) in outside or not (0<=x<src.width and 0<=y<src.height): continue
        c=pix[x,y][:3]
        if sum(abs(c[i]-bg[i]) for i in range(3))>18: continue
        outside.add((x,y))
        q.extend(((x-1,y),(x+1,y),(x,y-1),(x,y+1)))

    starts=[0,63,125,188,250,313,375,438]
    ends=[62,124,187,249,312,374,437,499]
    sheets=[]; indices=[]
    for n,(x0,x1) in enumerate(zip(starts,ends)):
        points=[]
        for y in range(src.height):
            for x in range(x0,x1+1):
                if (x,y) not in outside: points.append((x,y))
        bx0=min(x for x,y in points); bx1=max(x for x,y in points)
        by0=min(y for x,y in points); by1=max(y for x,y in points)
        ox=(W-(bx1-bx0+1))//2; oy=H-(by1-by0+1)
        out=Image.new("RGBA",(W,H),(0,0,0,0)); data=[0]*(W*H)
        pal=PALETTES[n]
        for x,y in points:
            r,g,b,_=pix[x,y]
            if r<68 and g<68 and b<68: idx=1
            elif r<125 and g<125 and b<125: idx=2
            elif r>175 and 75<g<175 and b<180: idx=3
            elif r>220 and g>190 and b<220: idx=4
            else: idx=5
            dx=ox+x-bx0; dy=oy+y-by0
            out.putpixel((dx,dy),(*pal[idx-1],255)); data[dy*W+dx]=idx
        # La source contient parfois une colonne de separation coloree au bord
        # d'une cellule. Une colonne presque pleine n'appartient jamais au
        # personnage et doit rester transparente.
        for dx in range(W):
            if sum(1 for dy in range(H) if data[dy*W+dx]) > 53:
                for dy in range(H):
                    out.putpixel((dx,dy),(0,0,0,0)); data[dy*W+dx]=0
        sheets.append(out); indices.extend(data)

    OUT.parent.mkdir(parents=True,exist_ok=True)
    sheet=Image.new("RGBA",(W*8,H),(0,0,0,0))
    for i,im in enumerate(sheets): sheet.alpha_composite(im,(i*W,0))
    sheet.save(OUT,optimize=True)

    lines=["#pragma once", "#include <Arduino.h>", "", f"#define KANTO_LEADER_W {W}", f"#define KANTO_LEADER_H {H}", ""]
    lines.append("static const uint16_t KANTO_LEADER_PALETTES[8][5] PROGMEM = {")
    for pal in PALETTES:
        lines.append("  { "+", ".join(f"0x{rgb565(c):04X}" for c in pal)+" },")
    lines.append("};\n")
    lines.append(f"static const uint8_t KANTO_LEADER_PIXELS[{len(indices)}] PROGMEM = {{")
    for i in range(0,len(indices),56):
        lines.append("  "+", ".join(str(v) for v in indices[i:i+56])+",")
    lines.append("};")
    HEADER.write_text("\n".join(lines)+"\n",encoding="utf-8")
    print(OUT)
    print(HEADER)

if __name__ == "__main__": main()
