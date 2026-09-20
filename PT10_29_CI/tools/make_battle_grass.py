#!/usr/bin/env python3
"""Convertit le sol herbe 240x112 fourni en palette RGB565 + RLE."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "battle-grass-original.png"
OUTPUT = ROOT / "battle_grass.h"

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

image = Image.open(SOURCE).convert("RGB")
if image.size != (240, 112):
    raise SystemExit(f"Dimension attendue 240x112, reçue {image.size}")

colors = list(dict.fromkeys(image.getdata()))
if len(colors) > 255:
    raise SystemExit(f"Palette trop grande: {len(colors)} couleurs")
lookup = {color: index for index, color in enumerate(colors)}
indices = [lookup[color] for color in image.getdata()]
runs = []
previous = indices[0]
count = 0
for value in indices:
    if value == previous and count < 255:
        count += 1
    else:
        runs.extend((count, previous))
        previous, count = value, 1
runs.extend((count, previous))

lines = [
    "#pragma once",
    "#include <Arduino.h>",
    "static constexpr uint16_t BATTLE_GRASS_W=240;",
    "static constexpr uint16_t BATTLE_GRASS_H=112;",
    f"static const uint16_t BATTLE_GRASS_PALETTE[{len(colors)}] PROGMEM={{",
    "  " + ",".join(f"0x{rgb565(color):04X}" for color in colors),
    "};",
    f"static const uint8_t BATTLE_GRASS_RLE[{len(runs)}] PROGMEM={{",
]
for start in range(0, len(runs), 24):
    lines.append("  " + ",".join(str(v) for v in runs[start:start + 24]) + ",")
lines.extend(["};", ""])
OUTPUT.write_text("\n".join(lines), encoding="utf-8")
print(f"Sol herbe: {len(colors)} couleurs, {len(runs)//2} segments RLE, {OUTPUT}")
