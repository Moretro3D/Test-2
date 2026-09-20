"""Split and pack the six validated home-screen habitat grounds."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "home_habitats_v10_19_concept.png"
HEADER = ROOT / "home_habitat_sprites.h"
OUT_DIR = ROOT / "assets" / "home_habitats_v10_19"
OUT_DIR.mkdir(parents=True, exist_ok=True)

NAMES = ("MEADOW", "WATER", "FOREST", "VOLCANO", "MOUNTAIN", "SNOW")
W, H = 233, 117  # drawn at x2 => exactly 466 x 234 pixels

im = Image.open(SOURCE).convert("RGBA")
sw, sh = im.size
columns = ((0, sw // 3), (sw // 3, 2 * sw // 3), (2 * sw // 3, sw))
rows = ((0, 500), (500, sh))

def visible_bbox(tile):
    mask = tile.getchannel("A").point(lambda v: 255 if v >= 96 else 0)
    return mask.getbbox()

def rgb565(c):
    r, g, b = c
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

packed = []
for row in range(2):
    for col in range(3):
        x0, x1 = columns[col]
        y0, y1 = rows[row]
        tile = im.crop((x0, y0, x1, y1))
        bbox = visible_bbox(tile)
        if not bbox:
            raise SystemExit(f"empty habitat tile {row},{col}")
        tile = tile.crop(bbox)

        # Remove near-transparent generation noise, then fill the complete ground
        # rectangle. The original art is wide; stretching it to the lower half of
        # the round display keeps every habitat on the exact same horizon.
        clean = Image.new("RGBA", tile.size, (0, 0, 0, 0))
        alpha = tile.getchannel("A").point(lambda v: 255 if v >= 96 else 0)
        clean.paste(tile, mask=alpha)
        bg = clean.resize((W, H), Image.Resampling.NEAREST)
        rgb = Image.new("RGB", (W, H), bg.getpixel((W // 2, H - 1))[:3])
        rgb.paste(bg.convert("RGB"), mask=bg.getchannel("A"))
        quant = rgb.quantize(colors=32, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal_raw = quant.getpalette()[: 32 * 3]
        palette = [tuple(pal_raw[i:i + 3]) for i in range(0, len(pal_raw), 3)]
        indices = list(quant.getdata())
        name = NAMES[row * 3 + col]
        rgb.resize((W * 2, H * 2), Image.Resampling.NEAREST).save(OUT_DIR / f"{name.lower()}.png")
        packed.append((name, palette, indices))

lines = [
    "#pragma once",
    "#include <Arduino.h>",
    "",
    f"static constexpr uint8_t HOME_HABITAT_W = {W};",
    f"static constexpr uint8_t HOME_HABITAT_H = {H};",
    "static constexpr uint8_t HOME_HABITAT_COLORS = 32;",
    "",
]
for name, palette, indices in packed:
    lines.append(f"static const uint16_t HOME_{name}_PALETTE[HOME_HABITAT_COLORS] PROGMEM = {{")
    for i in range(0, 32, 8):
        lines.append("  " + ", ".join(f"0x{rgb565(c):04X}" for c in palette[i:i + 8]) + ",")
    lines += ["};", f"static const uint8_t HOME_{name}_PIXELS[HOME_HABITAT_W * HOME_HABITAT_H] PROGMEM = {{"]
    for i in range(0, len(indices), 40):
        lines.append("  " + ", ".join(str(v) for v in indices[i:i + 40]) + ",")
    lines += ["};", ""]

lines += [
    "static const uint16_t *const HOME_HABITAT_PALETTES[6] = {",
    "  HOME_MEADOW_PALETTE, HOME_WATER_PALETTE, HOME_FOREST_PALETTE,",
    "  HOME_VOLCANO_PALETTE, HOME_MOUNTAIN_PALETTE, HOME_SNOW_PALETTE",
    "};",
    "static const uint8_t *const HOME_HABITAT_PIXELS[6] = {",
    "  HOME_MEADOW_PIXELS, HOME_WATER_PIXELS, HOME_FOREST_PIXELS,",
    "  HOME_VOLCANO_PIXELS, HOME_MOUNTAIN_PIXELS, HOME_SNOW_PIXELS",
    "};",
    "",
]
HEADER.write_text("\n".join(lines), encoding="utf-8")
print(f"generated {len(packed)} habitats at {W}x{H} (displayed x2)")
