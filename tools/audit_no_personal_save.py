#!/usr/bin/env python3
"""Bloque toute publication accidentelle d'une sauvegarde personnelle."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
forbidden = []

for path in ROOT.rglob("*"):
    if not path.is_file():
        continue
    rel = path.relative_to(ROOT).as_posix().lower()
    parts = rel.split("/")

    # Les pNNN.bin de tools/sdcard/mons sont des sprites PMD et sont autorises.
    if "save" in parts or "saves" in parts or "profiles" in parts:
        forbidden.append(rel)
        continue
    if path.name.lower() in {
        "poketama.bin", "poketama.bak", "poketama.tmp",
        "nvs.bin", "nvs_dump.bin", "preferences.bin",
    }:
        forbidden.append(rel)

if forbidden:
    print("ECHEC CONFIDENTIALITE : sauvegarde personnelle detectee :")
    for rel in forbidden:
        print(" -", rel)
    raise SystemExit(1)

web = ROOT / "web"
published = [p.relative_to(web).as_posix() for p in web.rglob("*") if p.is_file()]
if any(name.startswith("save/") or "/profiles/" in name for name in published):
    raise SystemExit("ECHEC CONFIDENTIALITE : le Web Flasher contient une sauvegarde")

print("OK CONFIDENTIALITE : aucune sauvegarde personnelle dans le depot ou le Web Flasher")
print("OK MISE A JOUR : chaque appareil conserve uniquement son propre dossier /save sur sa microSD")
