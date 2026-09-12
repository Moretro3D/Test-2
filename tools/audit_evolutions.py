#!/usr/bin/env python3
from pathlib import Path
import re, sys

ROOT=Path(__file__).resolve().parents[1]
dex=(ROOT/"dex.h").read_text(encoding="utf-8")
pet=(ROOT/"pet.cpp").read_text(encoding="utf-8")
ino=(ROOT/"TamaPoke.ino").read_text(encoding="utf-8")
SPECIAL={25,26,31,34,35,36,38,39,40,45,59,62,65,68,71,76,91,103,113,121,122,134,135,136,143,169,176,182,183,185,186,192,196,197,199,208,212,226,230,233,242,272,275,301,315,350,358,367,368}

rx=re.compile(r'\{\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*([A-Z_]+),.*?\},\s*//\s*(\d+)')
E={}
for m in rx.finditer(dex):
    name,to,lvl,rar,num=m.groups()
    E[int(num)]=(name,int(to),int(lvl))
if len(E)!=386:
    raise SystemExit(f"FAIL: {len(E)} entrées au lieu de 386")

if E[93][1:] != (94,40):
    raise SystemExit("FAIL: Spectrum doit evoluer en Ectoplasma au niveau 40")

for sid,(name,to,lvl) in E.items():
    if to<0 or to>386 or to==sid:
        raise SystemExit(f"FAIL cible {sid} {name} -> {to}")
    if to in SPECIAL and lvl!=0:
        raise SystemExit(f"FAIL spéciale avec faux niveau: {sid} {name} -> {to} niv {lvl}")
    if lvl>100:
        raise SystemExit(f"FAIL niveau impossible: {sid} {name} niv {lvl}")

# Pas de cycles.
for sid in E:
    seen=set()
    cur=sid
    while E[cur][1]:
        if cur in seen:
            raise SystemExit(f"FAIL cycle depuis {sid}")
        seen.add(cur)
        cur=E[cur][1]

# Pas de double évolution immédiate dans une chaîne de niveaux.
for sid in E:
    cur=sid
    previous=None
    seen=set()
    while cur not in seen:
        seen.add(cur)
        name,to,lvl=E[cur]
        if not to or lvl==0:
            break
        if previous is not None and lvl<=previous:
            raise SystemExit(f"FAIL seuil non croissant: chaîne {sid}, {cur} niv {lvl} <= {previous}")
        previous=lvl
        cur=to

combined=pet+"\n"+ino
if re.search(r'evolveLevel\s*\+\s*(?:pet\.)?careMistakes',combined):
    raise SystemExit("FAIL: careMistakes modifie encore un niveau d'évolution")
if "if (d.evolvesTo == 0 || d.evolveLevel == 0) return false;" not in pet:
    raise SystemExit("FAIL: évolutions spéciales encore déclenchables par niveau")

print("AUDIT EVOLUTIONS OK")
print(" - 386 espèces")
print(" - aucun cycle")
print(" - aucun seuil de niveau non croissant")
print(" - aucune évolution spéciale avec faux niveau")
print(" - careMistakes ne change plus jamais le seuil")
