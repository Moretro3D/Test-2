#!/usr/bin/env python3
from pathlib import Path
import json
import sys
import struct

root = Path(__file__).resolve().parents[1]
web = root / "web"
manifest_path = web / "manifest.json"
index_path = web / "index.html"
sprites_path = web / "sprites.pak"
music_path = web / "music.pak"
cries_installer_path = web / "cries-installer.js"

errors = []

def require(path, label, min_size=1):
    if not path.is_file():
        errors.append(f"{label} absent: {path.relative_to(root)}")
        return
    if path.stat().st_size < min_size:
        errors.append(f"{label} trop petit/vide: {path.relative_to(root)} ({path.stat().st_size} octets)")

require(index_path, "index.html", 100)
require(manifest_path, "manifest.json", 50)
require(sprites_path, "sprites.pak", 1024)
require(music_path, "music.pak", 1000000)
require(cries_installer_path, "cries-installer.js", 1000)

if sprites_path.is_file():
    raw = sprites_path.read_bytes()
    try:
        if raw[:4] != b"TPAK":
            raise ValueError("signature TPAK absente")
        count = struct.unpack_from("<H", raw, 4)[0]
        off = 6
        names = []
        for _ in range(count):
            name_len = raw[off]
            off += 1
            names.append(raw[off:off + name_len].decode("utf-8"))
            off += name_len + 4
        for required in ("music/morning.wav", "music/lofi.wav", "music/night.wav"):
            if required not in names:
                errors.append(f"asset automatique absent du bundle: {required}")
        if not any(name.startswith("backgrounds/") for name in names):
            errors.append("decors absents du bundle automatique")
    except Exception as exc:
        errors.append(f"sprites.pak invalide: {exc}")

try:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
except Exception as exc:
    errors.append(f"manifest.json invalide: {exc}")
    manifest = {}

builds = manifest.get("builds", [])
if len(builds) != 1:
    errors.append(f"manifest: 1 build attendu, trouvé {len(builds)}")
else:
    build = builds[0]
    if build.get("chipFamily") != "ESP32-S3":
        errors.append(f"manifest chipFamily incorrect: {build.get('chipFamily')!r}")
    parts = build.get("parts", [])
    expected_offsets = [0, 32768, 57344, 65536]
    offsets = [p.get("offset") for p in parts]
    if offsets != expected_offsets:
        errors.append(f"offsets manifest incorrects: {offsets} != {expected_offsets}")
    for part in parts:
        rel = part.get("path", "")
        if not rel:
            errors.append("manifest: part sans path")
            continue
        require(web / rel, f"firmware {rel}", 512)

if index_path.is_file():
    html = index_path.read_text(encoding="utf-8", errors="replace")
    if 'manifest="manifest.json' not in html:
        errors.append("index.html ne référence pas manifest.json")
    if "esp-web-tools" not in html:
        errors.append("index.html ne charge pas ESP Web Tools")
    if html.count('id="auto"') != 1:
        errors.append("bouton unique d'installation microSD absent ou dupliqué")
    if 'id="music"' in html or 'id="cries"' in html:
        errors.append("anciens boutons séparés musique/cris encore présents")
    if "sprites.pak?" not in html:
        errors.append("bundle sprites, décors et OST absent de l'installation complète")
    if "installAuthenticCries(criesOptions())" not in html:
        errors.append("386 cris absents de l'installation complète")

if errors:
    print("ECHEC VERIFICATION WEB:")
    for err in errors:
        print(" -", err)
    sys.exit(1)

print("VERIFICATION WEB OK")
print(" - index.html OK")
print(" - manifest.json OK")
print(" - ESP32-S3 OK")
print(" - offsets firmware OK")
print(" - 4 binaires firmware OK")
print(" - sprites.pak OK")
print(" - bouton microSD unique OK")
print(" - sprites, décors, OST et 386 cris installés ensemble")
