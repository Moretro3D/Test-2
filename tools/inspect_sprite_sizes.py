#!/usr/bin/env python3
"""Extrait et rend les sprites d'une liste Dex depuis web/sprites.pak."""

from pathlib import Path
import struct
import sys
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
PAK = ROOT / "web" / "sprites.pak"
OUT = ROOT / "sprite-audit"
DEXES = [int(x) for x in sys.argv[1:]] or [152, 153, 154, 158, 159, 160]


def unpack_pak(path):
    raw = path.read_bytes()
    if raw[:4] != b"TPAK":
        raise ValueError("TPAK invalide")
    count = struct.unpack_from("<H", raw, 4)[0]
    pos = 6
    entries = []
    for _ in range(count):
        size_name = raw[pos]
        pos += 1
        name = raw[pos:pos + size_name].decode("utf-8")
        pos += size_name
        size = struct.unpack_from("<I", raw, pos)[0]
        pos += 4
        entries.append((name, size))
    files = {}
    for name, size in entries:
        files[name] = raw[pos:pos + size]
        pos += size
    return files


def idle(buf):
    if buf[:4] != b"TPK2":
        raise ValueError("TPK2 invalide")
    actions = buf[4]
    palette_count = struct.unpack_from("<H", buf, 5)[0]
    palette = struct.unpack_from(f"<{palette_count}H", buf, 7)
    pos = 7 + palette_count * 2
    for _ in range(actions):
        action, width, height, frames = buf[pos:pos + 4]
        pos += 4
        pos += frames * 2
        pixels = width * height
        if action == 0:
            return width, height, palette, buf[pos:pos + pixels]
        pos += pixels * frames
    raise ValueError("Idle absent")


def to_rgb565(color):
    return ((color >> 11) * 255 // 31,
            ((color >> 5) & 63) * 255 // 63,
            (color & 31) * 255 // 31,
            255)


files = unpack_pak(PAK)
OUT.mkdir(exist_ok=True)
thumbs = files["mons/thumbs.bin"]


def thumbnail(dex):
    if thumbs[:4] != b"TPTH":
        raise ValueError("TPTH invalide")
    offset = struct.unpack_from("<I", thumbs, 6 + (dex - 1) * 4)[0]
    width, height, palette_count = thumbs[offset:offset + 3]
    palette = struct.unpack_from(f"<{palette_count}H", thumbs, offset + 3)
    start = offset + 3 + palette_count * 2
    return width, height, palette, thumbs[start:start + width * height]


cards = []
for dex in DEXES:
    key = f"mons/p{dex:03d}.bin"
    local = ROOT / "tools" / "sdcard" / "mons" / f"p{dex:03d}.bin"
    has_pmd = local.is_file() or key in files
    source = "PMD" if has_pmd else "miniature"
    pmd_data = local.read_bytes() if local.is_file() else files.get(key)
    width, height, palette, pixels = idle(pmd_data) if has_pmd else thumbnail(dex)
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    data = []
    for index in pixels:
        data.append((0, 0, 0, 0) if index == 0xFF else to_rgb565(palette[index]))
    image.putdata(data)
    bounds = image.getchannel("A").getbbox()
    visible = image.crop(bounds)
    visible.thumbnail((150, 150), Image.Resampling.NEAREST)
    card = Image.new("RGBA", (190, 205), "white")
    card.alpha_composite(visible, ((190 - visible.width) // 2, 15 + (150 - visible.height)))
    draw = ImageDraw.Draw(card)
    vw, vh = bounds[2] - bounds[0], bounds[3] - bounds[1]
    draw.text((10, 172), f"#{dex:03d} {source} {width}x{height}", fill="black")
    draw.text((10, 188), f"visible {vw}x{vh}", fill="black")
    card.save(OUT / f"{dex:03d}.png")
    cards.append(card)
    print(f"#{dex:03d}: cadre={width}x{height}, visible={vw}x{vh}, bbox={bounds}")

sheet = Image.new("RGBA", (190 * len(cards), 205), (225, 230, 240, 255))
for i, card in enumerate(cards):
    sheet.alpha_composite(card, (i * 190, 0))
sheet.save(OUT / "johto-starters-contact.png")
print(OUT / "johto-starters-contact.png")
