#!/usr/bin/env python3
"""Découpe les bases HGSS et produit six décors de combat RGB565/RLE."""
from pathlib import Path
from PIL import Image
import hashlib
import json

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "battle-bases-hgss.png"
OUTPUT = ROOT / "battle_bases.h"
PREVIEW = ROOT / "assets" / "battle-bases-preview.png"
FINAL_PREVIEW = ROOT / "assets" / "battle-combat-final-preview.png"
AUDIT_DATA = ROOT / "assets" / "battle-bases-audit.json"

# biome: (nom, rangée grande base joueur, colonne/rangée petite base adverse)
SETS = [
    ("HERBE",   0, 0, 0),
    ("EAU",     1, 1, 0),
    ("SABLE",   2, 2, 0),
    ("VOLCAN", 11, 3, 2),
    ("ROCHE",   4, 0, 1),
    ("NEIGE",   5, 1, 1),
]

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
    source = list(part.getdata())
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
    for r, g, b, a in image.getdata():
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

sheet = Image.open(SOURCE).convert("RGB")
if sheet.size != (816, 656):
    raise SystemExit(f"Planche attendue 816x656, reçue {sheet.size}")

scenes = []
layers = []
audit = []
for name, player_row, enemy_col, enemy_row in SETS:
    scene = Image.new("RGBA", (256, 112), (0, 0, 0, 0))
    player = crop_base(sheet, (8, 8 + player_row * 48, 264, 48 + player_row * 48))
    # Le premier plan original fait 256 px de large. Réduit à 176 px pour
    # rester élégant dans le cercle sans traverser tout l'écran.
    player = player.resize((176, 40), Image.Resampling.NEAREST)
    enemy_x = 272 + enemy_col * 136
    # La grille adverse commence à Y=16 et a un pas vertical réel de 72 px.
    # Une autre valeur coupe l'ovale ou prélève un décor dans la rangée voisine.
    enemy_y = 16 + enemy_row * 72
    enemy = crop_base(sheet, (enemy_x, enemy_y, enemy_x + 128, enemy_y + 48))
    enemy_source_bbox = enemy.getbbox()
    paste_on_fixed_baseline(scene, enemy, 128, 48)
    paste_on_fixed_baseline(scene, player, 0, 108)
    scenes.append((name, scene, *encode(scene)))
    player_layer=Image.new("RGBA",(256,112),(0,0,0,0))
    enemy_layer=Image.new("RGBA",(256,112),(0,0,0,0))
    paste_on_fixed_baseline(player_layer,player,0,108)
    paste_on_fixed_baseline(enemy_layer,enemy,128,48)
    layers.append((name,player_layer,enemy_layer))
    audit.append({
        "style": name,
        "player_row": player_row,
        "enemy_col": enemy_col,
        "enemy_row": enemy_row,
        "enemy_source_bbox": list(enemy_source_bbox or ()),
        "player_bbox": list(player_layer.getbbox() or ()),
        "enemy_bbox": list(enemy_layer.getbbox() or ()),
        "player_sha1": hashlib.sha1(player_layer.tobytes()).hexdigest(),
        "enemy_sha1": hashlib.sha1(enemy_layer.tobytes()).hexdigest(),
        "enemy_bottom_pixels": sum(1 for px in enemy_layer.crop((0,47,256,48)).getdata() if px[3]),
    })

AUDIT_DATA.write_text(json.dumps(audit, indent=2), encoding="utf-8")

preview = Image.new("RGBA", (512, 336), (232, 243, 217, 255))
for index, (_, scene, _, _) in enumerate(scenes):
    preview.alpha_composite(scene, ((index % 2) * 256, (index // 2) * 112))
preview.save(PREVIEW)

# Audit visuel composé avec les fonds DP de jour, au format exact de la zone combat.
dp_path=ROOT / "assets" / "battle-backgrounds-dp.png"
if dp_path.exists():
    dp=Image.open(dp_path).convert("RGBA")
    bg_rects=[(15,566),(17,17),(19,840),(133,1110),(16,300),(21,1387)]
    final=Image.new("RGBA",(932,960),(255,255,255,255))
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
