#!/usr/bin/env python3
"""Audit géométrique des sept paires de sols de combat."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
data = json.loads((ROOT / "assets" / "battle-bases-audit.json").read_text(encoding="utf-8"))
expected = [
    ("PLANTE", "battle_ground_grass_v10_32.png"),
    ("EAU", "battle_ground_water_v10_32.png"),
    ("FEU", "battle_ground_fire_v10_32.png"),
    ("NORMAL", "battle_ground_normal_v10_32.png"),
    ("ROCHE", "battle_ground_rock_v10_32.png"),
    ("GLACE", "battle_ground_ice_v10_32.png"),
    ("NUAGES", "battle_ground_cloud_v10_32.png"),
]
if len(data) != 7:
    raise SystemExit(f"FAIL SOLS: {len(data)} styles au lieu de 7")
for item, wanted in zip(data, expected):
    got = (item["style"], item["source"])
    if got != wanted:
        raise SystemExit(f"FAIL SOLS: mapping {got}, attendu {wanted}")
    pb, eb = item["player_bbox"], item["enemy_bbox"]
    if not pb or not eb:
        raise SystemExit(f"FAIL SOLS: {item['style']} contient une plateforme vide")
    if pb[3] != 108 or eb[3] != 48:
        raise SystemExit(f"FAIL SOLS: {item['style']} bouge verticalement joueur={pb} adverse={eb}")
    if pb[2] - pb[0] != 150 or pb[3] - pb[1] != 32:
        raise SystemExit(f"FAIL SOLS: demi-sol joueur {item['style']} mal dimensionné {pb}")
    source = item["enemy_source_bbox"]
    if source[0] <= 0 or source[1] <= 0 or source[2] >= 128 or source[3] >= 48:
        raise SystemExit(f"FAIL SOLS: ovale adverse {item['style']} rogné {source}")
    if item["enemy_bottom_pixels"] < 8:
        raise SystemExit(f"FAIL SOLS: bord inférieur adverse {item['style']} incomplet")
if len({x["player_sha1"] for x in data}) != 7 or len({x["enemy_sha1"] for x in data}) != 7:
    raise SystemExit("FAIL SOLS: deux familles utilisent encore le même visuel")
print("AUDIT SOLS OK")
print(" - plante, eau, feu, normal, roche, glace et nuages distincts")
print(" - sept demi-sols joueur alignés exactement sur Y=108")
print(" - demi-sols joueur compacts de 150x32 pixels source")
print(" - sept plateformes adverses complètes alignées exactement sur Y=48")
