#!/usr/bin/env python3
"""Convertit la planche recolorisee HD en huit sprites embarques 64x56."""
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "design" / "champions-kanto-recolores-hd.png"
OUT = ROOT / "assets" / "design" / "champions-kanto-recolores.png"
HEADER = ROOT / "kanto_leader_sprites.h"
W, H, COLORS = 64, 56, 15

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def fit_sprite(cell):
    alpha = cell.getchannel("A")
    bbox = alpha.point(lambda a: 255 if a > 24 else 0).getbbox()
    if not bbox:
        raise RuntimeError("Sprite vide dans la planche")
    crop = cell.crop(bbox)
    scale = min((W - 4) / crop.width, (H - 2) / crop.height)
    size = (max(1, round(crop.width * scale)), max(1, round(crop.height * scale)))
    crop = crop.resize(size, Image.Resampling.NEAREST)
    out = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    out.alpha_composite(crop, ((W - size[0]) // 2, H - size[1]))
    return out

def indexed_sprite(sprite):
    alpha = sprite.getchannel("A")
    rgb = Image.new("RGB", sprite.size, (0, 0, 0))
    rgb.paste(sprite.convert("RGB"), mask=alpha)
    quant = rgb.quantize(colors=COLORS, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    raw = quant.getpalette()[:COLORS * 3]
    palette = [tuple(raw[i:i+3]) for i in range(0, COLORS * 3, 3)]
    indices = [0 if a <= 24 else q + 1 for q, a in zip(quant.getdata(), alpha.getdata())]
    return palette, indices

def main():
    src = Image.open(SOURCE).convert("RGBA")
    sprites = []
    for i in range(8):
        x0, x1 = round(i * src.width / 8), round((i + 1) * src.width / 8)
        sprites.append(fit_sprite(src.crop((x0, 0, x1, src.height))))

    sheet = Image.new("RGBA", (W * 8, H), (0, 0, 0, 0))
    palettes, all_indices = [], []
    for i, sprite in enumerate(sprites):
        sheet.alpha_composite(sprite, (i * W, 0))
        palette, indices = indexed_sprite(sprite)
        palettes.append(palette); all_indices.extend(indices)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(OUT, optimize=True)

    lines = ["#pragma once", "#include <Arduino.h>", "",
             f"#define KANTO_LEADER_W {W}", f"#define KANTO_LEADER_H {H}", ""]
    lines.append(f"static const uint16_t KANTO_LEADER_PALETTES[8][{COLORS}] PROGMEM = {{")
    for palette in palettes:
        lines.append("  { " + ", ".join(f"0x{rgb565(c):04X}" for c in palette) + " },")
    lines.append("};\n")
    lines.append(f"static const uint8_t KANTO_LEADER_PIXELS[{len(all_indices)}] PROGMEM = {{")
    for i in range(0, len(all_indices), W):
        lines.append("  " + ", ".join(str(v) for v in all_indices[i:i+W]) + ",")
    lines.append("};")
    HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(OUT)
    print(HEADER)

if __name__ == "__main__":
    main()
