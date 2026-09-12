#!/usr/bin/env python3
from pathlib import Path
import re
import sys


root = Path(__file__).resolve().parents[1]
source = (root / "i18n.cpp").read_text(encoding="utf-8")
header = (root / "i18n.h").read_text(encoding="utf-8")
ino = (root / "TamaPoke.ino").read_text(encoding="utf-8")
langs = ("ES", "EN", "FR", "DE", "IT", "PT")
counts = []

for index, lang in enumerate(langs):
    start = source.index(f"// ---------------- {lang} ----------------")
    end = source.index(f"// ---------------- {langs[index + 1]} ----------------", start) if index + 1 < len(langs) else source.index("};", start)
    strings = re.findall(r'"(?:\\.|[^"\\])*"', source[start:end])
    counts.append(len(strings))
    print(f"{lang}: {len(strings)} traductions")

if len(set(counts)) != 1:
    raise SystemExit(f"FAIL: tables de tailles différentes {dict(zip(langs, counts))}")

required_ids = (
    "S_CHOOSE_GENERATION", "S_NORMAL_ATTACK", "S_HP", "S_SOUND_LABEL",
    "S_LOCAL_PLAY", "S_LAN_HOST", "S_LAN_TRADE", "S_LAN_BATTLE",
)
missing = [item for item in required_ids if item not in header or f"T({item})" not in ino and item.startswith(("S_CHOOSE", "S_NORMAL", "S_HP", "S_SOUND"))]
if missing:
    raise SystemExit("FAIL IDs/traductions non utilisés: " + ", ".join(missing))

for forbidden in ("CHOISIS TA GENERATION", "TOUCHE UNE POKEBALL", "TON STARTER", 'print("PV")', '"PUISSANTE"', '"AFFICHAGE"'):
    if forbidden in ino:
        raise SystemExit(f"FAIL texte UI codé en dur: {forbidden}")

print("AUDIT TRADUCTIONS 6 LANGUES OK")
