#!/usr/bin/env python3
"""Affecte et audite individuellement un sol aux 386 Pokémon."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEX = ROOT / "dex.h"
HEADER = ROOT / "battle_ground_map.h"
REPORT = ROOT / "assets" / "battle-ground-map-386.csv"

STYLES = ("PLANTE", "EAU", "FEU", "NORMAL", "ROCHE", "GLACE", "NUAGES")

def style_for(type1, type2):
    types = {type1, type2}
    if "TYPE_ICE" in types:
        return 5
    if "TYPE_WATER" in types:
        return 1
    if "TYPE_FIRE" in types:
        return 2
    if types & {"TYPE_ROCK", "TYPE_GROUND", "TYPE_STEEL"}:
        return 4
    if types & {"TYPE_GRASS", "TYPE_BUG"}:
        return 0
    if "TYPE_FLYING" in types:
        return 6
    return 3

pattern = re.compile(
    r'^\s*\{\s*"([^"]+)",.*?,\s*(TYPE_[A-Z]+),\s*(TYPE_[A-Z]+),\s*\d+\s*\},\s*//\s*(\d+)',
    re.M,
)
entries = []
for name, type1, type2, dex_text in pattern.findall(DEX.read_text(encoding="utf-8")):
    dex = int(dex_text)
    if dex:
        entries.append((dex, name, type1, type2, style_for(type1, type2)))

if [item[0] for item in entries] != list(range(1, 387)):
    raise SystemExit(f"FAIL MAPPING SOLS: {len(entries)} entrées ou numéros de dex non continus")

# Cas représentatifs contrôlés explicitement, y compris les doubles types.
representatives = {
    "VENUSAUR": 0, "BLASTOISE": 1, "CHARIZARD": 2, "SNORLAX": 3,
    "GOLEM": 4, "ARTICUNO": 5, "PIDGEOT": 6,
    "GYARADOS": 1, "AERODACTYL": 4, "BUTTERFREE": 0,
}
by_name = {name: style for _, name, _, _, style in entries}
for name, wanted in representatives.items():
    if by_name.get(name) != wanted:
        raise SystemExit(f"FAIL MAPPING SOLS: {name}={by_name.get(name)}, attendu {wanted}")

values = [3] + [item[4] for item in entries]
lines = [
    "#pragma once",
    "#include <Arduino.h>",
    "// Généré par tools/generate_battle_ground_map.py : 0 plante, 1 eau,",
    "// 2 feu, 3 normal, 4 roche, 5 glace, 6 nuages.",
    "static const uint8_t BATTLE_GROUND_BY_DEX[387] PROGMEM = {",
]
for start in range(0, len(values), 32):
    lines.append("  " + ",".join(str(value) for value in values[start:start + 32]) + ",")
lines.append("};")
HEADER.write_text("\n".join(lines) + "\n", encoding="utf-8")

with REPORT.open("w", encoding="utf-8", newline="") as handle:
    writer = csv.writer(handle, delimiter=";")
    writer.writerow(("dex", "pokemon", "type1", "type2", "sol", "index"))
    for dex, name, type1, type2, style in entries:
        writer.writerow((dex, name, type1.removeprefix("TYPE_"),
                         type2.removeprefix("TYPE_"), STYLES[style], style))

counts = {name: 0 for name in STYLES}
for *_, style in entries:
    counts[STYLES[style]] += 1
print("AUDIT AFFECTATION 386 OK")
print(" - " + ", ".join(f"{name.lower()}={count}" for name, count in counts.items()))
print(" - rapport:", REPORT)
