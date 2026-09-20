#!/usr/bin/env python3
"""Vérifie la taille simulée des 386 silhouettes PMD en combat."""
from pathlib import Path
import argparse
import csv
import struct

parser=argparse.ArgumentParser()
parser.add_argument("--partial",action="store_true",help="audit local des fichiers deja presents")
args=parser.parse_args()

ROOT=Path(__file__).resolve().parents[1]
raw=(ROOT/"web/sprites.pak").read_bytes()
if raw[:4]!=b"TPAK": raise SystemExit("FAIL SPRITES: sprites.pak invalide")
count=struct.unpack_from("<H",raw,4)[0]
pos=6; entries=[]
for _ in range(count):
    n=raw[pos]; pos+=1
    name=raw[pos:pos+n].decode(); pos+=n
    size=struct.unpack_from("<I",raw,pos)[0]; pos+=4
    entries.append((name,size))
files={}
for name,size in entries:
    files[name]=raw[pos:pos+size]; pos+=size

def idle_bounds(buf):
    palettes=struct.unpack_from("<H",buf,5)[0]
    p=7+palettes*2
    for _ in range(buf[4]):
        action,w,h,frames=buf[p:p+4]; p+=4+frames*2
        frame_size=w*h
        if action==0:
            pix=buf[p:p+frame_size]
            points=[(i%w,i//w) for i,v in enumerate(pix) if v!=255]
            if not points: return None
            xs=[q[0] for q in points]; ys=[q[1] for q in points]
            return max(xs)-min(xs)+1,max(ys)-min(ys)+1
        p+=frame_size*frames
    return None

available=[dex for dex in range(1,387) if f"mons/p{dex:03d}.bin" in files]
if not args.partial and available != list(range(1,387)):
    missing=sorted(set(range(1,387))-set(available))
    raise SystemExit(f"FAIL SPRITES: PMD absents: {missing[:12]} ({len(missing)} manquants)")
if args.partial and len(available)<151:
    raise SystemExit(f"FAIL SPRITES: seulement {len(available)} PMD disponibles")

sizes={}
rows=[]
for dex in available:
    key=f"mons/p{dex:03d}.bin"
    if key not in files: raise SystemExit(f"FAIL SPRITES: {key} absent")
    bounds=idle_bounds(files[key])
    if not bounds: raise SystemExit(f"FAIL SPRITES: #{dex:03d} idle vide")
    w,h=bounds
    enemy_target=84*92//100
    player_target=104*92//100
    ew=max(1,w*enemy_target//max(w,h)); eh=max(1,h*enemy_target//max(w,h))
    pw=max(1,w*player_target//max(w,h)); ph=max(1,h*player_target//max(w,h))
    sizes[dex]=(ew,eh,pw,ph)
    if max(ew,eh)!=enemy_target or max(pw,ph)!=player_target:
        raise SystemExit(f"FAIL SPRITES: #{dex:03d} mal normalise: adv {ew}x{eh}, joueur {pw}x{ph}")
    rows.append((dex,w,h,ew,eh,pw,ph))
thumbs=files.get("mons/thumbs.bin",b"")
thumb_count=struct.unpack_from("<H",thumbs,4)[0] if thumbs[:4]==b"TPTH" else 0
required_thumbs=len(available) if args.partial else 386
if thumb_count<required_thumbs:
    raise SystemExit(f"FAIL SPRITES: {thumb_count}/{required_thumbs} miniatures de repli")

report=ROOT/"assets"/"battle-sprite-size-audit.csv"
report.parent.mkdir(exist_ok=True)
with report.open("w",newline="",encoding="utf-8-sig") as fh:
    writer=csv.writer(fh,delimiter=";")
    writer.writerow(("dex","source_largeur","source_hauteur","adversaire_largeur","adversaire_hauteur","joueur_largeur","joueur_hauteur"))
    writer.writerows(rows)

show=((95,"Onix"),(106,"Kicklee"),(130,"Leviator"),(131,"Lokhlass"),(142,"Ptera"),(143,"Ronflex"),
      (144,"Artikodin"),(145,"Electhor"),(146,"Sulfura"),(149,"Dracolosse"),(150,"Mewtwo"),
      (208,"Steelix"),(243,"Raikou"),(244,"Entei"),(245,"Suicune"),(248,"Tyranocif"),(249,"Lugia"),(250,"Ho-Oh"),
      (289,"Monaflemit"),(306,"Galeking"),(321,"Wailord"),(330,"Libegon"),(334,"Altaria"),(350,"Milobellus"),
      (365,"Kaimorse"),(373,"Drattak"),(376,"Metalosse"),(377,"Regirock"),(378,"Regice"),(379,"Registeel"),
      (380,"Latias"),(381,"Latios"),(382,"Kyogre"),(383,"Groudon"),(384,"Rayquaza"),(385,"Jirachi"),(386,"Deoxys"))
for dex,name in show:
    if dex in sizes:
        ew,eh,pw,ph=sizes[dex]
        print(f" - {name} #{dex:03d}: adversaire {ew}x{eh}, joueur {pw}x{ph} px")
mode="partiel local" if args.partial else "integral"
print(f"AUDIT TAILLES COMBAT OK ({mode}): {len(available)} PMD normalises, {thumb_count} replis; rapport {report.name}")
