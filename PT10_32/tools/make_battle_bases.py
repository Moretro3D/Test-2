#!/usr/bin/env python3
"""Produit les sept familles de sols de combat RGB565/RLE.

Chaque source contient une plateforme adverse complète dans sa moitié haute et
un demi-sol joueur dans sa moitié basse. Le générateur normalise leur taille et
leur position afin qu'aucun type ne fasse monter ou descendre les Pokémon.
"""
from pathlib import Path
from PIL import Image
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "battle_bases.h"
PREVIEW = ROOT / "assets" / "battle-bases-preview.png"
FINAL_PREVIEW = ROOT / "assets" / "battle-combat-final-preview.png"
AUDIT_DATA = ROOT / "assets" / "battle-bases-audit.json"
SETS = [
    ("PLANTE",  ROOT / "assets" / "battle_ground_grass_v10_32.png"),
    ("EAU",     ROOT / "assets" / "battle_ground_water_v10_32.png"),
    ("FEU",     ROOT / "assets" / "battle_ground_fire_v10_32.png"),
    ("NORMAL",  ROOT / "assets" / "battle_ground_normal_v10_32.png"),
    ("ROCHE",   ROOT / "assets" / "battle_ground_rock_v10_32.png"),
    ("GLACE",   ROOT / "assets" / "battle_ground_ice_v10_32.png"),
    ("NUAGES",  ROOT / "assets" / "battle_ground_cloud_v10_32.png"),
]

ENEMY_BASELINE = 48
PLAYER_BASELINE = 108

def flat_pixels(image):
    """Compatibilité Pillow actuelle et future, sans avertissement getdata()."""
    if hasattr(image, "get_flattened_data"):
        return image.get_flattened_data()
    return image.getdata()

def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

def crop_base(sheet, box):
    part = sheet.crop(box).convert("RGBA")
    bg = part.getpixel((0, 0))[:3]
    sheet_backgrounds = {bg, (255,255,255), (248,248,248), (216,216,248), (184,192,248)}
    # Ne retire que le fond relié aux bords. Certaines bases utilisent elles-mêmes
    # du blanc ou du mauve clair : les supprimer partout produisait une palette vide.
    width, height = part.size
    source = list(flat_pixels(part))
    transparent = set()
    pending = []
    for x in range(width):
        pending.extend(((x, 0), (x, height - 1)))
    for y in range(height):
        pending.extend(((0, y), (width - 1, y)))
    while pending:
        x, y = pending.pop()
        pos = y * width + x
        if pos in transparent or source[pos][:3] not in sheet_backgrounds:
            continue
        transparent.add(pos)
        if x: pending.append((x - 1, y))
        if x + 1 < width: pending.append((x + 1, y))
        if y: pending.append((x, y - 1))
        if y + 1 < height: pending.append((x, y + 1))
    pixels = []
    for pos, (r, g, b, _) in enumerate(source):
        pixels.append((r, g, b, 0 if pos in transparent else 255))
    part.putdata(pixels)
    return part

def paste_on_fixed_baseline(scene, part, x, baseline):
    """Colle le dessin visible sur une base Y fixe, quelle que soit sa marge source."""
    bbox = part.getbbox()
    if not bbox:
        raise SystemExit("Sol vide impossible à aligner")
    visible = part.crop(bbox)
    scene.alpha_composite(visible, (x + bbox[0], baseline - visible.height))

def encode(image):
    colors = []
    lookup = {}
    values = []
    for r, g, b, a in flat_pixels(image):
        if not a:
            values.append(255)
            continue
        color = (r, g, b)
        if color not in lookup:
            lookup[color] = len(colors)
            colors.append(color)
        values.append(lookup[color])
    if not colors:
        raise SystemExit("Palette vide : découpe ou transparence du sol incorrecte")
    if len(colors) > 254:
        raise SystemExit(f"Palette trop grande: {len(colors)}")
    runs = []
    previous, count = values[0], 0
    for value in values:
        if value == previous and count < 255:
            count += 1
        else:
            runs.extend((count, previous))
            previous, count = value, 1
    runs.extend((count, previous))
    return colors, runs

scenes = []
layers = []
audit = []
for name, source_path in SETS:
    if not source_path.exists():
        raise SystemExit(f"Source de sol absente: {source_path}")
    scene = Image.new("RGBA", (256, 112), (0, 0, 0, 0))
    custom = Image.open(source_path).convert("RGBA").resize((256, 112), Image.Resampling.NEAREST)
    alpha = custom.getchannel("A").point(lambda value: 255 if value >= 96 else 0)
    rgb = Image.new("RGB", custom.size, (0, 0, 0))
    rgb.paste(custom.convert("RGB"), mask=alpha)
    custom = rgb.quantize(colors=31, method=Image.Quantize.MEDIANCUT,
                          dither=Image.Dither.NONE).convert("RGBA")
    custom.putalpha(alpha)
    upper = custom.crop((0, 0, 256, 56))
    lower = custom.crop((0, 56, 256, 112))
    upper_box, lower_box = upper.getbbox(), lower.getbbox()
    if not upper_box or not lower_box:
        raise SystemExit(f"Plateformes {name} incomplètes")
    enemy_art = upper.crop(upper_box).resize((120, 40), Image.Resampling.NEAREST)
    player_art = lower.crop(lower_box).resize((168, 36), Image.Resampling.NEAREST)
    enemy = Image.new("RGBA", (128, 48), (0, 0, 0, 0))
    player = Image.new("RGBA", (176, 40), (0, 0, 0, 0))
    enemy.alpha_composite(enemy_art, (4, 4))
    player.alpha_composite(player_art, (4, 2))
    enemy_source_bbox = enemy.getbbox()
    paste_on_fixed_baseline(scene, enemy, 128, ENEMY_BASELINE)
    paste_on_fixed_baseline(scene, player, 0, PLAYER_BASELINE)
    scenes.append((name, scene, *encode(scene)))
    player_layer=Image.new("RGBA",(256,112),(0,0,0,0))
    enemy_layer=Image.new("RGBA",(256,112),(0,0,0,0))
    paste_on_fixed_baseline(player_layer,player,0,PLAYER_BASELINE)
    paste_on_fixed_baseline(enemy_layer,enemy,128,ENEMY_BASELINE)
    layers.append((name,player_layer,enemy_layer))
    audit.append({
        "style": name,
        "source": source_path.name,
        "enemy_source_bbox": list(enemy_source_bbox or ()),
        "player_bbox": list(player_layer.getbbox() or ()),
        "enemy_bbox": list(enemy_layer.getbbox() or ()),
        "player_sha1": hashlib.sha1(player_layer.tobytes()).hexdigest(),
        "enemy_sha1": hashlib.sha1(enemy_layer.tobytes()).hexdigest(),
        "enemy_bottom_pixels": sum(1 for px in flat_pixels(enemy_layer.crop((0,47,256,48))) if px[3]),
    })

AUDIT_DATA.write_text(json.dumps(audit, indent=2), encoding="utf-8")

preview = Image.new("RGBA", (512, 448), (232, 243, 217, 255))
for index, (_, scene, _, _) in enumerate(scenes):
    preview.alpha_composite(scene, ((index % 2) * 256, (index // 2) * 112))
preview.save(PREVIEW)

# Audit visuel composé avec les fonds DP de jour, au format exact de la zone combat.
dp_path=ROOT / "assets" / "battle-backgrounds-dp.png"
if dp_path.exists():
    dp=Image.open(dp_path).convert("RGBA")
    bg_rects=[(15,566),(17,17),(133,1110),(19,840),(16,300),(21,1387),(15,566)]
    final=Image.new("RGBA",(932,1280),(255,255,255,255))
    for index,(_,scene,_,_) in enumerate(scenes):
        bx,by=bg_rects[index]
        bg=dp.crop((bx,by,bx+256,by+144)).resize((512,320),Image.Resampling.NEAREST)
        ground=scene.resize((512,224),Image.Resampling.NEAREST)
        bg.alpha_composite(ground,(0,102))
        framed=bg.crop((23,0,489,320))
        final.alpha_composite(framed,((index%2)*466,(index//2)*320))
    final.save(FINAL_PREVIEW)

lines = ["#pragma once", "#include <Arduino.h>",
         "static constexpr uint16_t BATTLE_BASE_W=256;",
         "static constexpr uint16_t BATTLE_BASE_H=112;"]
for index,(name,player_layer,enemy_layer) in enumerate(layers):
    for side,image in (("P",player_layer),("E",enemy_layer)):
        colors,runs=encode(image)
        stem=f"BATTLE_BASE_{side}_{index}"
        lines += [f"// Style {index}: {name}, côté {side}",
                  f"static const uint16_t {stem}_PAL[{len(colors)}] PROGMEM={{",
                  "  "+",".join(f"0x{rgb565(c):04X}" for c in colors)+",","};",
                  f"static const uint8_t {stem}_RLE[{len(runs)}] PROGMEM={{"]
        for start in range(0,len(runs),24):
            lines.append("  "+",".join(str(v) for v in runs[start:start+24]) + ",")
        lines += ["};",""]
OUTPUT.write_text("\n".join(lines), encoding="utf-8")
print("Décors:", ", ".join(name for name, *_ in scenes))
print("Header:", OUTPUT, OUTPUT.stat().st_size, "octets")
print("Aperçu:", PREVIEW)
if FINAL_PREVIEW.exists(): print("Aperçu final:",FINAL_PREVIEW)
