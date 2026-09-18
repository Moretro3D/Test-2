#!/usr/bin/env python3
"""Convertit le pixel art fourni en icône RGB565 transparente embarquée."""
import os
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
source = Path(os.environ["POKETAMA_BALL_SOURCE"])
output = ROOT / "battle_ball_icon.h"
image = Image.open(source).convert("RGBA")

# Le fichier reçu est un pixel art 2x (72x76) : retour à ses pixels natifs.
image = image.resize((36, 38), Image.Resampling.NEAREST)
pixels = list(image.getdata())
background = pixels[0][:3]
width, height = image.size

# Retire uniquement le fond turquoise connecté aux bords.
outside, pending = set(), []
for x in range(width): pending += [(x, 0), (x, height - 1)]
for y in range(height): pending += [(0, y), (width - 1, y)]
while pending:
    x, y = pending.pop()
    pos = y * width + x
    if pos in outside or pixels[pos][:3] != background: continue
    outside.add(pos)
    if x: pending.append((x - 1, y))
    if x + 1 < width: pending.append((x + 1, y))
    if y: pending.append((x, y - 1))
    if y + 1 < height: pending.append((x, y + 1))

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

colors = [rgb565(px[:3]) for px in pixels]
mask = [0] * ((width * height + 7) // 8)
for pos in range(width * height):
    if pos not in outside and pixels[pos][3]: mask[pos >> 3] |= 1 << (pos & 7)

lines = ["#pragma once", "#include <Arduino.h>",
         f"static constexpr uint8_t BATTLE_BALL_W={width};",
         f"static constexpr uint8_t BATTLE_BALL_H={height};",
         f"static const uint16_t BATTLE_BALL_PIXELS[{len(colors)}] PROGMEM={{"]
for start in range(0, len(colors), 18):
    lines.append("  " + ",".join(f"0x{value:04X}" for value in colors[start:start+18]) + ",")
lines += ["};", f"static const uint8_t BATTLE_BALL_MASK[{len(mask)}] PROGMEM={{"]
for start in range(0, len(mask), 24):
    lines.append("  " + ",".join(f"0x{value:02X}" for value in mask[start:start+24]) + ",")
lines += ["};", ""]
output.write_text("\n".join(lines), encoding="utf-8")
print(output, width, height)
