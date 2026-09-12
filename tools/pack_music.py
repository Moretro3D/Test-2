#!/usr/bin/env python3
"""Construit un petit paquet TPAK contenant uniquement les trois OST."""
from pathlib import Path
import struct


root = Path(__file__).resolve().parents[1]
music_dir = root / "tools" / "sdcard" / "music"
output = root / "web" / "music.pak"
paths = [music_dir / name for name in ("morning.wav", "lofi.wav", "night.wav")]

for path in paths:
    if not path.is_file():
        raise SystemExit(f"OST absente: {path}")

entries = [(f"music/{path.name}", path.read_bytes()) for path in paths]
with output.open("wb") as stream:
    stream.write(b"TPAK")
    stream.write(struct.pack("<H", len(entries)))
    for name, data in entries:
        encoded = name.encode("utf-8")
        stream.write(struct.pack("<B", len(encoded)))
        stream.write(encoded)
        stream.write(struct.pack("<I", len(data)))
    for _, data in entries:
        stream.write(data)

print(f"{output}: {len(entries)} OST, {output.stat().st_size} octets")
