#!/usr/bin/env python3
"""Convertit la grande planche HD des 8 badges Kanto en sprites RGB565 32x32."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r"C:\Users\morga\Documents\exec-5104fe0c-b573-40ea-9e8a-d7c6d465f2c4.png")
OUT = ROOT / "kanto_badge_sprites.h"
ARCHIVE = ROOT / "assets" / "design" / "badges-kanto-originaux.png"

im = Image.open(SOURCE).convert("RGBA")
if im.size != (1967, 799):
    raise SystemExit(f"Planche inattendue: {im.size}, attendu 1967x799")

# Zones des badges uniquement : les cartouches numerotes restent exclus.
boxes = [
    (150,145,410,390), (660,145,890,390), (1100,125,1390,405), (1600,125,1920,405),
    (150,495,425,765), (650,490,900,750), (1135,480,1410,750), (1625,485,1955,785),
]
pixels = []
for box in boxes:
    crop = im.crop(box)
    alpha = crop.getchannel("A").getbbox()
    if not alpha:
        raise SystemExit(f"Badge vide dans {box}")
    crop = crop.crop(alpha)
    crop.thumbnail((30,30), Image.Resampling.NEAREST)
    sprite = Image.new("RGBA", (32,32), (0,0,0,0))
    sprite.alpha_composite(crop, ((32-crop.width)//2, (32-crop.height)//2))
    for r, g, b, a in sprite.getdata():
        if a < 80:
            pixels.append(0)
        else:
            pixels.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

lines = [
    "#pragma once", "#include <Arduino.h>", "",
    "#define KANTO_BADGE_W 32", "#define KANTO_BADGE_H 32", "",
    f"static const uint16_t KANTO_BADGE_PIXELS[{len(pixels)}] PROGMEM = {{"
]
for i in range(0, len(pixels), 16):
    lines.append("  " + ", ".join(f"0x{v:04X}" for v in pixels[i:i+16]) + ",")
lines += ["};", ""]
OUT.write_text("\n".join(lines), encoding="utf-8")
ARCHIVE.parent.mkdir(parents=True, exist_ok=True)
ARCHIVE.write_bytes(SOURCE.read_bytes())
print(f"OK: {OUT.name}, 8 badges HD 32x32")
