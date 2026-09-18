#include "sdmon.h"
#include "pin_config.h"
#include <FS.h>
#include <SD_MMC.h>

bool sdReady = false;
bool sdDirty = false;
SdThumbs thumbs;

bool PmdMon::load(uint16_t dexNum, bool shiny) {
  unload();
  if (!sdReady || dexNum < 1 || dexNum > 386) return false;

  char path[28];
  snprintf(path, sizeof(path), "/mons/p%s%03u.bin", shiny ? "s" : "", dexNum);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f && shiny) {  // sin shiny PMD: usa el normal
    snprintf(path, sizeof(path), "/mons/p%03u.bin", dexNum);
    f = SD_MMC.open(path, FILE_READ);
  }
  if (!f) return false;

  uint32_t size = f.size();
  if (size < 7 || size > 3UL * 1024 * 1024) { f.close(); return false; }
  blob = (uint8_t *)ps_malloc(size);
  if (!blob || f.read(blob, size) != size || memcmp(blob, "TPK2", 4) != 0) {
    if (blob) { free(blob); blob = nullptr; }
    f.close();
    return false;
  }
  f.close();

  uint8_t nActs = blob[4];
  memcpy(&palCount, blob + 5, 2);
  if (palCount > 256 || (uint32_t)7 + palCount * 2 > size) { unload(); return false; }
  memcpy(pal, blob + 7, palCount * 2);

  const uint8_t *p = blob + 7 + palCount * 2;
  const uint8_t *end = blob + size;
  for (uint8_t i = 0; i < nActs && p + 4 <= end; i++) {
    uint8_t id = p[0], w = p[1], h = p[2], nf = p[3];
    p += 4;
    if (id >= PMD_NACTS || nf > 24) { unload(); return false; }
    // valida que ms[] y los datos del frame caben en el blob (archivo truncado)
    uint32_t bytes = (uint32_t)nf * 2 + (uint32_t)w * h * nf;
    if (w == 0 || h == 0 || nf == 0 || p + bytes > end) { unload(); return false; }
    PmdAct &a = acts[id];
    a.w = w;
    a.h = h;
    a.frames = nf;
    for (uint8_t k = 0; k < nf; k++) {
      a.ms[k] = p[0] | (p[1] << 8);
      p += 2;
    }
    a.data = p;
    p += (uint32_t)w * h * nf;
    // fila mas baja con contenido en cualquier frame: anclar por los pies
    uint8_t base = 1;
    for (uint8_t f = 0; f < nf; f++) {
      const uint8_t *fr = a.data + (uint32_t)f * w * h;
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++)
          if (fr[r * w + c] != 0xFF) any = true;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    }
    a.base = base;
  }
  loaded = true;
  Serial.printf("cargado %s (%u KB)\n", path, size / 1024);
  return true;
}

void PmdMon::unload() {
  if (blob) {
    free(blob);
    blob = nullptr;
  }
  for (auto &a : acts) {
    a.w = a.h = a.frames = a.base = 0;
    a.data = nullptr;
  }
  loaded = false;
}

bool SdThumbs::load() {
  if (!sdReady) return false;
  File f = SD_MMC.open("/mons/thumbs.bin", FILE_READ);
  if (!f) {
    Serial.println("sin thumbs.bin (galeria sin miniaturas)");
    return false;
  }
  uint32_t size = f.size();
  data = (uint8_t *)ps_malloc(size);
  if (!data || f.read(data, size) != size || memcmp(data, "TPTH", 4) != 0) {
    Serial.println("thumbs.bin invalido");
    if (data) { free(data); data = nullptr; }
    f.close();
    return false;
  }
  f.close();
  memcpy(&count, data + 4, 2);
  dataSize = size;
  loaded = true;
  Serial.printf("miniaturas cargadas: %u (%u KB)\n", count, size / 1024);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded || dex < 1 || dex > count) return nullptr;
  uint32_t off;
  memcpy(&off, data + 6 + 4 * (dex - 1), 4);
  if (off < 6 + 4UL * count || off + 3 > dataSize) return nullptr;
  uint8_t w=data[off], h=data[off+1], n=data[off+2];
  uint32_t need=3UL+2UL*n+(uint32_t)w*h;
  if (!w || !h || !n || off + need > dataSize) return nullptr;
  return data + off;
}

static String sdBaseName(const String &path) {
  int slash = path.lastIndexOf('/');
  return slash >= 0 ? path.substring(slash + 1) : path;
}

static bool validMonName(const String &name) {
  if (name == "thumbs.bin") return true;
  int digit = name.startsWith("ps") ? 2 : (name.startsWith("p") ? 1 : -1);
  int expectedLen = digit == 2 ? 9 : 8;
  if (digit < 0 || name.length() != expectedLen || !name.endsWith(".bin")) return false;
  for (int i=0;i<3;i++) if (!isDigit(name[digit+i])) return false;
  int dex = name.substring(digit, digit + 3).toInt();
  return dex >= 1 && dex <= 386;
}

static bool validBackgroundName(const String &name) {
  static const char *const biomes[] = {"prairie", "foret", "eau_plage", "montagne", "volcan", "neige"};
  static const char *const phases[] = {"dawn", "day", "sunset", "night"};
  for (const char *biome : biomes) for (const char *phase : phases) {
    String expected = String(biome) + "_" + phase + "_466.png";
    if (name == expected) return true;
  }
  return false;
}

static bool validMusicName(const String &name) {
  return name == "morning.wav" || name == "lofi.wav" || name == "night.wav";
}

// Retourne la taille logique d'un TPK2. Si un ancien PUT a ajoute une seconde
// copie a la fin, cette taille est inferieure a f.size() et permet de reparer.
static uint32_t logicalTpk2Size(File &f) {
  uint8_t h[7];
  if (!f.seek(0) || f.read(h, sizeof(h)) != sizeof(h) || memcmp(h, "TPK2", 4)) return 0;
  uint8_t acts = h[4];
  uint16_t palettes = h[5] | (h[6] << 8);
  if (acts == 0 || acts > PMD_NACTS || palettes > 256) return 0;
  uint32_t pos = 7 + (uint32_t)palettes * 2;
  for (uint8_t i=0;i<acts;i++) {
    uint8_t a[4];
    if (pos + 4 > f.size() || !f.seek(pos) || f.read(a, 4) != 4) return 0;
    uint8_t id=a[0], w=a[1], hgt=a[2], frames=a[3];
    if (id >= PMD_NACTS || !w || !hgt || !frames || frames > 24) return 0;
    pos += 4 + (uint32_t)frames * 2 + (uint32_t)w * hgt * frames;
    if (pos > f.size()) return 0;
  }
  return pos;
}

static bool clearSdTree(const char *root);

static void maintainOwnedDir(const char *root, uint8_t kind) {
  File dir = SD_MMC.open(root);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String path = entry.path();
    String name = sdBaseName(path);
    bool isDir = entry.isDirectory();
    bool valid = !isDir && (kind == 0 ? validMonName(name) :
                            (kind == 1 ? validBackgroundName(name) : validMusicName(name)));
    uint32_t physical = entry.size();
    uint32_t logical = (valid && kind == 0 && name != "thumbs.bin") ? logicalTpk2Size(entry) : physical;
    entry.close();
    if (!valid || (kind == 0 && name != "thumbs.bin" && logical == 0)) {
      if (isDir) clearSdTree(path.c_str()); else SD_MMC.remove(path);
      continue;
    }
    // Une taille superieure a la taille logique signifie qu'une ou plusieurs
    // copies ont ete ajoutees a la fin. Supprimer ce fichier evite qu'il ne
    // continue a occuper plusieurs fois sa place; le ZIP propre le remplacera.
    if (logical > 0 && logical < physical) SD_MMC.remove(path);
  }
  dir.close();
}

static void maintainTamaPokeSd() {
  maintainOwnedDir("/mons", 0);
  maintainOwnedDir("/backgrounds", 1);
  maintainOwnedDir("/music", 2);
}

bool sdBegin() {
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  sdReady = SD_MMC.begin("/sdcard", true /* modo 1-bit */, true /* formatea si no monta */);
  if (sdReady) {
    Serial.printf("SD montada: %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    SD_MMC.mkdir("/mons");
    SD_MMC.mkdir("/backgrounds");
    SD_MMC.mkdir("/music");
    // Ne pas rescanner des centaines de gros sprites à chaque démarrage : la
    // maintenance complète est réservée aux commandes d'installation USB.
  } else {
    Serial.println("SD no detectada (el juego usa los sprites de flash)");
  }
  return sdReady;
}

bool SdMon::load(uint16_t dexNum, bool shiny) {
  unload();
  if (!sdReady || dexNum < 1 || dexNum > 386) return false;

  char path[24];
  snprintf(path, sizeof(path), "/mons/%s%03u.bin", shiny ? "s" : "", dexNum);
  File f = SD_MMC.open(path, FILE_READ);
  if (!f && shiny) {  // sin variante shiny: usa la normal
    snprintf(path, sizeof(path), "/mons/%03u.bin", dexNum);
    f = SD_MMC.open(path, FILE_READ);
  }
  if (!f) {
    Serial.printf("no existe %s\n", path);
    return false;
  }

  char magic[4];
  uint16_t header[4];
  if (f.read((uint8_t *)magic, 4) != 4 || memcmp(magic, "TPK1", 4) != 0 ||
      f.read((uint8_t *)header, 8) != 8) {
    f.close();
    return false;
  }
  w = header[0];
  h = header[1];
  frames = header[2];
  frameMs = header[3];
  // acota dimensiones: evita size desbordado o absurdo con archivo corrupto
  if (f.read((uint8_t *)&palCount, 2) != 2 || palCount > 256 ||
      w == 0 || w > 256 || h == 0 || h > 256 || frames == 0 || frames > 64) {
    f.close();
    return false;
  }
  if (f.read((uint8_t *)pal, palCount * 2) != palCount * 2) {
    f.close();
    return false;
  }

  uint32_t size = (uint32_t)w * h * frames;
  data = (uint8_t *)ps_malloc(size);
  if (!data) {
    Serial.println("sin PSRAM para el sprite");
    f.close();
    return false;
  }
  uint32_t got = f.read(data, size);
  f.close();
  if (got != size) {
    Serial.printf("%s truncado (%u de %u)\n", path, got, size);
    unload();
    return false;
  }

  // zoom entero para que el bicho mida ~200 px de alto en pantalla
  scale = 200 / h;
  if (scale < 2) scale = 2;
  if (scale > 5) scale = 5;

  Serial.printf("cargado %s: %ux%u x%u frames @%ums, escala %u\n",
                path, w, h, frames, frameMs, scale);
  loaded = true;
  return true;
}

void SdMon::unload() {
  if (data) {
    free(data);
    data = nullptr;
  }
  loaded = false;
}

// ---------------------------------------------------------------------------
// Protocolo de carga por USB (para llenar la SD sin sacarla de la placa):
//   PUT <ruta> <bytes>\n  + datos crudos   -> "OK" ... "DONE"
//   LS\n                                   -> listado de /mons
// Usar con tools/send_sd.py
// ---------------------------------------------------------------------------

static bool clearSdTree(const char *root) {
  File dir = SD_MMC.open(root);
  if (!dir) return true;  // dossier absent = deja propre
  if (!dir.isDirectory()) { dir.close(); return SD_MMC.remove(root); }
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    String child = entry.path();
    bool isDir = entry.isDirectory();
    entry.close();
    if (isDir) {
      if (!clearSdTree(child.c_str())) { dir.close(); return false; }
    } else if (!SD_MMC.remove(child)) {
      dir.close(); return false;
    }
  }
  dir.close();
  return SD_MMC.rmdir(root);
}

bool sdSerialCommand(const String &line) {
  if (line == "PREPARE") {
    if (!sdReady) {
      SD_MMC.end();
      delay(120);
      sdBegin();
    }
    if (!sdReady) { Serial.println("ERRSD"); return true; }
    bool clean = clearSdTree("/mons") && clearSdTree("/backgrounds") &&
                 clearSdTree("/music");
    if (!clean) { Serial.println("ERRCLEAN"); return true; }
    bool dirs = SD_MMC.mkdir("/mons") && SD_MMC.mkdir("/backgrounds") &&
                SD_MMC.mkdir("/music");
    Serial.println(dirs ? "READY" : "ERRCLEAN");
    return true;
  } else if (line.startsWith("PUT ")) {
    int sp = line.lastIndexOf(' ');
    String path = line.substring(4, sp);
    uint32_t size = line.substring(sp + 1).toInt();
    if (!sdReady || size == 0 || size > 4 * 1024 * 1024) {
      Serial.println("ERR");
      return true;
    }
    if (!path.startsWith("/")) path = "/" + path;
    int slash = path.lastIndexOf('/');
    if (slash > 0) {
      String parent = path.substring(0, slash);
      if (!SD_MMC.exists(parent)) SD_MMC.mkdir(parent);
    }
    // FILE_WRITE ajoute a la fin d'un fichier existant sur Arduino-ESP32.
    // Un second chargement doublait donc tous les sprites jusqu'a remplir la SD.
    // Supprimer d'abord l'ancienne copie garantit un vrai remplacement et
    // permet aussi de recuperer progressivement une carte deja saturee.
    if (SD_MMC.exists(path) && !SD_MMC.remove(path)) {
      Serial.println("ERR");
      return true;
    }
    uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
    if (freeBytes < (uint64_t)size + 65536ULL) {
      Serial.println("ERRFULL");
      return true;
    }
    File f = SD_MMC.open(path, FILE_WRITE);
    if (!f) {
      Serial.println("ERR");
      return true;
    }
    // Pendant un transfert le navigateur attend chaque accusé de réception.
    // Le timeout nul global peut jeter ces messages si l'USB est momentanément
    // occupé : on lui laisse ici le temps de partir.
    Serial.setTxTimeoutMs(1000);
    Serial.println("OK");
    static uint8_t buf[2048];
    uint32_t remaining = size;
    Serial.setTimeout(5000);
    while (remaining > 0) {
      size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
      size_t received = 0;
      uint32_t deadline = millis() + 8000;
      // readBytes() peut rendre un bloc partiel. Ne jamais envoyer l'ACK avant
      // d'avoir reçu exactement le bloc demandé, sinon navigateur et carte se
      // désynchronisent après quelques paquets.
      while (received < want && (int32_t)(deadline - millis()) > 0) {
        size_t n = Serial.readBytes(buf + received, want - received);
        if (n > 0) received += n;
      }
      if (received != want) break;
      if (f.write(buf, want) != want) {
        remaining = UINT32_MAX;
        break;
      }
      remaining -= want;
      Serial.println("#");  // ack: listo para el siguiente bloque
    }
    f.close();
    Serial.setTimeout(1000);
    sdDirty = (remaining == 0);
    if (remaining == 0) Serial.println("DONE");
    else if (remaining == UINT32_MAX) Serial.println("ERRWRITE");
    else Serial.println("ERRTIMEOUT");
    Serial.flush();
    Serial.setTxTimeoutMs(0);
    return true;
  } else if (line == "LS") {
    File dir = SD_MMC.open("/mons");
    if (dir) {
      File e;
      while ((e = dir.openNextFile())) {
        Serial.printf("%s %u\n", e.name(), (uint32_t)e.size());
        e.close();
      }
      dir.close();
    }
    Serial.println("DONE");
    return true;
  }
  return false;
}
