#!/usr/bin/env python3
"""Extract a TPAK asset bundle into the SD source tree for offline packaging."""
from pathlib import Path
import struct

root = Path(__file__).resolve().parents[1]
bundle = (root / "web" / "sprites.pak").read_bytes()
if bundle[:4] != b"TPAK":
    raise SystemExit("Invalid TPAK header")
count = struct.unpack_from("<H", bundle, 4)[0]
pos = 6
entries = []
for _ in range(count):
    length = bundle[pos]
    pos += 1
    name = bundle[pos:pos + length].decode("ascii")
    pos += length
    size = struct.unpack_from("<I", bundle, pos)[0]
    pos += 4
    if not name.startswith(("mons/", "backgrounds/", "music/")) or ".." in Path(name).parts:
        raise SystemExit(f"Invalid asset path: {name}")
    entries.append((name, size))

data_pos = pos
if data_pos + sum(size for _, size in entries) != len(bundle):
    raise SystemExit("TPAK size mismatch")

for name, size in entries:
    target = root / "tools" / "sdcard" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(bundle[data_pos:data_pos + size])
    data_pos += size

print(f"Extracted {count} assets ({len(bundle):,} bytes) into tools/sdcard")
