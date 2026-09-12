#!/bin/bash
# Build TamaPoke pour le Web Flasher.
# Le sketch est copié dans un dossier temporaire nommé "TamaPoke"
# afin qu'Arduino CLI trouve toujours TamaPoke.ino, quel que soit
# le nom du dépôt GitHub (ex: Test-1).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
VERSION="1.46.54-moretro3d-v9.92-sprite-audit-386"

echo "Préparation du sketch TamaPoke..."
TMP="$(mktemp -d "${TMPDIR:-/tmp}/tamapoke-ci.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

SKETCH="$TMP/TamaPoke"
BUILD="$TMP/build"
CACHE="${HOME}/.cache/arduino/poketama-build"
mkdir -p "$SKETCH" "$BUILD" "$ROOT/web/firmware"
mkdir -p "$CACHE"

# Arduino exige que le fichier .ino principal porte le même nom que
# le dossier du sketch. On copie donc toutes les sources dans TamaPoke/.
cp "$ROOT/TamaPoke.ino" "$SKETCH/TamaPoke.ino"
cp "$ROOT"/*.cpp "$SKETCH/" 2>/dev/null || true
cp "$ROOT"/*.h   "$SKETCH/" 2>/dev/null || true

echo "Compilation..."
arduino-cli compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD" \
  --build-cache-path "$CACHE" \
  "$SKETCH"

echo "Copie des binaires pour le Web Flasher..."
cp "$BUILD/TamaPoke.ino.bootloader.bin" "$ROOT/web/firmware/poketama-$VERSION-bootloader.bin"
cp "$BUILD/TamaPoke.ino.partitions.bin" "$ROOT/web/firmware/poketama-$VERSION-partitions.bin"
cp "$BUILD/boot_app0.bin" "$ROOT/web/firmware/poketama-$VERSION-boot_app0.bin"
cp "$BUILD/TamaPoke.ino.bin" "$ROOT/web/firmware/poketama-$VERSION-app.bin"

echo "Firmware OK."
ls -lh "$ROOT/web/firmware/poketama-$VERSION-app.bin"

echo "Vérification de Pillow pour le générateur PMD..."
python3 - <<'PY'
try:
    from PIL import Image
    print("Pillow disponible :", Image.__version__)
except Exception as exc:
    raise SystemExit("Pillow/PIL absent : " + str(exc))
PY

echo "Préparation des sprites Johto + Hoenn #152-386..."
missing=0
for n in $(seq 152 386); do
  printf -v num "%03d" "$n"
  if [ ! -f "$ROOT/tools/sdcard/mons/p${num}.bin" ] || [ ! -f "$ROOT/tools/sdcard/mons/ps${num}.bin" ]; then
    missing=1
    break
  fi
done

if [ "$missing" -eq 1 ]; then
  echo "Téléchargement/packaging PMD SpriteCollab pour Johto + Hoenn..."
  python3 "$ROOT/tools/pack_pmd.py" $(seq 152 386)
fi

echo "Génération des 386 miniatures Pokédex..."
python3 "$ROOT/tools/make_thumbs.py"

echo "Empaquetage des sprites, decors et OST pour l'installation automatique..."
python3 "$ROOT/tools/pack_bundle.py"
python3 "$ROOT/tools/pack_music.py"

echo "Audit intégral des assets #001-386..."
python3 "$ROOT/tools/audit_386_assets.py"

echo "Audit intégral des évolutions #001-386..."
python3 "$ROOT/tools/audit_evolutions.py"

echo "Audit final V9 interface + combat + habitats..."
python3 "$ROOT/tools/audit_final_v9.py"

echo "BUILD TERMINE"
