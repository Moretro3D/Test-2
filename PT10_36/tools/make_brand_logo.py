#!/usr/bin/env python3
"""Convertit le logo transparent Moretro3D en bitmap RGB565 embarque."""

from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "moretro3d-logo-transparent.png"
OUTPUT = ROOT / "brand_logo.h"
SIZE = 176
BACKGROUND = (0x10, 0x18, 0x2E, 255)


def rgb565(r: int, g: int, b: int) -> int:
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


image = Image.open(SOURCE).convert("RGBA")
alpha = image.getchannel("A")
bounds = alpha.getbbox()
if not bounds:
    raise SystemExit("Logo vide")
image = image.crop(bounds)
image.thumbnail((SIZE - 8, SIZE - 8), Image.Resampling.LANCZOS)

canvas = Image.new("RGBA", (SIZE, SIZE), (0, 0, 0, 0))
x = (SIZE - image.width) // 2
y = (SIZE - image.height) // 2
canvas.alpha_composite(image, (x, y))
preview = ROOT / "assets" / "moretro3d-logo-176.png"
canvas.save(preview)

flat = Image.alpha_composite(Image.new("RGBA", canvas.size, BACKGROUND), canvas).convert("RGB")
values = [rgb565(r, g, b) for r, g, b in flat.getdata()]

lines = [
    "#pragma once",
    "#include <Arduino.h>",
    "",
    f"static constexpr uint16_t BRAND_LOGO_W = {SIZE};",
    f"static constexpr uint16_t BRAND_LOGO_H = {SIZE};",
    f"static const uint16_t BRAND_LOGO_PIXELS[{SIZE * SIZE}] PROGMEM = {{",
]
for start in range(0, len(values), 12):
    chunk = values[start:start + 12]
    lines.append("  " + ", ".join(f"0x{value:04X}" for value in chunk) + ",")
lines.extend(["};", ""])
OUTPUT.write_text("\n".join(lines), encoding="utf-8")
print(f"Logo {SIZE}x{SIZE}: {OUTPUT}")
print(f"Apercu transparent: {preview}")
