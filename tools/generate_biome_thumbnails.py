#!/usr/bin/env python3
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "biome_thumbnails_canva"
OUT = ROOT / "biome_thumbnails.h"
NAMES = ("PRAIRIE", "PLAGE", "FORET", "VOLCAN", "MONTAGNE", "NEIGE", "CIEL")
SIZE = 88
COLORS = 31


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def prepare(path):
    image = Image.open(path).convert("RGBA")
    alpha = image.getchannel("A")
    box = alpha.getbbox()
    if not box:
        raise SystemExit(f"Image vide: {path}")
    image = image.crop(box)
    # Les créations Canva sont plus hautes que larges. Pour remplir réellement
    # la bulle AMOLED, on les adapte au carré complet sans conserver de marge.
    canvas = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
    mask = canvas.getchannel("A")
    base = Image.new("RGB", canvas.size, (0, 0, 0))
    base.paste(canvas.convert("RGB"), mask=mask)
    quant = base.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT)
    palette = quant.getpalette()[:COLORS*3]
    pixels = list(quant.getdata())
    alphas = list(mask.getdata())
    pixels = [p if a >= 96 else 255 for p, a in zip(pixels, alphas)]
    colors = [rgb565(tuple(palette[i:i+3])) for i in range(0, len(palette), 3)]
    while len(colors) < COLORS:
        colors.append(0)
    return colors, pixels


lines = [
    "#pragma once",
    "#include <Arduino.h>",
    f"#define BIOME_THUMB_W {SIZE}",
    f"#define BIOME_THUMB_H {SIZE}",
    f"#define BIOME_THUMB_COLORS {COLORS}",
    "#define BIOME_THUMB_TRANSPARENT 255",
    "",
]

for name in NAMES:
    colors, pixels = prepare(SRC / f"{name.lower()}.png")
    lines.append(f"static const uint16_t BIOME_{name}_PALETTE[BIOME_THUMB_COLORS] PROGMEM = {{")
    for i in range(0, len(colors), 8):
        lines.append("  " + ", ".join(f"0x{x:04X}" for x in colors[i:i+8]) + ",")
    lines.append("};")
    lines.append(f"static const uint8_t BIOME_{name}_PIXELS[BIOME_THUMB_W * BIOME_THUMB_H] PROGMEM = {{")
    for i in range(0, len(pixels), 32):
        lines.append("  " + ", ".join(str(x) for x in pixels[i:i+32]) + ",")
    lines.append("};\n")

lines.extend([
    "static const uint16_t *const BIOME_THUMB_PALETTES[7] = {",
    "  BIOME_PRAIRIE_PALETTE, BIOME_PLAGE_PALETTE, BIOME_FORET_PALETTE,",
    "  BIOME_VOLCAN_PALETTE, BIOME_MONTAGNE_PALETTE, BIOME_NEIGE_PALETTE, BIOME_CIEL_PALETTE",
    "};",
    "static const uint8_t *const BIOME_THUMB_PIXELS[7] = {",
    "  BIOME_PRAIRIE_PIXELS, BIOME_PLAGE_PIXELS, BIOME_FORET_PIXELS,",
    "  BIOME_VOLCAN_PIXELS, BIOME_MONTAGNE_PIXELS, BIOME_NEIGE_PIXELS, BIOME_CIEL_PIXELS",
    "};",
])

OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Miniatures Canva: {OUT} ({OUT.stat().st_size} octets)")
