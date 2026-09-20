#!/usr/bin/env python3
"""Empaqueta todos los assets SD (sprites, decors et OST) dans un seul
fichier web/sprites.pak pour que l'installateur web les charge en un clic.

Formato TPAK (little-endian):
  char[4]  "TPAK"
  uint16   count
  count x { uint8 nameLen; char name[nameLen]; uint32 size }   (indice)
  ...datos de cada fichero, en el mismo orden...

El instalador (web/index.html) lo descarga, lo parte por el indice y manda cada
fichero a la placa con el protocolo PUT (igual que tools/send_sd.py).
"""
import glob
import os
import struct

HERE = os.path.dirname(__file__)
SDCARD = os.path.join(HERE, 'sdcard')
OUT = os.path.join(HERE, '..', 'web', 'sprites.pak')


def main():
    patterns = (
        os.path.join(SDCARD, 'mons', '*.bin'),
        os.path.join(SDCARD, 'backgrounds', '*_466.png'),
        os.path.join(SDCARD, 'music', '*.wav'),
    )
    files = sorted(f for pattern in patterns for f in glob.glob(pattern))
    if not files:
        raise SystemExit('aucun asset SD dans ' + SDCARD)
    names = [os.path.relpath(f, SDCARD).replace(os.sep, '/') for f in files]
    blobs = [open(f, 'rb').read() for f in files]

    with open(OUT, 'wb') as o:
        o.write(b'TPAK')
        o.write(struct.pack('<H', len(files)))
        for name, blob in zip(names, blobs):
            nb = name.encode()
            o.write(struct.pack('<B', len(nb)))
            o.write(nb)
            o.write(struct.pack('<I', len(blob)))
        for blob in blobs:
            o.write(blob)

    total = sum(len(b) for b in blobs)
    print(f'{os.path.normpath(OUT)}: {len(files)} assets SD, {total / 1048576:.1f} MB de donnees '
          f'({os.path.getsize(OUT) / 1048576:.1f} MB total)')


if __name__ == '__main__':
    main()
