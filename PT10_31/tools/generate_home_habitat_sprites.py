"""Split and pack the six validated home-screen habitat grounds."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "home_habitats_v10_19_concept.png"
WATER_SOURCE = ROOT / "assets" / "home_habitat_water_v10_23.png"
FOREST_SOURCE = ROOT / "assets" / "home_habitat_forest_v10_24.png"
MOUNTAIN_SOURCE = ROOT / "assets" / "home_habitat_mountain_v10_31.png"
HEADER = ROOT / "home_habitat_sprites.h"
OUT_DIR = ROOT / "assets" / "home_habitats_v10_19"
OUT_DIR.mkdir(parents=True, exist_ok=True)

NAMES = ("MEADOW", "WATER", "FOREST", "VOLCANO", "MOUNTAIN", "SNOW")
# Full-bleed format. Drawn at x2, every habitat is 500x210 and deliberately
# exceeds the 466 px panel so the physical round screen performs the crop.
W, H = 250, 105
DISPLAY_X, DISPLAY_Y = -17, 193

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

def flat_pixels(image):
    if hasattr(image, "get_flattened_data"):
        return image.get_flattened_data()
    return image.getdata()

packed = []
for row in range(2):
    for col in range(3):
        x0, x1 = columns[col]
        y0, y1 = rows[row]
        tile = im.crop((x0, y0, x1, y1))
        if row == 0 and col == 1 and WATER_SOURCE.exists():
            tile = Image.open(WATER_SOURCE).convert("RGBA")
        if row == 0 and col == 2 and FOREST_SOURCE.exists():
            tile = Image.open(FOREST_SOURCE).convert("RGBA")
        if row == 1 and col == 1 and MOUNTAIN_SOURCE.exists():
            tile = Image.open(MOUNTAIN_SOURCE).convert("RGBA")
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
        quant = rgb.quantize(colors=31, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal_raw = quant.getpalette()[: 31 * 3]
        palette = [(0, 0, 0)] + [tuple(pal_raw[i:i + 3]) for i in range(0, len(pal_raw), 3)]
        indices = [v + 1 for v in flat_pixels(quant)]
        resized_alpha = list(flat_pixels(bg.getchannel("A")))
        for i, alpha_value in enumerate(resized_alpha):
            if alpha_value < 96:
                indices[i] = 0

        name = NAMES[row * 3 + col]
        preview = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        preview_pixels = []
        for idx in indices:
            preview_pixels.append((*palette[idx], 255) if idx else (0, 0, 0, 0))
        preview.putdata(preview_pixels)
        preview.resize((W * 2, H * 2), Image.Resampling.NEAREST).save(OUT_DIR / f"{name.lower()}.png")
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
