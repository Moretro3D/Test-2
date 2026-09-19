#!/usr/bin/env python3
"""Convertit la rangee originale des 8 badges Kanto en sprites RGB565 16x16."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(r"C:\Users\morga\Pictures\images\Badge Kanto.png")
OUT = ROOT / "kanto_badge_sprites.h"
ARCHIVE = ROOT / "assets" / "design" / "badges-kanto-originaux.png"

im = Image.open(SOURCE).convert("RGB")
if im.size != (128, 52):
    raise SystemExit(f"Planche inattendue: {im.size}, attendu 128x52")

bg = im.getpixel((0, 0))
pixels = []
for badge in range(8):
    col, row = badge % 4, badge // 4
    crop = im.crop((col * 32 + 8, row * 26 + 8,
                    col * 32 + 24, row * 26 + 24))
    for r, g, b in crop.getdata():
        if abs(r-bg[0]) + abs(g-bg[1]) + abs(b-bg[2]) < 18:
            pixels.append(0)
        else:
            pixels.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

lines = [
    "#pragma once", "#include <Arduino.h>", "",
    "#define KANTO_BADGE_W 16", "#define KANTO_BADGE_H 16", "",
    f"static const uint16_t KANTO_BADGE_PIXELS[{len(pixels)}] PROGMEM = {{"
]
for i in range(0, len(pixels), 16):
    lines.append("  " + ", ".join(f"0x{v:04X}" for v in pixels[i:i+16]) + ",")
lines += ["};", ""]
OUT.write_text("\n".join(lines), encoding="utf-8")
ARCHIVE.parent.mkdir(parents=True, exist_ok=True)
ARCHIVE.write_bytes(SOURCE.read_bytes())
print(f"OK: {OUT.name}, 8 badges originaux 16x16")
