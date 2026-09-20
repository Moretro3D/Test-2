#!/usr/bin/env python3
"""
Audit TamaPoke #001..386.
Échoue volontairement si :
- un sprite normal/shiny manque
- un TPK2 est corrompu
- thumbs.bin n'annonce pas 386
- un type de transport Dex retombe en uint8_t
"""
from pathlib import Path
import struct, sys, re, hashlib

ROOT = Path(__file__).resolve().parents[1]
MONS = ROOT / "tools" / "sdcard" / "mons"

def check_tpk2(path, dex, shiny):
    data = path.read_bytes()
    if len(data) < 7 or data[:4] != b"TPK2":
        raise RuntimeError(f"{path.name}: TPK2 invalide")
    nacts = data[4]
    pal = struct.unpack_from("<H", data, 5)[0]
    if nacts < 1 or nacts > 12 or pal < 1 or pal > 256:
        raise RuntimeError(f"{path.name}: header invalide")
    p = 7 + pal * 2
    idle = False
    for _ in range(nacts):
        if p + 4 > len(data):
            raise RuntimeError(f"{path.name}: header action tronqué")
        aid,w,h,nf = data[p:p+4]
        p += 4
        if w == 0 or h == 0 or nf == 0 or nf > 24:
            raise RuntimeError(f"{path.name}: action invalide")
        need = nf * 2 + w * h * nf
        if p + need > len(data):
            raise RuntimeError(f"{path.name}: données tronquées")
        if aid == 0:
            idle = True
        p += need
    if not idle:
        raise RuntimeError(f"{path.name}: pas d'Idle")

def main():
    missing = []
    hashes_normal = {}
    for dex in range(1, 387):
        for shiny in (False, True):
            p = MONS / f"p{'s' if shiny else ''}{dex:03d}.bin"
            if not p.exists():
                missing.append(p.name)
                continue
            check_tpk2(p, dex, shiny)
            if not shiny:
                h = hashlib.sha256(p.read_bytes()).hexdigest()
                hashes_normal.setdefault(h, []).append(dex)

    if missing:
        raise RuntimeError(f"{len(missing)} sprites manquants: {missing[:20]}")

    # Les duplications exactes entre espèces sont extrêmement suspectes.
    duplicates = [ids for ids in hashes_normal.values() if len(ids) > 1]
    if duplicates:
        print("ATTENTION: binaires normaux identiques:", duplicates)

    thumbs = MONS / "thumbs.bin"
    if not thumbs.exists():
        raise RuntimeError("thumbs.bin absent")
    data = thumbs.read_bytes()
    if data[:4] != b"TPTH" or len(data) < 6:
        raise RuntimeError("thumbs.bin invalide")
    count = struct.unpack_from("<H", data, 4)[0]
    if count != 386:
        raise RuntimeError(f"thumbs.bin count={count}, attendu 386")
    head = 6 + 4 * count
    for dex in range(1, count + 1):
        off = struct.unpack_from("<I", data, 6 + 4 * (dex - 1))[0]
        if off < head or off + 3 > len(data):
            raise RuntimeError(f"thumbs.bin #{dex:03d}: offset invalide {off}")
        w, h, pal = data[off:off + 3]
        need = 3 + 2 * pal + w * h
        if not w or not h or not pal or off + need > len(data):
            raise RuntimeError(f"thumbs.bin #{dex:03d}: blob invalide {w}x{h}, pal={pal}")

    # Audit statique anti-régression du bug #278 -> #22.
    sdh = (ROOT / "sdmon.h").read_text(encoding="utf-8")
    sdc = (ROOT / "sdmon.cpp").read_text(encoding="utf-8")
    aud = (ROOT / "audio.cpp").read_text(encoding="utf-8")
    bad_patterns = [
        ("sdmon.h", r"load\(uint8_t\s+dexNum"),
        ("sdmon.cpp", r"load\(uint8_t\s+dexNum"),
        ("audio.cpp", r"playSpeciesChirp\(uint8_t\s+dex"),
        ("audio.cpp", r"AUDIO_EVENT_CHIRP,\s*\(uint8_t\)dex"),
    ]
    sources = {"sdmon.h":sdh, "sdmon.cpp":sdc, "audio.cpp":aud}
    for fn,pat in bad_patterns:
        if re.search(pat, sources[fn]):
            raise RuntimeError(f"Régression largeur Dex dans {fn}: {pat}")

    if "uint16_t value;" not in aud:
        raise RuntimeError("AudioEvent.value n'est pas uint16_t")

    print("AUDIT 386 OK")
    print("  386 sprites normaux")
    print("  386 sprites shiny")
    print("  thumbs.bin = 386")
    print("  transport Dex = 16 bits")
    print("  #278 reste #278 (aucun wrap vers #22)")

if __name__ == "__main__":
    main()
