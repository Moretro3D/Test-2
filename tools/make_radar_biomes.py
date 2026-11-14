from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "radar-biomes"
OUT = ROOT / "radar_biomes.h"
NAMES = ("prairie", "plage", "foret", "volcan", "montagne", "neige", "ciel")
W = H = 233
COLORS = 64


def rgb565(rgb):
    r, g, b = rgb
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


lines = [
    "#pragma once",
    "#include <Arduino.h>",
    f"#define RADAR_BIOME_W {W}",
    f"#define RADAR_BIOME_H {H}",
    f"#define RADAR_BIOME_COLORS {COLORS}",
    "",
]
counts = []
for name in NAMES:
    source = Image.open(SRC / f"{name}.png").convert("RGB")
    # Recadrage carre puis reduction de qualite, faite une seule fois pendant le build.
    side = min(source.size)
    left = (source.width - side) // 2
    top = (source.height - side) // 2
    source = source.crop((left, top, left + side, top + side))
    source = source.resize((W, H), Image.Resampling.LANCZOS)
    indexed = source.quantize(colors=COLORS, method=Image.Quantize.MEDIANCUT,
                              dither=Image.Dither.NONE)
    palette = indexed.getpalette()[:COLORS * 3]
    palette += [0] * (COLORS * 3 - len(palette))
    colors = [rgb565(tuple(palette[i:i + 3])) for i in range(0, COLORS * 3, 3)]
    key = name.upper()
    lines.append(f"static const uint16_t RADAR_{key}_PALETTE[RADAR_BIOME_COLORS] PROGMEM = {{")
    for i in range(0, COLORS, 8):
        lines.append("  " + ", ".join(f"0x{x:04X}" for x in colors[i:i + 8]) + ",")
    lines.append("};")

    data = list(indexed.getdata())
    runs = []
    for y in range(H):
        row = data[y * W:(y + 1) * W]
        x = 0
        while x < W:
            color = row[x]
            end = x + 1
            while end < W and row[end] == color:
                end += 1
            runs.append(((end - x) << 6) | color)
            x = end
    counts.append(len(runs))
    lines.append(f"static const uint16_t RADAR_{key}_RUNS[{len(runs)}] PROGMEM = {{")
    for i in range(0, len(runs), 16):
        lines.append("  " + ", ".join(f"0x{x:04X}" for x in runs[i:i + 16]) + ",")
    lines.append("};")
    lines.append("")

lines.append("static const uint16_t *const RADAR_BIOME_PALETTES[7] = {")
lines.append("  " + ", ".join(f"RADAR_{n.upper()}_PALETTE" for n in NAMES))
lines.append("};")
lines.append("static const uint16_t *const RADAR_BIOME_RUNS[7] = {")
lines.append("  " + ", ".join(f"RADAR_{n.upper()}_RUNS" for n in NAMES))
lines.append("};")
lines.append("static const uint32_t RADAR_BIOME_RUN_COUNTS[7] = {")
lines.append("  " + ", ".join(str(x) for x in counts))
lines.append("};")
OUT.write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"Radar biomes: {', '.join(NAMES)}")
print(f"Header: {OUT} ({OUT.stat().st_size} bytes, {sum(counts)} runs)")
