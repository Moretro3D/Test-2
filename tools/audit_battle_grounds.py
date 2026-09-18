#!/usr/bin/env python3
"""Audit sémantique et visuel des six paires de sols de combat."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
data = json.loads((ROOT / "assets" / "battle-bases-audit.json").read_text(encoding="utf-8"))
expected = [
    ("HERBE", 0, 0, 0),
    ("EAU", 1, 1, 0),
    ("SABLE", 2, 2, 0),
    ("VOLCAN", 11, 3, 2),
    ("ROCHE", 4, 0, 1),
    ("NEIGE", 5, 1, 1),
]
if len(data) != 6:
    raise SystemExit(f"FAIL SOLS: {len(data)} styles au lieu de 6")
for item, wanted in zip(data, expected):
    got = (item["style"], item["player_row"], item["enemy_col"], item["enemy_row"])
    if got != wanted:
        raise SystemExit(f"FAIL SOLS: mapping {got}, attendu {wanted}")
    pb, eb = item["player_bbox"], item["enemy_bbox"]
    if not pb or not eb:
        raise SystemExit(f"FAIL SOLS: {item['style']} contient une plateforme vide")
    if pb[3] != 108 or eb[3] != 48:
        raise SystemExit(f"FAIL SOLS: {item['style']} bouge verticalement joueur={pb} adverse={eb}")
    source = item["enemy_source_bbox"]
    if source[0] <= 0 or source[1] <= 0 or source[2] >= 128 or source[3] >= 48:
        raise SystemExit(f"FAIL SOLS: ovale adverse {item['style']} rogne par la decoupe {source}")
    if item["enemy_bottom_pixels"] < 8:
        raise SystemExit(f"FAIL SOLS: bord inferieur adverse {item['style']} incomplet")
if len({x["player_sha1"] for x in data}) != 6 or len({x["enemy_sha1"] for x in data}) != 6:
    raise SystemExit("FAIL SOLS: deux biomes utilisent encore le même visuel")
print("AUDIT SOLS OK")
print(" - herbe, eau, sable, volcan, roche et neige distincts")
print(" - plateformes joueur alignées sur Y=108")
print(" - plateformes adverses complètes alignées sur Y=48")
print(" - six ovales adverses fermes, sans bord rogne")
