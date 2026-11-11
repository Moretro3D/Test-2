#include "pet.h"
#include "dex.h"
#include "audio.h"
#ifdef ARDUINO
#include "sdmon.h"
#include <FS.h>
#include <SD_MMC.h>
#endif

#ifdef ARDUINO
namespace {
static const uint32_t SD_SAVE_MAGIC = 0x50545356UL; // "PTSV"
static const uint16_t SD_SAVE_VERSION = 1;
static const char *SD_SAVE_DIR = "/save";
static const char *SD_SAVE_MAIN = "/save/poketama.bin";
static const char *SD_SAVE_BACKUP = "/save/poketama.bak";
static const char *SD_SAVE_TEMP = "/save/poketama.tmp";

struct SdPetSaveV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t size;
  uint32_t sequence;
  uint8_t fullness, joy, energy, hygiene, poops, weight;
  uint8_t geneAtk, geneDef, geneSpe, trAtk, trDef, trSpe;
  uint8_t flags;
  uint32_t ageMinutes;
  int16_t speciesId;
  int16_t eggTarget;
  uint8_t eggTaps, careMistakes, lastEnd;
  uint32_t lastSeenEpoch;
  uint8_t dexReg[49], dexShinyReg[49], dexCaught[49];
  uint16_t streak, bestStreak;
  uint32_t lastCareDay;
  uint8_t bond;
  char nick[12];
  uint16_t medals, totalMedals, lastMilestone;
  uint16_t gameHi, strHi, catchHi, memoHi, cleanHi, typeHi;
  uint16_t battleWins, battleLosses, battleStreak, bestBattleStreak;
  uint8_t kantoBadges, collectionFrame, boxBackground;
  int16_t boxFavorites[2][8];
  uint32_t lastPetInteractMinute;
  uint8_t dexRewardMask;
  uint32_t dailyGoalDay;
  uint8_t dailyGoalType[DAILY_GOAL_COUNT];
  uint8_t dailyGoalProgress[DAILY_GOAL_COUNT];
  uint8_t dailyGoalDone;
  uint8_t itemCounts[EXP_ITEM_COUNT];
  uint32_t expeditionEndEpoch;
  uint8_t expeditionRewardItem;
  uint8_t profileCount;
  int16_t profileDex[STORED_PROFILE_LIMIT];
  StoredPetProfile profiles[STORED_PROFILE_LIMIT];
  uint32_t crc;
};

uint32_t sdSaveChecksum(const SdPetSaveV1 &save) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&save);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < offsetof(SdPetSaveV1, crc); i++) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

bool validSdSave(const SdPetSaveV1 &save) {
  return save.magic == SD_SAVE_MAGIC && save.version == SD_SAVE_VERSION &&
         save.size == sizeof(SdPetSaveV1) && save.profileCount <= STORED_PROFILE_LIMIT &&
         save.crc == sdSaveChecksum(save);
}

bool readSdSaveFile(const char *path, SdPetSaveV1 &save) {
  File file = SD_MMC.open(path, FILE_READ);
  if (!file || file.size() != sizeof(SdPetSaveV1)) {
    if (file) file.close();
    return false;
  }
  bool ok = file.read(reinterpret_cast<uint8_t *>(&save), sizeof(save)) == sizeof(save);
  file.close();
  return ok && validSdSave(save);
}
}
#endif

void Pet::begin() {
  prefs.begin("tamapoke", false);
  bool hadSave = prefs.getBool("init", false);
  saveLoadedFromNvs = hadSave;
  saveCreatedThisBoot = !hadSave;
  if (!hadSave) {
    prefs.putBool("init", true);
    newEgg();
  } else {
    load();
  }
  lastTick = millis();
}

void Pet::newEgg() {
  ceremony = CER_NONE;
  neglectTicks = 0;
  weight = 0;
  speciesId = -1;
  prevSpeciesId = -1;
  starterPick = (registeredCount() == 0);  // primera partida: le joueur choisit
  // Valeur neutre et sure tant que le choix n'est pas confirme. Aucun tirage
  // aleatoire (et notamment aucun Evoli) ne peut remplacer le starter choisi.
  eggTarget = starterPick ? 1 : pickEggSpecies();
  // sorteo shiny: 1/48 base, mejor con despedida y con racha/vinculo altos
  int shinyBase = (lastEnd == CER_FAREWELL ? 24 : 48) - careBonus();
  if (shinyBase < 8) shinyBase = 8;
  eggShiny = (random(shinyBase) == 0);
  eggTaps = 0;
  fullness = 80;
  joy = 80;
  energy = 80;
  hygiene = 100;
  poops = 0;
  ageMinutes = 0;
  careMistakes = 0;
  mistakeCooldown = 0;
  sleeping = false;
  save();
}

// progresion offline: el tiempo paso aunque estuviera apagado, pero con
// piedad — las barras bajan con suelo en 15 (vuelve hambriento, no muerto),
// sin descuidos ni escapadas en ausencia
static uint8_t dropTo(uint8_t v, uint8_t d, uint8_t fl) {
  if (v <= fl) return v;
  return (v - fl > d) ? v - d : fl;
}

void Pet::setClock(uint32_t nowEpoch) {
  lastSeenEpoch = nowEpoch;
  if (nowEpoch) save();  // persiste ya: un corte de luz no pierde la referencia
}

void Pet::syncClock(uint32_t nowEpoch) {
  uint32_t seen = prefs.getUInt("seen", 0);
  lastSeenEpoch = nowEpoch;
  if (nowEpoch == 0) return;
  uint32_t mins = (seen && nowEpoch > seen) ? (nowEpoch - seen) / 60 : 0;
  if (mins < 2 || ceremony != CER_NONE) {
    save();  // primera vez o sin tiempo que aplicar: solo persistir la hora
    return;
  }
  if (mins > 14UL * 24 * 60) mins = 14UL * 24 * 60;  // tope: 2 semanas

  for (uint32_t i = 0; i < mins; i++) {
    if (starterPick) continue; // aucun vieillissement/eclosion avant le choix
    ageMinutes++;
    if (isEgg()) {
      if (ageMinutes >= 3) hatch();  // eclosiona en tu ausencia
      continue;
    }
    if (sleeping) {  // descanso: baja lento y con suelo, igual que en vivo
      energy = clamp100(energy + 6);
      if (ageMinutes % 2 == 0) {
        fullness = dropTo(fullness, 1, 30);
        joy = dropTo(joy, 1, 35);
      }
      if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
      continue;
    }
    fullness = dropTo(fullness, 2, 15);
    energy = dropTo(energy, 1, 15);
    hygiene = dropTo(hygiene, 1, 15);
    joy = dropTo(joy, 1, 15);
  }
  if (!isEgg()) {
    if (!sleeping) {  // durmiendo no ensucia
      uint8_t p = poops + mins / 240;
      poops = p > 3 ? 3 : p;
    }
    // Le niveau gagné hors ligne est traité au retour dans la boucle principale,
    // avec une animation par étape pour préserver les chaînes d'évolution.
  }
  Serial.printf("offline: %u min aplicados (nv.%u)\n", mins, level());
  save();
}

void Pet::update(uint32_t nowMs) {
  // fin de ceremonia: la criatura se va y queda un huevo nuevo
  if (ceremony != CER_NONE && millis() > ceremonyUntil) {
    newEgg();
    return;
  }
  while (nowMs - lastTick >= PET_TICK_MS) {
    lastTick += PET_TICK_MS;
    tick();
  }
}

void Pet::tick() {
  if (ceremony != CER_NONE) return;  // el tiempo se detiene en la despedida
  if (starterPick) return;           // le premier choix ne peut pas expirer
  ageMinutes++;

  if (isEgg()) {
    if (ageMinutes >= 3) hatch();  // si no lo tocas, eclosiona solo a los 3 min
    return;
  }

  // el sueño es descanso: la energia se recupera y las necesidades bajan MUCHO
  // mas lento que despierto y con suelo (amanece pidiendo algo de mimo, no a
  // cero, sin descuidos ni escapadas). despierto: comida -2/min, hig/joy -1/min.
  // El peso aun se quema; la racha de buen cuidado (goodTicks) queda en pausa.
  if (sleeping) {
    energy = clamp100(energy + 6);
    if (weight > 0 && ageMinutes % 3 == 0) weight--;
    if (ageMinutes % 2 == 0) {                 // ~4x mas lento que despierto
      fullness = dropTo(fullness, 1, 30);
      joy = dropTo(joy, 1, 35);
    }
    if (ageMinutes % 3 == 0) hygiene = dropTo(hygiene, 1, 45);
    checkMedals();  // aun puede cruzar un nivel por edad mientras duerme
    if (++ticksSinceSave >= 5) pendingSave = true;
    return;
  }

  if (ageMinutes % MINUTES_PER_LEVEL == 0) sfxPlay(SFX_LEVEL);  // subio de nivel (despierto)

  fullness = clamp100(fullness - 2);
  energy = clamp100(energy - 1);
  if (fullness > 40 && poops < 3 && random(100) < 15) poops++;

  hygiene = clamp100(hygiene - 1 - 4 * poops);
  // el sobrepeso da pereza: la energia cae el doble
  if (weight > 50) energy = clamp100(energy - 1);
  if (weight > 0 && ageMinutes % 3 == 0) weight--;

  // la disciplina forja la defensa: 12 h seguidas bien cuidado = +1 DEF
  if (lowestStat() >= 40) {
    if (++goodTicks >= 720) {
      goodTicks = 0;
      if (trDef < 100) trDef++;
    }
  } else {
    goodTicks = 0;
  }

  int dJoy = -1;
  if (fullness < 30) dJoy -= 2;
  if (hygiene < 30) dJoy -= 2;
  joy = clamp100(joy + dJoy);

  checkMedals();  // la evolucion la dispara el usuario (canEvolveNow + tap), no el tick
  // La negligence n'est plus comptabilisee et le Pokemon ne fuit jamais.
  careMistakes = 0;
  mistakeCooldown = 0;
  neglectTicks = 0;

  // autoguardado periodico: NO escribir a flash aqui (corre dentro del loop,
  // mientras se anima); solo marcar y dejar que el loop lo vuelque al atenuar
  if (++ticksSinceSave >= 5) pendingSave = true;
}

// vuelca el guardado periodico pendiente (lo llama el loop en un momento sin
// animacion para que el paron de la escritura a flash no se vea)
void Pet::flushSave() {
  if (pendingSave) save();
}

// quedan miembros sin registrar en la linea evolutiva de esta base?
bool Pet::lineHasUnregistered(int16_t base) const {
  int16_t cur = base;
  for (int guard = 0; cur >= 1 && cur <= DEX_COUNT && guard < 6; guard++) {
    if (!isRegistered(cur)) return true;
    if (cur == DEX_EEVEE) {
      static const int16_t EEVEE_FORMS[] = { 134, 135, 136, 196, 197 };
      for (uint8_t i = 0; i < sizeof(EEVEE_FORMS) / sizeof(EEVEE_FORMS[0]); i++)
        if (!isRegistered(EEVEE_FORMS[i])) return true;
      return false;
    }
    cur = DEX_TBL[cur].evolvesTo;
  }
  return false;
}

uint8_t Pet::eggRarity() const {
  return (eggTarget >= 1 && eggTarget <= DEX_COUNT) ? (uint8_t)DEX_TBL[eggTarget].rarity : (uint8_t)R_COMUN;
}

// elige la especie del huevo: tirada de rareza (mejorada por una despedida
// completa, castigada por una escapada) y sesgo hacia lineas incompletas
int16_t Pet::pickEggSpecies() {
  // primera partida: inicial clasico
  if (registeredCount() == 0) {
    return CLASSIC_DEX[random(NUM_CLASSIC_DEX)];
  }

  uint8_t tier = R_COMUN;
  if (lastEnd != CER_RUNAWAY) {
    bool blessed = (lastEnd == CER_FAREWELL);
    int rare = (blessed ? 45 : 27) + careBonus();
    int leg = (registeredCount() >= 25) ? (blessed ? 10 : 3) + careBonus() / 3 : 0;
    int r = random(100);
    if (r < leg) tier = R_LEGENDARIO;
    else if (r < leg + rare) tier = R_RARO;
  }

  // candidatos del tier con linea incompleta; si no hay, baja de tier;
  // si la pokedex del tier esta completa, vale cualquiera del tier
  for (int pass = 0; pass < 2; pass++) {
    for (int t = tier; t >= R_COMUN; t--) {
      int16_t cand[160];
      int n = 0;
      for (int16_t d = 1; d <= DEX_COUNT && n < 160; d++) {
        if (DEX_TBL[d].rarity != t) continue;
        if (pass == 0 && !lineHasUnregistered(d)) continue;
        cand[n++] = d;
      }
      if (n > 0) return cand[random(n)];
    }
  }
  return CLASSIC_DEX[random(NUM_CLASSIC_DEX)];  // inalcanzable, por si acaso
}

void Pet::registerSpecies(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return;
  bool wasKnown = isRegistered(dex) || isCaught(dex);
  dexReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (shiny) dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (!wasKnown) applyDexRewards();
}

// la racha y el vinculo mejoran el sorteo del huevo (0..~14)
int Pet::careBonus() const {
  int s = streak > 30 ? 30 : streak;
  return s / 3 + bond / 25;
}

uint8_t Pet::dailyGoalTarget(uint8_t goalType) const {
  switch (goalType) {
    case DAILY_GOAL_CARE: return 1;
    case DAILY_GOAL_PLAY: return 1;
    case DAILY_GOAL_BATTLE: return 1;
    case DAILY_GOAL_CATCH: return 5;
    case DAILY_GOAL_MEMO: return 3;
    default: return 1;
  }
}

bool Pet::dailyGoalComplete(uint8_t index) const {
  return index < DAILY_GOAL_COUNT && (dailyGoalDone & (1 << index));
}

void Pet::ensureDailyGoals() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == dailyGoalDay) return;
  static const uint8_t POOL[] = {
    DAILY_GOAL_CARE, DAILY_GOAL_PLAY, DAILY_GOAL_CATCH, DAILY_GOAL_MEMO, DAILY_GOAL_BATTLE
  };
  uint8_t seed = (uint8_t)((d + (speciesId > 0 ? speciesId : 0)) % 5);
  for (uint8_t i = 0; i < DAILY_GOAL_COUNT; i++) {
    dailyGoalType[i] = POOL[(seed + i) % 5];
    dailyGoalProgress[i] = 0;
  }
  dailyGoalDone = 0;
  dailyGoalDay = d;
  save();
}

void Pet::applyDailyReward() {
  joy = clamp100((int)joy + 4);
  addBond(1);
}

void Pet::noteDailyGoal(uint8_t goalType, uint8_t amount) {
  if (isEgg() || ceremony != CER_NONE || amount == 0) return;
  ensureDailyGoals();
  if (dailyGoalDay == 0) return;
  bool changed = false;
  const uint8_t allDoneMask = (1 << DAILY_GOAL_COUNT) - 1;
  bool rewardWasComplete = (dailyGoalDone & allDoneMask) == allDoneMask;
  for (uint8_t i = 0; i < DAILY_GOAL_COUNT; i++) {
    if (dailyGoalType[i] != goalType || dailyGoalComplete(i)) continue;
    uint8_t target = dailyGoalTarget(goalType);
    uint16_t next = (uint16_t)dailyGoalProgress[i] + amount;
    dailyGoalProgress[i] = next > target ? target : next;
    changed = true;
    if (dailyGoalProgress[i] >= target) {
      dailyGoalDone |= (1 << i);
    }
  }
  // Une seule recompense, directement au moment ou les trois objectifs sont faits.
  if (!rewardWasComplete && (dailyGoalDone & allDoneMask) == allDoneMask) {
    applyDailyReward();
    heartUntil = millis() + HEART_MS;
    sfxPlay(SFX_DAILY_GOAL);
  }
  if (changed) save();
}

// primer cuidado del dia: avanza la racha y afianza el vinculo
void Pet::registerCare() {
  if (isEgg() || ceremony != CER_NONE) return;
  uint32_t d = today();
  if (d == 0 || d == lastCareDay) return;  // sin reloj, o ya conto hoy
  if (lastCareDay == 0 || d == lastCareDay + 1) {
    streak++;
  } else {
    streak = 1;        // hubo un hueco de dias
    lastMilestone = 0;
  }
  lastCareDay = d;
  bondToday = 0;
  if (streak > bestStreak) bestStreak = streak;
  bond = clamp100(bond + 4);
  uint16_t ms = (streak >= 100) ? 100 : (streak >= 30) ? 30
              : (streak >= 7)   ? 7   : (streak >= 3)  ? 3 : 0;
  if (ms > lastMilestone) {
    lastMilestone = ms;
    milestoneUntil = millis() + 4500;
  }
  checkMedals();
  save();
}

void Pet::addBond(uint8_t amt) {
  if (bondToday >= 8) return;  // tope diario: el vinculo no se farmea
  bond = clamp100(bond + amt);
  bondToday += amt;
}

void Pet::checkMedals() {
  if (isEgg()) return;
  uint16_t before = medals;
  if (level() >= 10) medals |= MED_LV10;
  if (level() >= 25) medals |= MED_LV25;
  if (level() >= 50) medals |= MED_LV50;
  if (berryKnown) medals |= MED_BERRY;
  if (streak >= 7) medals |= MED_STREAK7;
  if (bond >= 100) medals |= MED_BOND;
  if (DEX_TBL[speciesId].evolvesTo == 0) medals |= MED_FINAL;
  if (weight == 0 && level() >= 5) medals |= MED_FIT;
  uint16_t gained = medals & ~before;
  if (gained) {
    for (uint16_t m = gained; m; m &= (m - 1)) totalMedals++;
    newMedal = gained;
    medalUntil = millis() + 4000;
    if (!sleeping) sfxPlay(SFX_MEDAL);
    save();
  }
}

void Pet::rename(const char *name) {
  strncpy(nick, name, sizeof(nick) - 1);
  nick[sizeof(nick) - 1] = 0;
  save();
}

static uint16_t calcStat(uint8_t base, uint8_t gene, uint8_t lvl, uint8_t tr) {
  return (uint16_t)base * gene / 100 + lvl + tr;
}

uint16_t Pet::atkStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bAtk, geneAtk, level(), trAtk);
}
uint16_t Pet::defStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bDef, geneDef, level(), trDef);
}
uint16_t Pet::speStat() const {
  return isEgg() ? 0 : calcStat(DEX_TBL[speciesId].bSpe, geneSpe, level(), trSpe);
}

uint16_t Pet::registeredCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= DEX_COUNT; i++)
    if (isRegistered(i)) n++;
  return n;
}

uint16_t Pet::caughtCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= DEX_COUNT; i++)
    if (isCaught(i)) n++;
  return n;
}

uint16_t Pet::knownDexCount() const {
  uint16_t n = 0;
  for (int i = 1; i <= DEX_COUNT; i++)
    if (isRegistered(i) || isCaught(i)) n++;
  return n;
}

uint8_t Pet::collectionRank() const {
  uint16_t known = knownDexCount();
  if (known >= 151) return 5;
  if (known >= 100) return 4;
  if (known >= 50) return 3;
  if (known >= 25) return 2;
  if (known >= 10) return 1;
  return 0;
}

uint8_t Pet::unlockedCollectionFrameCount() const {
  return (uint8_t)(collectionRank() + 1);
}

bool Pet::setCollectionFrame(uint8_t frame) {
  if (frame >= unlockedCollectionFrameCount()) return false;
  if (collectionFrame == frame) return true;
  collectionFrame = frame;
  save();
  return true;
}

bool Pet::setBoxBackground(uint8_t background) {
  if (background >= 16) return false;
  boxBackground = background;
  // Même si l'aperçu a déjà appliqué cette valeur en RAM, Valider doit
  // l'écrire en NVS pour la conserver après extinction.
  save();
  return true;
}

uint16_t Pet::nextDexGoal() const {
  static const uint16_t GOALS[] = { 10, 25, 50, 100, 151, 251, 386 };
  uint16_t known = knownDexCount();
  for (uint8_t i = 0; i < sizeof(GOALS) / sizeof(GOALS[0]); i++)
    if (known < GOALS[i]) return GOALS[i];
  return 386;
}

uint16_t Pet::applyDexRewards() {
  if (ceremony != CER_NONE || isEgg()) return 0;
  static const uint16_t GOALS[] = { 10, 25, 50, 100, 151, 251, 386 };
  uint16_t known = knownDexCount();
  uint16_t reached = 0;
  for (uint8_t i = 0; i < sizeof(GOALS) / sizeof(GOALS[0]); i++) {
    uint8_t bit = 1 << i;
    if (known < GOALS[i] || (dexRewardMask & bit)) continue;
    dexRewardMask |= bit;
    reached = GOALS[i];
    if (GOALS[i] == 10) joy = clamp100((int)joy + 5);
    else if (GOALS[i] == 25) addBond(2);
    else if (GOALS[i] == 50) {
      if (trAtk <= trDef && trAtk <= trSpe) trAtk = clamp100((int)trAtk + 1);
      else if (trDef <= trAtk && trDef <= trSpe) trDef = clamp100((int)trDef + 1);
      else trSpe = clamp100((int)trSpe + 1);
    } else if (GOALS[i] == 100) {
      addBond(4);
    } else if (GOALS[i] == 151) {
      heartUntil = millis() + HEART_MS;
    } else if (GOALS[i] == 251) {
      addBond(5);
      heartUntil = millis() + HEART_MS;
    } else if (GOALS[i] == 386) {
      addBond(8);
      heartUntil = millis() + HEART_MS;
    }
  }
  if (reached) save();
  if (reached) {
    lastDexReward = reached;
    dexRewardUntil = millis() + 4200;
    sfxPlay(SFX_MEDAL);
  }
  return reached;
}

void Pet::registerCaught(int16_t dex, bool caughtShiny) {
  if (dex < 1 || dex > DEX_COUNT) return;
  bool wasKnown = isRegistered(dex) || isCaught(dex);
  dexCaught[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  if (caughtShiny) dexShinyReg[(dex - 1) >> 3] |= (1 << ((dex - 1) & 7));
  noteDailyGoal(DAILY_GOAL_CATCH, 1);
  if (!wasKnown) applyDexRewards();
  save();
}

uint8_t Pet::catchChanceForWild(int16_t wildDex, uint8_t wildLevel, uint8_t petLevel, bool closeWin) const {
  if (wildDex < 1 || wildDex > DEX_COUNT) return 0;
  const DexEntry &wild = DEX_TBL[wildDex];
  if (wild.rarity == R_LEGENDARIO) return 0;
  int chance = wild.rarity == R_RARO ? 28 : 55;
  int levelGap = (int)wildLevel - (int)(petLevel ? petLevel : 1);
  if (levelGap > 0) chance -= levelGap * 4;
  else if (levelGap < 0) chance += (-levelGap) * 2;
  if (closeWin) chance += 8;
  chance += bond / 20;
  if (wild.rarity == R_RARO && chance > 60) chance = 60;
  if (chance > 75) chance = 75;
  if (chance < 10) chance = 10;
  return (uint8_t)chance;
}

uint8_t Pet::respectCatchChanceForWild(int16_t wildDex, uint8_t wildLevel, uint8_t petLevel) const {
  uint8_t normal = catchChanceForWild(wildDex, wildLevel, petLevel, true);
  if (normal == 0) return 0;
  uint8_t chance = (uint8_t)((uint16_t)normal * 40 / 100);
  if (chance < 5) chance = 5;
  if (chance > 25) chance = 25;
  return chance;
}

bool Pet::tryCatchWild(int16_t wildDex, uint8_t wildLevel, uint8_t petLevel, bool closeWin, uint8_t luckRoll, bool wildShiny) {
  uint8_t chance = catchChanceForWild(wildDex, wildLevel, petLevel, closeWin);
  if (chance == 0) return false;
  if ((luckRoll % 100) < chance) {
    registerCaught(wildDex, wildShiny);
    joy = clamp100((int)joy + 4);
    addBond(1);
    save();
    return true;
  }
  return false;
}

bool Pet::tryRespectCatchWild(int16_t wildDex, uint8_t wildLevel, uint8_t petLevel, uint8_t luckRoll, bool wildShiny) {
  uint8_t chance = respectCatchChanceForWild(wildDex, wildLevel, petLevel);
  if (chance == 0) return false;
  if ((luckRoll % 100) < chance) {
    registerCaught(wildDex, wildShiny);
    save();
    return true;
  }
  return false;
}

// forma final que ya cumplio su ciclo (7 dias): lista para despedirse. La
// despedida la dispara el usuario con el boton (no salta sola, para que la vea)
void Pet::release() {
  if (isEgg() || ceremony != CER_NONE) return;
  lastEnd = CER_RELEASE;
  ceremony = CER_RELEASE;
  ceremonyUntil = millis() + CEREMONY_MS;
  heartUntil = ceremonyUntil;
  sfxPlay(SFX_BYE);
  save();
}

void Pet::hatch() {
  if (starterPick) return; // securite absolue : jamais d'eclosion avant validation
  speciesId = eggTarget;
  shiny = eggShiny;
  // genes del individuo: 90-110% por stat (cada crianza es unica)
  geneAtk = 90 + random(21);
  geneDef = 90 + random(21);
  geneSpe = 90 + random(21);
  trAtk = trDef = trSpe = 0;
  berryKnown = false;
  bond = 0;          // vinculo, medallas y nombre son del individuo
  bondToday = 0;
  medals = 0;
  newMedal = 0;
  nick[0] = 0;
  registerSpecies(speciesId);  // criado = registrado en la pokedex
  // La boite represente les individus obtenus, y compris le starter et les
  // Pokemon nes d'un oeuf. Le Pokedex conserve separement les especes vues.
  dexCaught[(speciesId - 1) >> 3] |= (1 << ((speciesId - 1) & 7));
  checkMedals();     // por si nace ya en forma final (legendario)
  sfxPlay(SFX_HATCH);
  save();
}

// Toutes les évolutions ont désormais un seuil de niveau fixe. La boucle
// principale déclenche une étape à la fois, après chaque animation.
bool Pet::canEvolveNow() const {
  if (isEgg() || sleeping || ceremony != CER_NONE) return false;
  const DexEntry &d = DEX_TBL[speciesId];

  if (d.evolvesTo == 0 || d.evolveLevel == 0) return false;
  return level() >= d.evolveLevel;
}

void Pet::evolve() {
  if (!canEvolveNow()) return;
  const DexEntry &d = DEX_TBL[speciesId];
  prevSpeciesId = speciesId;
  int16_t next = d.evolvesTo;

  // V8.5 : seules les branches réellement liées au niveau restent ici.
  // Tyrogue évolue au niveau 20 selon Attaque / Défense.
  if (speciesId == 236) {
    uint16_t a = atkStat(), df = defStat();
    next = (a > df) ? 106 : (df > a) ? 107 : 237;
  }

  // L'individu change de forme : sa place dans la boite doit etre remplacee,
  // pas dupliquee. L'ancienne forme reste connue dans le Pokedex (dexReg).
  dexCaught[(prevSpeciesId - 1) >> 3] &= ~(1 << ((prevSpeciesId - 1) & 7));
  dexCaught[(next - 1) >> 3] |= (1 << ((next - 1) & 7));
  speciesId = next;
  registerSpecies(speciesId);
  sfxPlay(SFX_EVOLVE);
  evolveUntil = millis() + EVOLVE_ANIM_MS;
  // Le profil appartient a l'individu, pas a toutes les formes de sa chaine.
  // Sans ce nettoyage, un ancien profil de la forme precedente restait en NVS.
  removeStoredProfile(prevSpeciesId);
  save();
}

void Pet::feed() {
  feedBerry(0);
}

void Pet::feedBerry(uint8_t color) {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  if (lovesBerry(color)) {
    fullness = clamp100(fullness + 35);
    joy = clamp100(joy + 10);
    heartUntil = millis() + HEART_MS;  // "le encanta!"
    berryKnown = true;                 // descubierto: se muestra en la ficha
    addBond(2);
  } else {
    fullness = clamp100(fullness + 25);
  }
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
}

void Pet::feedCandy() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  fullness = clamp100(fullness + 10);
  joy = clamp100(joy + 12);
  weight = clamp100(weight + 12);  // las chuches pasan factura
  eatUntil = millis() + EAT_ANIM_MS;
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
}

void Pet::playResult(uint8_t score) {
  if (ceremony != CER_NONE || isEgg()) return;
  uint8_t v = trSpe + score / 5;  // jugar entrena la velocidad
  trSpe = v > 100 ? 100 : v;
  joy = clamp100(joy + 5 + (score > 15 ? 30 : score * 2));
  energy = dropTo(energy, 10 + score / 2, 5);
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - score * 2;  // el ejercicio quema peso
  weight = burn > 0 ? burn : 0;
  if (score >= 5) heartUntil = millis() + HEART_MS;
  if (score > gameHi) gameHi = score;  // nuevo record
  addBond(2);
  registerCare();
  noteDailyGoal(DAILY_GOAL_PLAY, 1);
  save();
}

uint8_t Pet::applyCatchResult(uint8_t score) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = score / 3;
  if (gain > 12) gain = 12;
  uint8_t v = trSpe + gain;
  trSpe = v > 100 ? 100 : v;
  joy = clamp100(joy + 4 + (score > 12 ? 20 : score));
  energy = dropTo(energy, 8 + score / 3, 5);
  fullness = dropTo(fullness, 4, 5);
  int burn = (int)weight - score;
  weight = burn > 0 ? burn : 0;
  if (score >= 5) heartUntil = millis() + HEART_MS;
  if (score > catchHi) catchHi = score;
  addBond(1);
  registerCare();
  noteDailyGoal(DAILY_GOAL_CATCH, score);
  save();
  return gain;
}

uint8_t Pet::applyMemoResult(uint8_t rounds) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = rounds / 2;
  if (gain > 10) gain = 10;
  uint8_t v = trDef + gain;
  trDef = v > 100 ? 100 : v;
  joy = clamp100(joy + 5 + (rounds > 8 ? 18 : rounds * 2));
  energy = dropTo(energy, 6 + rounds / 2, 5);
  fullness = dropTo(fullness, 3, 5);
  if (rounds >= 4) heartUntil = millis() + HEART_MS;
  if (rounds > memoHi) memoHi = rounds;
  addBond(2);
  registerCare();
  noteDailyGoal(DAILY_GOAL_MEMO, rounds);
  save();
  return gain;
}

uint8_t Pet::applyCleanResult(uint8_t score) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = score / 2;
  if (gain > 18) gain = 18;
  hygiene = clamp100((int)hygiene + 20 + score * 3);
  joy = clamp100((int)joy + 3 + (score > 10 ? 12 : score));
  energy = dropTo(energy, 4 + score / 4, 8);
  if (poops && score >= 4) poops--;
  if (score >= 6) heartUntil = millis() + HEART_MS;
  if (score > cleanHi) cleanHi = score;
  addBond(1);
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
  return gain;
}

uint8_t Pet::applyTypeResult(uint8_t score) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = score / 4;
  if (gain > 10) gain = 10;
  uint8_t v = trAtk + gain;
  trAtk = v > 100 ? 100 : v;
  joy = clamp100((int)joy + 4 + (score > 12 ? 18 : score));
  energy = dropTo(energy, 5 + score / 3, 8);
  fullness = dropTo(fullness, 2, 5);
  if (score >= 5) heartUntil = millis() + HEART_MS;
  if (score > typeHi) typeHi = score;
  addBond(1);
  registerCare();
  noteDailyGoal(DAILY_GOAL_PLAY, 1);
  save();
  return gain;
}

bool Pet::applyPetEvent(uint8_t eventType) {
  if (ceremony != CER_NONE || isEgg()) return false;
  if (eventType == PET_EVENT_BERRY) {
    fullness = clamp100((int)fullness + 10);
    joy = clamp100((int)joy + 4);
  } else if (eventType == PET_EVENT_HEART) {
    joy = clamp100((int)joy + 6);
    addBond(1);
  } else if (eventType == PET_EVENT_SPARKLE) {
    joy = clamp100((int)joy + 5);
    if (energy <= hygiene) energy = clamp100((int)energy + 3);
    else hygiene = clamp100((int)hygiene + 3);
  } else {
    return false;
  }
  heartUntil = millis() + HEART_MS;
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
  return true;
}

uint8_t Pet::interactPet(bool eveningBonus) {
  if (ceremony != CER_NONE || isEgg() || sleeping) return PET_INTERACT_NONE;
  uint32_t nowMinute = ageMinutes ? ageMinutes : 1;
  if (lastPetInteractMinute && nowMinute < lastPetInteractMinute + 10) {
    heartUntil = millis() + HEART_MS;
    return PET_INTERACT_NONE;
  }
  lastPetInteractMinute = nowMinute;
  uint8_t result = PET_INTERACT_JOY;
  PetPersonality p = personality();
  int joyGain = (p == PERS_PLAYFUL) ? 4 : 2;
  joy = clamp100((int)joy + joyGain);
  if (p == PERS_LAZY) {
    energy = clamp100((int)energy + 2);
    result |= PET_INTERACT_ENERGY;
  }
  bool bondGain = eveningBonus || p == PERS_CALM || (p == PERS_BRAVE && battleWins > 0);
  if (bondGain) {
    uint8_t before = bond;
    addBond(1);
    if (bond > before) result |= PET_INTERACT_BOND;
  }
  heartUntil = millis() + HEART_MS;
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
  return result;
}

PetPersonality Pet::personality() const {
  if (isEgg()) return PERS_BALANCED;
  if (weight >= 72 || energy <= 20) return PERS_LAZY;
  if (battleWins >= 8 || bestBattleStreak >= 4) return PERS_BRAVE;
  if (catchHi >= 18 || memoHi >= 8 || gameHi >= 24 || trSpe >= 55) return PERS_PLAYFUL;
  if (bond >= 45 || streak >= 5) return PERS_CALM;
  return PERS_BALANCED;
}

// saco de entrenamiento: los golpes entrenan la fuerza. Devuelve la subida.
uint8_t Pet::trainStrength(uint16_t hits) {
  if (ceremony != CER_NONE || isEgg()) return 0;
  uint8_t gain = hits / 4;          // ~4 golpes = 1 punto de entrenamiento
  if (gain > 18) gain = 18;         // tope por sesion: la FUE se forja a fuego lento
  uint8_t v = trAtk + gain;
  trAtk = v > 100 ? 100 : v;
  energy = dropTo(energy, 12, 5);   // cansa
  fullness = dropTo(fullness, 5, 5);
  int burn = (int)weight - hits / 3;  // tambien quema peso
  weight = burn > 0 ? burn : 0;
  joy = clamp100(joy + 6);
  if (hits >= 20) heartUntil = millis() + HEART_MS;
  if (hits > strHi) strHi = hits;   // record de golpes
  addBond(2);
  registerCare();
  save();
  return gain;
}

BattleReward Pet::applyBattleWin(int16_t wildDex, bool closeWin) {
  BattleReward reward;
  if (ceremony != CER_NONE || isEgg()) return reward;
  if (wildDex < 1 || wildDex > DEX_COUNT) wildDex = 1;
  const DexEntry &wild = DEX_TBL[wildDex];
  reward.amount = (wild.rarity == R_RARO) ? 2 : 1;
  if (closeWin) reward.amount++;
  if (wild.bAtk >= wild.bDef && wild.bAtk >= wild.bSpe) {
    reward.stat = BATTLE_REWARD_DEF;
    trDef = clamp100((int)trDef + reward.amount);
  } else if (wild.bDef >= wild.bAtk && wild.bDef >= wild.bSpe) {
    reward.stat = BATTLE_REWARD_ATK;
    trAtk = clamp100((int)trAtk + reward.amount);
  } else {
    reward.stat = BATTLE_REWARD_SPE;
    trSpe = clamp100((int)trSpe + reward.amount);
  }
  battleWins++;
  battleStreak++;
  if (battleStreak > bestBattleStreak) bestBattleStreak = battleStreak;
  joy = clamp100((int)joy + 8 + (closeWin ? 4 : 0));
  energy = dropTo(energy, 8, 20);
  fullness = dropTo(fullness, 3, 10);
  addBond(closeWin ? 3 : 2);
  registerCare();
  noteDailyGoal(DAILY_GOAL_BATTLE, 1);
  save();
  return reward;
}

void Pet::applyBattleLoss() {
  if (ceremony != CER_NONE || isEgg()) return;
  battleLosses++;
  battleStreak = 0;
  joy = dropTo(joy, 12, 20);
  energy = dropTo(energy, 18, 20);
  fullness = dropTo(fullness, 4, 10);
  save();
}

bool Pet::isBoxFavorite(uint8_t list, int16_t dex) const {
  if (list >= 2 || dex < 1 || dex > DEX_COUNT) return false;
  for (uint8_t slot = 0; slot < 8; slot++)
    if (boxFavorites[list][slot] == dex) return true;
  return false;
}

uint8_t Pet::boxFavoriteCount(uint8_t list) const {
  if (list >= 2) return 0;
  uint8_t count = 0;
  for (uint8_t slot = 0; slot < 8; slot++)
    if (boxFavorites[list][slot] >= 1 && boxFavorites[list][slot] <= DEX_COUNT) count++;
  return count;
}

int16_t Pet::boxFavoriteAt(uint8_t list, uint8_t slot) const {
  if (list >= 2 || slot >= 8) return 0;
  return boxFavorites[list][slot];
}

bool Pet::toggleBoxFavorite(uint8_t list, int16_t dex) {
  if (list >= 2 || dex < 1 || dex > DEX_COUNT || !isCaught(dex)) return false;
  for (uint8_t slot = 0; slot < 8; slot++) {
    if (boxFavorites[list][slot] != dex) continue;
    for (uint8_t i = slot; i < 7; i++) boxFavorites[list][i] = boxFavorites[list][i + 1];
    boxFavorites[list][7] = 0;
    save();
    return true;
  }
  uint8_t count = boxFavoriteCount(list);
  if (count >= 8) return false;
  boxFavorites[list][count] = dex;
  save();
  return true;
}

void Pet::awardKantoBadge(uint8_t index) {
  if (index >= 8) return;
  uint8_t bit = (uint8_t)(1U << index);
  if (kantoBadges & bit) return;
  kantoBadges |= bit;
  save();
}

uint8_t Pet::expeditionEnergyCost(uint8_t minutes) {
  if (minutes == 15) return 12;
  if (minutes == 30) return 20;
  if (minutes == 60) return 32;
  return 0xFF;
}

bool Pet::expeditionActive(uint32_t nowEpoch) const {
  return expeditionEndEpoch != 0 && nowEpoch < expeditionEndEpoch;
}

bool Pet::expeditionReady(uint32_t nowEpoch) const {
  return expeditionEndEpoch != 0 && nowEpoch >= expeditionEndEpoch;
}

bool Pet::canReceiveExpeditionItem(ExpeditionItem item) const {
  if (item >= EXP_ITEM_COUNT || itemCounts[item] >= EXP_ITEM_MAX) return false;
  return item != EXP_ITEM_TRAIN || trAtk < 100 || trDef < 100 || trSpe < 100;
}

bool Pet::expeditionInventoryFull() const {
  for (uint8_t i = 0; i < EXP_ITEM_COUNT; i++) {
    if (canReceiveExpeditionItem((ExpeditionItem)i)) return false;
  }
  return true;
}

bool Pet::canStartExpedition(uint8_t minutes, uint32_t nowEpoch) const {
  uint8_t cost = expeditionEnergyCost(minutes);
  return cost != 0xFF && nowEpoch != 0 && !isEgg() && !sleeping && ceremony == CER_NONE &&
         expeditionEndEpoch == 0 && energy >= cost && !expeditionInventoryFull();
}

uint8_t Pet::expeditionTrainingChance(uint8_t minutes) const {
  uint8_t base = minutes == 15 ? 8 : minutes == 30 ? 15 : minutes == 60 ? 25 : 0;
  if (!base) return 0;
  uint16_t avg = ((uint16_t)fullness + joy + energy + hygiene) / 4;
  if (avg >= 80 && bond >= 50) return minutes == 15 ? 18 : minutes == 30 ? 30 : 45;
  if (avg >= 60 && bond >= 20) return minutes == 15 ? 13 : minutes == 30 ? 22 : 35;
  return base;
}

bool Pet::startExpedition(uint8_t minutes, uint32_t nowEpoch, uint8_t luckRoll, uint8_t itemRoll) {
  if (!canStartExpedition(minutes, nowEpoch)) return false;

  uint8_t cost = expeditionEnergyCost(minutes);
  ExpeditionItem reward = EXP_ITEM_NONE;
  if (canReceiveExpeditionItem(EXP_ITEM_TRAIN) && luckRoll < expeditionTrainingChance(minutes)) {
    reward = EXP_ITEM_TRAIN;
  } else {
    const ExpeditionItem common[] = { EXP_ITEM_SNACK, EXP_ITEM_ENERGY, EXP_ITEM_CARE };
    uint8_t first = itemRoll % 3;
    for (uint8_t i = 0; i < 3; i++) {
      ExpeditionItem candidate = common[(first + i) % 3];
      if (canReceiveExpeditionItem(candidate)) {
        reward = candidate;
        break;
      }
    }
  }
  if (reward == EXP_ITEM_NONE) return false;

  energy -= cost;
  expeditionEndEpoch = nowEpoch + (uint32_t)minutes * 60UL;
  expeditionRewardItem = reward;
  save();
  return true;
}

ExpeditionItem Pet::claimExpedition(uint32_t nowEpoch) {
  if (!expeditionReady(nowEpoch) || expeditionRewardItem >= EXP_ITEM_COUNT ||
      !canReceiveExpeditionItem((ExpeditionItem)expeditionRewardItem)) return EXP_ITEM_NONE;
  ExpeditionItem reward = (ExpeditionItem)expeditionRewardItem;
  itemCounts[reward]++;
  expeditionEndEpoch = 0;
  expeditionRewardItem = EXP_ITEM_NONE;
  save();
  return reward;
}

bool Pet::useExpeditionItem(ExpeditionItem item, int8_t trainingStat) {
  if (item >= EXP_ITEM_COUNT || itemCounts[item] == 0) return false;
  if (item == EXP_ITEM_SNACK) {
    fullness = clamp100((int)fullness + 25);
    joy = clamp100((int)joy + 5);
  } else if (item == EXP_ITEM_ENERGY) {
    energy = clamp100((int)energy + 30);
  } else if (item == EXP_ITEM_CARE) {
    hygiene = clamp100((int)hygiene + 30);
    if (poops > 0) poops--;
  } else {
    if (trainingStat == TRAIN_STAT_ATK && trAtk < 100) trAtk = clamp100((int)trAtk + 2);
    else if (trainingStat == TRAIN_STAT_DEF && trDef < 100) trDef = clamp100((int)trDef + 2);
    else if (trainingStat == TRAIN_STAT_SPE && trSpe < 100) trSpe = clamp100((int)trSpe + 2);
    else return false;
  }
  itemCounts[item]--;
  save();
  return true;
}

void Pet::play() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 25);
  energy = clamp100(energy - 10);
  fullness = clamp100(fullness - 5);
  heartUntil = millis() + HEART_MS;
  addBond(2);
  registerCare();
  save();
}

void Pet::toggleLight() {
  if (ceremony != CER_NONE) return;
  if (isEgg()) return;
  sleeping = !sleeping;
  save();
}

void Pet::clean() {
  if (ceremony != CER_NONE) return;
  poops = 0;
  hygiene = 100;
  addBond(1);
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
}

void Pet::caress() {
  if (ceremony != CER_NONE) return;
  if (isEgg() || sleeping) return;
  joy = clamp100(joy + 5);
  heartUntil = millis() + HEART_MS;
  addBond(1);
  registerCare();
  noteDailyGoal(DAILY_GOAL_CARE, 1);
  save();
}

void Pet::eggTap() {
  if (!isEgg() || starterPick) return;
  if (++eggTaps >= 3) hatch();
  else save();
}

PetMood Pet::mood() const {
  if (sleeping) return MOOD_SLEEPING;
  if (eating()) return MOOD_EATING;
  if (lowestStat() < 25) return MOOD_SAD;
  return MOOD_HAPPY;
}

static void petProfileKey(int16_t dex, char *out, size_t n) {
  snprintf(out, n, "p%03d", (int)dex);
}

bool Pet::hasStoredProfile(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  char key[8];
  petProfileKey(dex, key, sizeof(key));
  return prefs.getBytesLength(key) == sizeof(StoredPetProfile);
}

void Pet::removeStoredProfile(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return;
  char key[8];
  petProfileKey(dex, key, sizeof(key));
  prefs.remove(key);
}

uint16_t Pet::storedProfileCount() {
  uint16_t count = 0;
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++)
    if (hasStoredProfile(dex)) count++;
  return count;
}

void Pet::compactStoredProfiles() {
  // Les bitmaps dexCaught/dexReg sont la source de verite de la collection.
  // Les profils ne contiennent que l'etat de soin individuel et sont gardes
  // comme un cache borne afin de ne jamais saturer la petite partition NVS.
  uint16_t kept = hasStoredProfile(speciesId) ? 1 : 0;
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++) {
    if (dex == speciesId || !hasStoredProfile(dex)) continue;
    if (kept < STORED_PROFILE_LIMIT) kept++;
    else removeStoredProfile(dex);
  }
}

bool Pet::persistActiveSpecies() {
  if (prefs.putShort("dexn", speciesId) == sizeof(int16_t)) return true;

  // Les anciennes versions pouvaient remplir la NVS avec des centaines de
  // profils. Libere progressivement les profils inactifs et retente l'ecriture
  // critique. La Boite et le Pokedex restent conserves dans leurs bitmaps.
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++) {
    if (dex == speciesId || !hasStoredProfile(dex)) continue;
    removeStoredProfile(dex);
    if (prefs.putShort("dexn", speciesId) == sizeof(int16_t)) return true;
  }
  return false;
}

bool Pet::persistCollection() {
  auto writeCollection = [this]() {
    bool regOk = prefs.putBytes("dexreg", dexReg, sizeof(dexReg)) == sizeof(dexReg);
    bool caughtOk = prefs.putBytes("dexcgt", dexCaught, sizeof(dexCaught)) == sizeof(dexCaught);
    bool shinyOk = prefs.putBytes("dexsh", dexShinyReg, sizeof(dexShinyReg)) == sizeof(dexShinyReg);
    return regOk && caughtOk && shinyOk;
  };
  if (writeCollection()) return true;
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++) {
    if (dex == speciesId || !hasStoredProfile(dex)) continue;
    removeStoredProfile(dex);
    if (writeCollection()) return true;
  }
  return false;
}

void Pet::repairCaughtProfiles() {
  bool caughtChanged = false;
  bool registeredChanged = false;

  // Une reparation peut restaurer des donnees, mais ne doit jamais effacer un
  // bit de capture. Deux profils d'une meme chaine peuvent correspondre a deux
  // captures distinctes (par exemple Carapuce + Tortank).
  for (int16_t candidate = 1; candidate <= DEX_COUNT; candidate++) {
    if (!hasStoredProfile(candidate)) continue;
    uint8_t &slot = dexCaught[(candidate - 1) >> 3];
    uint8_t mask = (1 << ((candidate - 1) & 7));
    if ((slot & mask) == 0) {
      slot |= mask;
      caughtChanged = true;
    }
    uint8_t &regSlot = dexReg[(candidate - 1) >> 3];
    if ((regSlot & mask) == 0) {
      regSlot |= mask;
      registeredChanged = true;
    }
  }

  // Securite pour les sauvegardes tres anciennes sans profil individuel.
  if (speciesId >= 1 && speciesId <= DEX_COUNT && !isCaught(speciesId)) {
    dexCaught[(speciesId - 1) >> 3] |= (1 << ((speciesId - 1) & 7));
    caughtChanged = true;
  }
  if (caughtChanged) prefs.putBytes("dexcgt", dexCaught, sizeof(dexCaught));
  if (registeredChanged) prefs.putBytes("dexreg", dexReg, sizeof(dexReg));
}
void Pet::saveActiveProfile() {
  if (speciesId < 1 || speciesId > DEX_COUNT) return;
  if (!hasStoredProfile(speciesId) && storedProfileCount() >= STORED_PROFILE_LIMIT) {
    for (int16_t dex = 1; dex <= DEX_COUNT; dex++) {
      if (dex != speciesId && hasStoredProfile(dex)) {
        removeStoredProfile(dex);
        break;
      }
    }
  }
  StoredPetProfile p;
  p.fullness=fullness; p.joy=joy; p.energy=energy; p.hygiene=hygiene;
  p.poops=poops; p.weight=weight;
  p.geneAtk=geneAtk; p.geneDef=geneDef; p.geneSpe=geneSpe;
  p.trAtk=trAtk; p.trDef=trDef; p.trSpe=trSpe;
  p.flags=(berryKnown?1:0)|(shiny?2:0)|(sleeping?4:0);
  p.ageMinutes=ageMinutes; p.careMistakes=careMistakes; p.bond=bond;
  p.medals=medals; p.lastPetInteractMinute=lastPetInteractMinute;
  strncpy(p.nick,nick,sizeof(p.nick)-1);
  p.nick[sizeof(p.nick)-1]=0;
  char key[8];
  petProfileKey(speciesId,key,sizeof(key));
  if (prefs.putBytes(key,&p,sizeof(p)) != sizeof(p)) {
    Serial.printf("[SAVE] profil #%d non ecrit\n", speciesId);
  }
}

bool Pet::switchToCaught(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT || (!isCaught(dex) && !isRegistered(dex))) return false;
  if (ceremony != CER_NONE || isEgg()) return false;
  if (dex == speciesId) return true;

  // Garde l'individu actuel avant le changement.
  saveActiveProfile();

  char key[8];
  petProfileKey(dex,key,sizeof(key));
  StoredPetProfile p;
  size_t got=prefs.getBytes(key,&p,sizeof(p));
  if (got==sizeof(p) && p.version==1) {
    fullness=p.fullness; joy=p.joy; energy=p.energy; hygiene=p.hygiene;
    poops=p.poops; weight=p.weight;
    geneAtk=p.geneAtk; geneDef=p.geneDef; geneSpe=p.geneSpe;
    trAtk=p.trAtk; trDef=p.trDef; trSpe=p.trSpe;
    berryKnown=(p.flags&1); shiny=(p.flags&2); sleeping=(p.flags&4);
    ageMinutes=p.ageMinutes; careMistakes=p.careMistakes; bond=p.bond;
    medals=p.medals; lastPetInteractMinute=p.lastPetInteractMinute;
    strncpy(nick,p.nick,sizeof(nick)-1); nick[sizeof(nick)-1]=0;
  } else {
    // Première prise en charge d'un Pokémon capturé ou déjà élevé.
    fullness=80; joy=80; energy=80; hygiene=100; poops=0; weight=0;
    geneAtk=90+random(21); geneDef=90+random(21); geneSpe=90+random(21);
    trAtk=trDef=trSpe=0; berryKnown=false;
    shiny=isShinyRegistered(dex); sleeping=false;
    ageMinutes=0; careMistakes=0; bond=0; medals=0;
    lastPetInteractMinute=0; nick[0]=0;
  }

  speciesId=dex;
  prevSpeciesId=-1;
  ceremony=CER_NONE;
  evolveUntil=0;
  neglectTicks=0;
  mistakeCooldown=0;
  starterPick=false;
  registerSpecies(dex);
  saveActiveProfile();
  save();
  return true;
}

void Pet::save() {
  const bool activeSaved = persistActiveSpecies();
  const bool collectionSaved = persistCollection();
  saveActiveProfile();
  ticksSinceSave = 0;
  pendingSave = false;
  prefs.putUChar("full", fullness);
  prefs.putUChar("joy", joy);
  prefs.putUChar("ene", energy);
  prefs.putUChar("hyg", hygiene);
  prefs.putUChar("poop", poops);
  prefs.putUChar("wgt", weight);
  prefs.putUChar("gatk", geneAtk);
  prefs.putUChar("gdef", geneDef);
  prefs.putUChar("gspe", geneSpe);
  prefs.putUChar("tatk", trAtk);
  prefs.putUChar("tdef", trDef);
  prefs.putUChar("tspe", trSpe);
  prefs.putBool("bk", berryKnown);
  prefs.putBool("shy", shiny);
  prefs.putBool("eshy", eggShiny);
  prefs.putBool("stpk", starterPick);
  prefs.putUInt("age", ageMinutes);
  if (!activeSaved) Serial.printf("[SAVE] espece active #%d non ecrite\n", speciesId);
  prefs.putShort("eggT2", eggTarget);
  prefs.putUChar("crack", eggTaps);
  prefs.putUChar("mist", careMistakes);
  prefs.putBool("sleep", sleeping);
  prefs.putUChar("lend", lastEnd);
  if (lastSeenEpoch) prefs.putUInt("seen", lastSeenEpoch);
  if (!collectionSaved) Serial.println("[SAVE] collection non ecrite");
  prefs.putUShort("strk", streak);
  prefs.putUShort("bstrk", bestStreak);
  prefs.putUInt("cday", lastCareDay);
  prefs.putUChar("bond", bond);
  prefs.putUShort("medal", medals);
  prefs.putUShort("tmedal", totalMedals);
  prefs.putUShort("mstone", lastMilestone);
  prefs.putUShort("ghi", gameHi);
  prefs.putUShort("shi", strHi);
  prefs.putUShort("chi", catchHi);
  prefs.putUShort("mhi", memoHi);
  prefs.putUShort("clhi", cleanHi);
  prefs.putUShort("tyhi", typeHi);
  prefs.putUShort("bwin", battleWins);
  prefs.putUShort("bloss", battleLosses);
  prefs.putUShort("bstk", battleStreak);
  prefs.putUShort("bbstk", bestBattleStreak);
  prefs.putUChar("kbadge", kantoBadges);
  prefs.putUChar("cfrm", collectionFrame);
  prefs.putUChar("boxbg", boxBackground);
  prefs.putBytes("boxfav1", boxFavorites[0], sizeof(boxFavorites[0]));
  prefs.putBytes("boxfav2", boxFavorites[1], sizeof(boxFavorites[1]));
  prefs.putUInt("pimin", lastPetInteractMinute);
  prefs.putUChar("dxrew", dexRewardMask);
  prefs.putUInt("dgday", dailyGoalDay);
  prefs.putBytes("dgtype", dailyGoalType, sizeof(dailyGoalType));
  prefs.putBytes("dgprog", dailyGoalProgress, sizeof(dailyGoalProgress));
  prefs.putUChar("dgdone", dailyGoalDone);
  prefs.putBytes("items", itemCounts, sizeof(itemCounts));
  prefs.putUInt("exend", expeditionEndEpoch);
  prefs.putUChar("exrwd", expeditionRewardItem);
  prefs.putString("nick", nick);
  saveSequence++;
  prefs.putUInt("svseq", saveSequence);
  saveSdBackup();
}

static void loadCompatBitmap(Preferences &prefs, const char *key, uint8_t *dst, size_t dstLen) {
  memset(dst, 0, dstLen);
  size_t oldLen = prefs.getBytesLength(key);
  if (!oldLen) return;
  size_t n = oldLen < dstLen ? oldLen : dstLen;
  prefs.getBytes(key, dst, n);
}

void Pet::load() {
  saveSequence = prefs.getUInt("svseq", 0);
  fullness = prefs.getUChar("full", 80);
  joy = prefs.getUChar("joy", 80);
  energy = prefs.getUChar("ene", 80);
  hygiene = prefs.getUChar("hyg", 100);
  poops = prefs.getUChar("poop", 0);
  weight = prefs.getUChar("wgt", 0);
  geneAtk = prefs.getUChar("gatk", 0);
  geneDef = prefs.getUChar("gdef", 0);
  geneSpe = prefs.getUChar("gspe", 0);
  if (geneAtk == 0) {  // mascota anterior a los genes: tirada unica ahora
    geneAtk = 90 + random(21);
    geneDef = 90 + random(21);
    geneSpe = 90 + random(21);
  }
  trAtk = prefs.getUChar("tatk", 0);
  trDef = prefs.getUChar("tdef", 0);
  trSpe = prefs.getUChar("tspe", 0);
  berryKnown = prefs.getBool("bk", false);
  shiny = prefs.getBool("shy", false);
  eggShiny = prefs.getBool("eshy", false);
  starterPick = prefs.getBool("stpk", false);
  loadCompatBitmap(prefs, "dexsh", dexShinyReg, sizeof(dexShinyReg));
  ageMinutes = prefs.getUInt("age", 0);
  if (prefs.isKey("dexn")) {
    speciesId = prefs.getShort("dexn", -1);
    eggTarget = prefs.getShort("eggT2", 4);
  } else {
    // migracion desde la version con indices de flash (0-8)
    static const uint8_t OLD2DEX[9] = { 4, 5, 6, 1, 2, 3, 7, 8, 9 };
    int8_t old = prefs.getChar("spec", -1);
    speciesId = (old >= 0 && old < 9) ? OLD2DEX[old] : -1;
    int8_t oldT = prefs.getChar("eggT", 0);
    eggTarget = (oldT >= 0 && oldT < 9) ? OLD2DEX[oldT] : 4;
  }
  eggTaps = prefs.getUChar("crack", 0);
  careMistakes = prefs.getUChar("mist", 0);
  sleeping = prefs.getBool("sleep", false);
  lastEnd = prefs.getUChar("lend", CER_NONE);
  loadCompatBitmap(prefs, "dexreg", dexReg, sizeof(dexReg));
  loadCompatBitmap(prefs, "dexcgt", dexCaught, sizeof(dexCaught));
  // La collection appartient uniquement a cette sauvegarde. Les profils de
  // soin individuels ne doivent jamais fabriquer de captures.
  compactStoredProfiles();
  persistCollection();
  streak = prefs.getUShort("strk", 0);
  bestStreak = prefs.getUShort("bstrk", 0);
  lastCareDay = prefs.getUInt("cday", 0);
  bond = prefs.getUChar("bond", 0);
  medals = prefs.getUShort("medal", 0);
  totalMedals = prefs.getUShort("tmedal", 0);
  lastMilestone = prefs.getUShort("mstone", 0);
  gameHi = prefs.getUShort("ghi", 0);
  strHi = prefs.getUShort("shi", 0);
  catchHi = prefs.getUShort("chi", 0);
  memoHi = prefs.getUShort("mhi", 0);
  cleanHi = prefs.getUShort("clhi", 0);
  typeHi = prefs.getUShort("tyhi", 0);
  battleWins = prefs.getUShort("bwin", 0);
  battleLosses = prefs.getUShort("bloss", 0);
  battleStreak = prefs.getUShort("bstk", 0);
  bestBattleStreak = prefs.getUShort("bbstk", 0);
  kantoBadges = prefs.getUChar("kbadge", 0);
  collectionFrame = prefs.getUChar("cfrm", 0);
  if (collectionFrame >= unlockedCollectionFrameCount()) collectionFrame = 0;
  boxBackground = prefs.getUChar("boxbg", 0);
  if (boxBackground >= 16) boxBackground = 0;
  memset(boxFavorites, 0, sizeof(boxFavorites));
  prefs.getBytes("boxfav1", boxFavorites[0], sizeof(boxFavorites[0]));
  prefs.getBytes("boxfav2", boxFavorites[1], sizeof(boxFavorites[1]));
  // Nettoie les anciennes valeurs invalides et compacte chaque liste sans
  // toucher aux captures. Une sauvegarde antérieure obtient deux listes vides.
  for (uint8_t list = 0; list < 2; list++) {
    int16_t clean[8] = {0};
    uint8_t used = 0;
    for (uint8_t slot = 0; slot < 8; slot++) {
      int16_t dex = boxFavorites[list][slot];
      if (dex < 1 || dex > DEX_COUNT || !isCaught(dex)) continue;
      bool duplicate = false;
      for (uint8_t i = 0; i < used; i++) if (clean[i] == dex) duplicate = true;
      if (!duplicate) clean[used++] = dex;
    }
    memcpy(boxFavorites[list], clean, sizeof(clean));
  }
  lastPetInteractMinute = prefs.getUInt("pimin", 0);
  dexRewardMask = prefs.getUChar("dxrew", 0);
  dailyGoalDay = prefs.getUInt("dgday", 0);
  size_t gotTypes = prefs.getBytes("dgtype", dailyGoalType, sizeof(dailyGoalType));
  size_t gotProg = prefs.getBytes("dgprog", dailyGoalProgress, sizeof(dailyGoalProgress));
  if (gotTypes != sizeof(dailyGoalType)) {
    dailyGoalType[0] = DAILY_GOAL_CARE;
    dailyGoalType[1] = DAILY_GOAL_PLAY;
    dailyGoalType[2] = DAILY_GOAL_CATCH;
  }
  if (gotProg != sizeof(dailyGoalProgress)) {
    dailyGoalProgress[0] = dailyGoalProgress[1] = dailyGoalProgress[2] = 0;
  }
  dailyGoalDone = prefs.getUChar("dgdone", 0);
  size_t gotItems = prefs.getBytes("items", itemCounts, sizeof(itemCounts));
  if (gotItems != sizeof(itemCounts)) memset(itemCounts, 0, sizeof(itemCounts));
  for (uint8_t i = 0; i < EXP_ITEM_COUNT; i++)
    if (itemCounts[i] > EXP_ITEM_MAX) itemCounts[i] = EXP_ITEM_MAX;
  expeditionEndEpoch = prefs.getUInt("exend", 0);
  expeditionRewardItem = prefs.getUChar("exrwd", EXP_ITEM_NONE);
  if (expeditionEndEpoch == 0 || expeditionRewardItem >= EXP_ITEM_COUNT) {
    expeditionEndEpoch = 0;
    expeditionRewardItem = EXP_ITEM_NONE;
  }
  prefs.getString("nick", nick, sizeof(nick));
  // siembra: la mascota actual cuenta como criada (guardados antiguos)
  if (speciesId >= 1) {
    registerSpecies(speciesId);
    // Migration V5 -> V6 : le Pokémon actif existant devient le premier profil.
    if (!hasStoredProfile(speciesId)) saveActiveProfile();
  }

  // Repare l'ancien bug du premier demarrage : une eclosion automatique
  // pouvait installer Evoli avant que le joueur valide son starter. Si Evoli
  // est le seul Pokemon obtenu, on revient proprement au choix du starter.
  uint16_t caughtTotal = 0;
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++) {
    if (dexCaught[(dex - 1) >> 3] & (1 << ((dex - 1) & 7))) caughtTotal++;
  }
  if (speciesId == 133 && caughtTotal <= 1 && battleWins == 0) {
    dexReg[(133 - 1) >> 3] &= ~(1 << ((133 - 1) & 7));
    dexCaught[(133 - 1) >> 3] &= ~(1 << ((133 - 1) & 7));
    speciesId = -1;
    prevSpeciesId = -1;
    eggTarget = 1;
    eggTaps = 0;
    ageMinutes = 0;
    starterPick = true;
    nick[0] = 0;
    save();
  }
}

void Pet::factoryReset() {
  prefs.clear();
  saveSequence = 0;
#ifdef ARDUINO
  if (sdReady) {
    SD_MMC.remove(SD_SAVE_TEMP);
    SD_MMC.remove(SD_SAVE_MAIN);
    SD_MMC.remove(SD_SAVE_BACKUP);
    SD_MMC.rmdir(SD_SAVE_DIR);
  }
#endif
}

bool Pet::saveSdBackup() {
#ifndef ARDUINO
  return false;
#else
  if (!sdReady) return false;
  SdPetSaveV1 data{};
  data.magic = SD_SAVE_MAGIC;
  data.version = SD_SAVE_VERSION;
  data.size = sizeof(data);
  data.sequence = saveSequence;
  data.fullness=fullness; data.joy=joy; data.energy=energy; data.hygiene=hygiene;
  data.poops=poops; data.weight=weight;
  data.geneAtk=geneAtk; data.geneDef=geneDef; data.geneSpe=geneSpe;
  data.trAtk=trAtk; data.trDef=trDef; data.trSpe=trSpe;
  data.flags=(berryKnown?1:0)|(shiny?2:0)|(eggShiny?4:0)|(starterPick?8:0)|(sleeping?16:0);
  data.ageMinutes=ageMinutes; data.speciesId=speciesId; data.eggTarget=eggTarget;
  data.eggTaps=eggTaps; data.careMistakes=careMistakes; data.lastEnd=lastEnd;
  data.lastSeenEpoch=lastSeenEpoch;
  memcpy(data.dexReg,dexReg,sizeof(dexReg));
  memcpy(data.dexShinyReg,dexShinyReg,sizeof(dexShinyReg));
  memcpy(data.dexCaught,dexCaught,sizeof(dexCaught));
  data.streak=streak; data.bestStreak=bestStreak; data.lastCareDay=lastCareDay;
  data.bond=bond; memcpy(data.nick,nick,sizeof(nick));
  data.medals=medals; data.totalMedals=totalMedals; data.lastMilestone=lastMilestone;
  data.gameHi=gameHi; data.strHi=strHi; data.catchHi=catchHi; data.memoHi=memoHi;
  data.cleanHi=cleanHi; data.typeHi=typeHi;
  data.battleWins=battleWins; data.battleLosses=battleLosses;
  data.battleStreak=battleStreak; data.bestBattleStreak=bestBattleStreak;
  data.kantoBadges=kantoBadges; data.collectionFrame=collectionFrame; data.boxBackground=boxBackground;
  memcpy(data.boxFavorites,boxFavorites,sizeof(boxFavorites));
  data.lastPetInteractMinute=lastPetInteractMinute; data.dexRewardMask=dexRewardMask;
  data.dailyGoalDay=dailyGoalDay;
  memcpy(data.dailyGoalType,dailyGoalType,sizeof(dailyGoalType));
  memcpy(data.dailyGoalProgress,dailyGoalProgress,sizeof(dailyGoalProgress));
  data.dailyGoalDone=dailyGoalDone; memcpy(data.itemCounts,itemCounts,sizeof(itemCounts));
  data.expeditionEndEpoch=expeditionEndEpoch; data.expeditionRewardItem=expeditionRewardItem;
  for (int16_t dex=1; dex<=DEX_COUNT && data.profileCount<STORED_PROFILE_LIMIT; dex++) {
    if (!hasStoredProfile(dex)) continue;
    char key[8]; petProfileKey(dex,key,sizeof(key));
    StoredPetProfile profile;
    if (prefs.getBytes(key,&profile,sizeof(profile)) != sizeof(profile)) continue;
    uint8_t slot=data.profileCount++;
    data.profileDex[slot]=dex;
    data.profiles[slot]=profile;
  }
  data.crc=sdSaveChecksum(data);

  SD_MMC.mkdir(SD_SAVE_DIR);
  SD_MMC.remove(SD_SAVE_TEMP);
  File file=SD_MMC.open(SD_SAVE_TEMP,FILE_WRITE);
  if (!file) return false;
  bool written=file.write(reinterpret_cast<const uint8_t *>(&data),sizeof(data))==sizeof(data);
  file.flush(); file.close();
  if (!written) { SD_MMC.remove(SD_SAVE_TEMP); return false; }
  SD_MMC.remove(SD_SAVE_BACKUP);
  if (SD_MMC.exists(SD_SAVE_MAIN) && !SD_MMC.rename(SD_SAVE_MAIN,SD_SAVE_BACKUP)) {
    SD_MMC.remove(SD_SAVE_TEMP); return false;
  }
  if (!SD_MMC.rename(SD_SAVE_TEMP,SD_SAVE_MAIN)) {
    if (SD_MMC.exists(SD_SAVE_BACKUP)) SD_MMC.rename(SD_SAVE_BACKUP,SD_SAVE_MAIN);
    return false;
  }
  return true;
#endif
}

void Pet::syncSdSave() {
#ifdef ARDUINO
  if (!sdReady) return;
  SdPetSaveV1 mainSave{}, backupSave{};
  bool mainOk=readSdSaveFile(SD_SAVE_MAIN,mainSave);
  bool backupOk=readSdSaveFile(SD_SAVE_BACKUP,backupSave);
  const SdPetSaveV1 *best=nullptr;
  if (mainOk) best=&mainSave;
  if (backupOk && (!best || backupSave.sequence>best->sequence)) best=&backupSave;
  if (!best || (!saveCreatedThisBoot && best->sequence<=saveSequence)) {
    saveSdBackup();
    return;
  }

  const SdPetSaveV1 &data=*best;
  fullness=data.fullness; joy=data.joy; energy=data.energy; hygiene=data.hygiene;
  poops=data.poops; weight=data.weight;
  geneAtk=data.geneAtk; geneDef=data.geneDef; geneSpe=data.geneSpe;
  trAtk=data.trAtk; trDef=data.trDef; trSpe=data.trSpe;
  berryKnown=data.flags&1; shiny=data.flags&2; eggShiny=data.flags&4;
  starterPick=data.flags&8; sleeping=data.flags&16;
  ageMinutes=data.ageMinutes;
  speciesId=(data.speciesId>=-1 && data.speciesId<=DEX_COUNT)?data.speciesId:-1;
  eggTarget=(data.eggTarget>=1 && data.eggTarget<=DEX_COUNT)?data.eggTarget:1;
  eggTaps=data.eggTaps; careMistakes=data.careMistakes; lastEnd=data.lastEnd;
  lastSeenEpoch=data.lastSeenEpoch;
  memcpy(dexReg,data.dexReg,sizeof(dexReg));
  memcpy(dexShinyReg,data.dexShinyReg,sizeof(dexShinyReg));
  memcpy(dexCaught,data.dexCaught,sizeof(dexCaught));
  streak=data.streak; bestStreak=data.bestStreak; lastCareDay=data.lastCareDay;
  bond=data.bond; memcpy(nick,data.nick,sizeof(nick)); nick[sizeof(nick)-1]=0;
  medals=data.medals; totalMedals=data.totalMedals; lastMilestone=data.lastMilestone;
  gameHi=data.gameHi; strHi=data.strHi; catchHi=data.catchHi; memoHi=data.memoHi;
  cleanHi=data.cleanHi; typeHi=data.typeHi;
  battleWins=data.battleWins; battleLosses=data.battleLosses;
  battleStreak=data.battleStreak; bestBattleStreak=data.bestBattleStreak;
  kantoBadges=data.kantoBadges; collectionFrame=data.collectionFrame; boxBackground=data.boxBackground;
  memcpy(boxFavorites,data.boxFavorites,sizeof(boxFavorites));
  lastPetInteractMinute=data.lastPetInteractMinute; dexRewardMask=data.dexRewardMask;
  dailyGoalDay=data.dailyGoalDay;
  memcpy(dailyGoalType,data.dailyGoalType,sizeof(dailyGoalType));
  memcpy(dailyGoalProgress,data.dailyGoalProgress,sizeof(dailyGoalProgress));
  dailyGoalDone=data.dailyGoalDone; memcpy(itemCounts,data.itemCounts,sizeof(itemCounts));
  expeditionEndEpoch=data.expeditionEndEpoch; expeditionRewardItem=data.expeditionRewardItem;
  for (int16_t dex=1; dex<=DEX_COUNT; dex++) removeStoredProfile(dex);
  for (uint8_t i=0; i<data.profileCount; i++) {
    int16_t dex=data.profileDex[i];
    if (dex<1 || dex>DEX_COUNT || data.profiles[i].version!=1) continue;
    char key[8]; petProfileKey(dex,key,sizeof(key));
    prefs.putBytes(key,&data.profiles[i],sizeof(StoredPetProfile));
  }
  prevSpeciesId=-1; ceremony=CER_NONE; saveSequence=data.sequence;
  saveLoadedFromNvs=true; saveCreatedThisBoot=false;
  save();
  Serial.printf("SAVE SD restauree seq=%lu captures=%u\n",
                (unsigned long)saveSequence,(unsigned)caughtCount());
#endif
}
