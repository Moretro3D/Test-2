"""Prepare the validated home gym artwork for the embedded AMOLED UI."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "home_gym_source_v10_17.png"
PREVIEW = ROOT / "assets" / "home_gym_sprite_v10_17_preview.png"
HEADER = ROOT / "home_gym_sprite.h"
MAX_W, MAX_H = 156, 136

im = Image.open(SOURCE).convert("RGBA")
alpha = im.getchannel("A")
bbox = alpha.getbbox()
if not bbox:
    raise SystemExit("home gym source has no visible pixels")
im = im.crop(bbox)
scale = min(MAX_W / im.width, MAX_H / im.height)
w = max(1, round(im.width * scale))
h = max(1, round(im.height * scale))

# The source is deliberately blocky. NEAREST preserves its hard pixel clusters.
im = im.resize((w, h), Image.Resampling.NEAREST)
canvas = Image.new("RGBA", (MAX_W, MAX_H), (0, 0, 0, 0))
ox, oy = (MAX_W - w) // 2, MAX_H - h
canvas.alpha_composite(im, (ox, oy))

# Reserve index 0 for transparency and use a compact, deterministic palette.
rgb = Image.new("RGB", canvas.size, (0, 0, 0))
rgb.paste(canvas.convert("RGB"), mask=canvas.getchannel("A"))
quant = rgb.quantize(colors=31, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
palette_raw = quant.getpalette()[: 31 * 3]
palette = [(0, 0, 0)] + [tuple(palette_raw[i:i + 3]) for i in range(0, len(palette_raw), 3)]

indices = []
alpha_pixels = list(canvas.getchannel("A").getdata())
quant_pixels = list(quant.getdata())
for a, q in zip(alpha_pixels, quant_pixels):
    indices.append(0 if a < 96 else q + 1)

def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

lines = [
    "#pragma once",
    "#include <Arduino.h>",
    "",
    f"static constexpr uint8_t HOME_GYM_W = {MAX_W};",
    f"static constexpr uint8_t HOME_GYM_H = {MAX_H};",
    f"static constexpr uint8_t HOME_GYM_COLORS = {len(palette)};",
    "static const uint16_t HOME_GYM_PALETTE[HOME_GYM_COLORS] PROGMEM = {",
]
for i in range(0, len(palette), 8):
    lines.append("  " + ", ".join(f"0x{rgb565(c):04X}" for c in palette[i:i + 8]) + ",")
lines += ["};", "", "static const uint8_t HOME_GYM_PIXELS[HOME_GYM_W * HOME_GYM_H] PROGMEM = {"]
for i in range(0, len(indices), 32):
    lines.append("  " + ", ".join(str(v) for v in indices[i:i + 32]) + ",")
lines += ["};", ""]
HEADER.write_text("\n".join(lines), encoding="utf-8")

# Enlarged nearest-neighbour preview for visual verification outside firmware.
canvas.resize((MAX_W * 4, MAX_H * 4), Image.Resampling.NEAREST).save(PREVIEW)
print(f"generated {HEADER.name}: {MAX_W}x{MAX_H}, {len(palette)} colors")
