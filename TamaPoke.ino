// TamaPoke - tamagotchi pixel art inspirado en la gen 1
// para Waveshare ESP32-S3-Touch-AMOLED-1.75
//
// Librerias (Library Manager o repo de Waveshare):
//   - "GFX Library for Arduino" (moononournation), con soporte CO5300 QSPI
//   - "SensorLib" (Lewis He), driver tactil CST9217
//
// Placa: ESP32S3 Dev Module | Flash 16MB | PSRAM: OPI PSRAM | USB CDC On Boot: Enabled
//
// Los sprites y la tabla de especies se generan con tools/sprites.py (emit).

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include "Arduino_GFX_Library.h"
#include "TouchDrvCSTXXX.hpp"
#include "pin_config.h"
#include "species.h"
#include "dex.h"
#include "pet.h"
#include "sdmon.h"
#include "rtcbat.h"
#include "i18n.h"
#include "audio.h"
#include "battle.h"
#include "brand_logo.h"
#include "battle_ball_icon.h"
#include "battle_bases.h"
#include "battle_backgrounds.h"
#include "box_backgrounds.h"

// Version del firmware. Subir este numero en cada release (y manifest.json para
// el instalador web). Se muestra en la pantalla de ajustes y por serie al arrancar.
#define FW_VERSION "1.46.54-moretro3d-v9.92-boite-cadre-agrandi"
#define HELP_PAGE_COUNT 8
#define HELP_LINE_COUNT 6

Arduino_DataBus *bus = new Arduino_ESP32QSPI(
  LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3);
Arduino_CO5300 *panel = new Arduino_CO5300(
  bus, LCD_RESET, 0 /*rotation*/, LCD_WIDTH, LCD_HEIGHT, 6, 0, 0, 0);
// Framebuffer completo en PSRAM: dibujamos todo y hacemos flush() (sin parpadeo)
Arduino_Canvas *gfx = new Arduino_Canvas(LCD_WIDTH, LCD_HEIGHT, panel);

void drawBrandLogo(int16_t x0, int16_t y0) {
  for (uint16_t y = 0; y < BRAND_LOGO_H; y++) {
    for (uint16_t x = 0; x < BRAND_LOGO_W; x++) {
      uint16_t color = pgm_read_word(&BRAND_LOGO_PIXELS[(uint32_t)y * BRAND_LOGO_W + x]);
      gfx->drawPixel(x0 + x, y0 + y, color);
    }
  }
}

TouchDrvCST92xx touch;
Pet pet;

// sprite animado de la SD para la especie actual (si existe el archivo)
SdMon mon;          // sprite B/N (respaldo y minijuego si no hay PMD)
PmdMon pmd;         // sprite PMD multi-accion (pantalla principal)
PmdMon evoPmd;      // forma anterior, solo durante el parpadeo de evolucion
PmdMon wildPmd;     // rival salvaje en la pantalla de combate
int16_t monFor = -2;
bool monShinyFor = false;

// comportamiento del bicho en pantalla
struct {
  uint8_t mode = 0;     // 0 idle, 1 paseo, 2 gesto one-shot
  uint8_t act = PMD_IDLE;
  uint32_t t0 = 0;      // inicio de la animacion en curso
  uint32_t until = 0;   // fin del estado actual
  float x = 233, targetX = 233;
} beh;
#define PET_GROUND 304  // linea de suelo de la mascota
PmdMon galleryPmd;  // sprite grande de la vista detalle de la galeria (PMD/TPK2, legal)

// galeria pokedex
bool galleryOpen = false;
bool galleryDirty = false;
int galleryPage = 0;        // 10 paginas de 16
int16_t galleryDetail = 0;  // dex en vista detalle, 0 = rejilla
uint8_t galleryFilter = 0;  // 0 todos, 1 criados, 2 capturados

bool screenOff = false;       // pulsacion corta del boton PWR
bool cardOpen = false;        // ficha del bicho (deslizar vertical)
bool kbOpen = false;          // teclado para renombrar al bicho
char nameBuf[12] = "";
uint8_t nameLen = 0;
#define CARD_COUNT 9
uint8_t cardPage = 0;         // 0 profil, 1 caractere, 2 quotidien, 3 boite, 4 combat, 5 medailles, 6 progres, 7 expedition, 8 records
uint8_t boxPage = 0;
uint8_t boxSort = 0;          // 0 dex, 1 tipo, 2 criados primero
bool expeditionTrainChoiceOpen = false;
bool clockOpen = false;       // pantalla de ajuste de hora (deslizar abajo)
int clockH = 12, clockM = 0;  // hora en edicion
bool powerSave = false;       // ahorro opcional: off por defecto
bool darkMode = false;        // thème sombre manuel, persistant
uint8_t settingsPage = 0;     // 0 = réglages principaux, 1 = affichage/cadres
uint8_t boxBgPreview = 0;
bool helpOpen = false;
uint8_t helpPage = 0;
bool uiDirty = true;
bool cardDirty = true;
bool clockDirty = true;
bool helpDirty = true;
bool keyboardDirty = true;
bool gameMenuDirty = true;
bool battleDirty = true;
bool starterDirty = true;

// escena de bano: espuma sobre el bicho y limpieza al reventar
uint32_t bathUntil = 0;
bool bathPending = false;
struct { int16_t x, y; uint8_t r, ph; } bubbles[14];
uint32_t feedMenuUntil = 0;   // selector de comida abierto hasta este millis
uint32_t nextAmbientSoundAt = 0;

// minijuego "toques": mantener la pokeball en el aire
bool gameOpen = false;
bool gameMenuOpen = false;
uint8_t gameMode = 0;  // 0 ball, 1 catch, 2 memo
uint32_t gameOverUntil = 0;
float ballX, ballY, ballVX, ballVY, gamePetX;
uint8_t gameScore, gameMisses;
float hitX, hitY;             // ultimo golpe (anillo de impacto)
uint32_t hitTime = 0;
uint32_t ballLastHitAt = 0;
bool gameNewHi = false;
uint8_t gameGain = 0;
uint32_t catchUntil = 0, catchTargetUntil = 0;
int16_t catchX = 0, catchY = 0;
uint8_t catchIcon = 0;
uint8_t memoSeq[14] = { 0 };
uint8_t memoLen = 0, memoShow = 0, memoInput = 0, memoRounds = 0;
uint32_t memoNextAt = 0;
bool memoShowing = false;
int8_t memoActivePad = -1, memoFlashPad = -1, memoHintPad = -1;
bool memoFlashGood = false;
uint32_t memoFlashUntil = 0, memoFailUntil = 0, memoTurnUntil = 0;
int16_t cleanX[4] = { 0 }, cleanY[4] = { 0 };
bool cleanAlive[4] = { false };
uint32_t cleanUntil = 0, cleanSpawnAt = 0;
uint8_t cleanActive = 0;
uint8_t typeEnemy = TYPE_GRASS;
uint8_t typeChoice[3] = { TYPE_FIRE, TYPE_WATER, TYPE_GRASS };
uint8_t typeCorrect = 0;
uint32_t typeUntil = 0;

// saco de entrenamiento (entrena la fuerza)
bool sackOpen = false;
uint32_t sackUntil = 0, sackOverUntil = 0;
uint16_t sackHits = 0;
float sackShake = 0;
uint8_t sackGain = 0;
bool sackNewHi = false;

bool battleOpen = false;
bool battleResolved = false;
int16_t battleDex = 0;
uint8_t battleLevel = 1;
BattleStats battlePlayer = {};
BattleStats battleEnemy = {};
BattleResult battleResult = {};
BattleRuntime battleRun = {};
BattleTurnResult battleTurn = {};
BattleAction battleLastAction = BATTLE_ATTACK;
BattleReward battleReward = {};
char battleMsg[28] = "";
uint32_t battleAttackMenuUntil = 0;
bool battleCatchOffered = false;
bool battleCatchTried = false;
bool battleCatchDone = false;
bool battleCatchSuccess = false;
bool battleRespectCatch = false;
uint8_t battleCatchChance = 0;
bool battleLowHpWarned = false;
bool battleEnemyShiny = false;
uint32_t battleShinyFxUntil = 0;
uint8_t battlePlayerSex = 2, battleEnemySex = 2; // 0 mâle, 1 femelle, 2 sans sexe

#define WILD_COOLDOWN_MS (20UL * 60UL * 1000UL)
#define WILD_PROMPT_MS 20000UL
#define WILD_CHECK_MS 60000UL
uint32_t wildPromptUntil = 0;
uint32_t nextWildEligible = 0;
uint32_t lastWildCheck = 0;
int16_t wildPromptDex = 0;
uint8_t wildPromptLevel = 1;

#define PET_EVENT_COOLDOWN_MS (15UL * 60UL * 1000UL)
#define PET_EVENT_PROMPT_MS 18000UL
#define PET_EVENT_CHECK_MS 60000UL
uint32_t petEventUntil = 0;
uint32_t nextPetEventEligible = 0;
uint32_t lastPetEventCheck = 0;
uint8_t petEventType = PET_EVENT_BERRY;
uint32_t petEventFeedbackUntil = 0;
char petEventMsg[18] = "";

// las 9 especies con sprite propio en flash (respaldo sin SD): dex -> indice
int flashIdxForDex(int16_t dex) {
  static const int8_t IDX[10] = { -1, 3, 4, 5, 0, 1, 2, 6, 7, 8 };
  return (dex >= 1 && dex <= 9) ? IDX[dex] : -1;
}

// L'edition normale affiche les numeros nationaux 001 a 386 tels quels.
uint16_t displayedDexNumber(int16_t dex) { return dex; }

bool hasPreEvolution(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  for (int16_t i = 1; i <= DEX_COUNT; i++)
    if (DEX_TBL[i].evolvesTo == dex) return true;
  return false;
}

// Une seule configuration visuelle pour les 386 especes. Le cadrage se fait
// toujours sur la silhouette visible, puis cette reduction uniforme evite les
// gros sprites qui donnaient un rendu irregulier selon l'evolution.
uint8_t spriteSizePercent(int16_t dex) {
  (void)dex;
  return 92;
}

#define CX 233  // centro de la pantalla redonda
#define CY 233
#define PET_CY 202  // centro vertical del sprite

static const uint16_t INK_K = 0x18C4;  // spriteColor('k')

void loadPowerSave() {
  Preferences p;
  p.begin("tamapoke", true);
  powerSave = p.getBool("psave", false);
  p.end();
}

void setPowerSave(bool on) {
  powerSave = on;
  Preferences p;
  p.begin("tamapoke", false);
  p.putBool("psave", powerSave);
  p.end();
}

#define C565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
void loadDarkMode() {
  Preferences p;
  p.begin("tamapoke", true);
  darkMode = p.getBool("darkui", false);
  p.end();
}

void setDarkMode(bool on) {
  darkMode = on;
  Preferences p;
  p.begin("tamapoke", false);
  p.putBool("darkui", darkMode);
  p.end();
  uiDirty = cardDirty = galleryDirty = clockDirty = helpDirty = true;
}

// Palette dynamique de l'interface. Les sprites conservent leurs vraies couleurs.
uint16_t uiBg()    { return darkMode ? C565(0x0b,0x0e,0x18) : UI_BG_DAY; }
uint16_t uiPanel() { return darkMode ? C565(0x19,0x20,0x31) : UI_WHITE; }
uint16_t uiPanel2(){ return darkMode ? C565(0x22,0x2b,0x40) : C565(0xf7,0xf5,0xed); }
uint16_t uiInk()   { return darkMode ? C565(0xee,0xf1,0xff) : UI_INK; }
uint16_t uiSub()   { return darkMode ? C565(0x9f,0xaa,0xc6) : UI_TRACK; }
uint16_t uiLine()  { return darkMode ? C565(0x44,0x50,0x70) : UI_TRACK; }

// Choisit automatiquement un texte noir ou blanc selon la luminosité du fond.
// Évite les textes blancs illisibles sur les cartes claires en mode sombre.
uint16_t uiContrastText(uint16_t bg) {
  uint8_t r = ((bg >> 11) & 0x1F) << 3;
  uint8_t g = ((bg >> 5)  & 0x3F) << 2;
  uint8_t b = ( bg        & 0x1F) << 3;
  // Luminance perceptuelle entière ~ 0.299R + 0.587G + 0.114B
  uint16_t lum = (uint16_t)(r * 3 + g * 6 + b) / 10;
  return lum >= 145 ? C565(0x08,0x0a,0x0f) : C565(0xf4,0xf6,0xfb);
}



// botones de icono siguiendo el arco inferior de la pantalla redonda
// (los exteriores van mas altos para no salirse del circulo)
struct Btn {
  int16_t cx, cy;
  const char *const *icon;
  uint8_t frameSize;
  uint8_t iconSize;
  uint8_t hitRadius;
  int8_t iconDx;
  int8_t iconDy;
};
Btn buttons[4] = {
  // cx, cy, sprite, cadre, icone, tactile, dx, dy
  { 140, 390, SPR_ICON_FOOD,  52, 32, 36, 0, 0 },
  { 202, 404, SPR_ICON_PLAY,  52, 28, 36, 0, 0 },
  { 264, 404, SPR_ICON_LIGHT, 52, 32, 36, 0, 0 },
  { 326, 390, SPR_ICON_CLEAN, 52, 32, 36, 0, 0 },
};

// grietas del huevo (pixeles 'k' sobre el sprite)
static const uint8_t CRACK1[][2] = { {15,8},{16,9},{15,10} };
static const uint8_t CRACK2[][2] = { {11,13},{12,14},{11,15},{20,12},{19,13},{20,14} };
// estrellas del modo noche
static const uint16_t STARS[][2] = { {120,140},{330,120},{370,210},{95,230},{280,90},{160,95} };

bool wasPressed = false;
// Premier demarrage : choix de generation, puis starter via trois Pokeballs.
static const int16_t STARTER_DEX[3][3] = {
  { 1, 4, 7 },       // Kanto : Bulbizarre, Salameche, Carapuce
  { 152, 155, 158 }, // Johto : Germignon, Hericendre, Kaiminus
  { 252, 255, 258 }  // Hoenn : Arcko, Poussifeu, Gobou
};
uint8_t starterGeneration = 0; // 0 = choix 1G/2G/3G, sinon 1..3
int16_t starterPreviewDex = 0; // popup de confirmation
bool starterLanguageChosen = false; // langue obligatoire avant la generation
#define STARTER_GEN_Y 128
#define STARTER_GEN_H 64
#define STARTER_BALL_Y 226
#define STARTER_BALL_R 42
// boton-CTA de evolucion (centrado, mitad de pantalla)
#define EVO_BTN_W 256
#define EVO_BTN_H 64
#define EVO_BTN_X (CX - EVO_BTN_W / 2)
#define EVO_BTN_Y 172
// boton-CTA de despedida (mas ancho: lleva el nombre + frase)
#define FAR_BTN_W 408
#define FAR_BTN_H 58
#define FAR_BTN_X (CX - FAR_BTN_W / 2)
#define FAR_BTN_Y 176
// el CST9217 avisa por el pin INT cuando hay datos tactiles; lo usamos para no
// leer el bus I2C mientras el chip esta dormido (esa lectura se colgaba ~1s)
volatile bool gTouchIrq = false;
void IRAM_ATTR touchIsr() { gTouchIrq = true; }
uint32_t lastRender = 0;
uint32_t lastLoopStart = 0;
uint32_t perfRenderLastMs = 0, perfRenderMaxMs = 0;
uint32_t perfLoopLastMs = 0, perfLoopMaxMs = 0;
uint32_t perfRenderCount = 0, perfRenderSkipCount = 0;
// proteccion del AMOLED: atenuado por inactividad
uint32_t lastInteract = 0;
uint8_t dimStage = 0;        // 0 despierto, 1 atenuado (90s), 2 casi apagado (5min)
bool swallowGesture = false; // el toque que despierta no acciona nada
uint32_t ignoreTouchUntil = 0;
uint32_t holdStart = 0;     // pulsacion larga sobre el bicho
uint32_t confirmUntil = 0;  // dialogo "soltar?" activo hasta este millis
uint8_t choiceKind = 0;     // dialogue de decision : 0 aucun, 1 evolution
uint32_t choiceUntil = 0;   // se cierra solo a este millis
int16_t tX0, tY0, tXl, tYl; // gesto en curso (inicio y ultima posicion)
uint32_t tStart = 0;
bool holdFired = false;

void markUiDirty() {
  uiDirty = true;
  starterDirty = true;
  cardDirty = true;
  clockDirty = true;
  helpDirty = true;
  keyboardDirty = true;
  gameMenuDirty = true;
  battleDirty = true;
  galleryDirty = true;
}

void lockTouchBrief(uint16_t ms = 160) {
  uint32_t until = millis() + ms;
  if (until > ignoreTouchUntil) ignoreTouchUntil = until;
  wasPressed = false;
  swallowGesture = true;
}

const char *screenName() {
  if (pet.awaitingStarter()) return "starter";
  if (galleryOpen) return galleryDetail ? "gallery-detail" : "gallery-grid";
  if (gameOpen) return "game";
  if (sackOpen) return "sack";
  if (battleOpen) return battleResolved ? "battle-result" : "battle";
  if (kbOpen) return "keyboard";
  if (helpOpen) return "help";
  if (clockOpen) return "settings";
  if (cardOpen) return "card";
  if (gameMenuOpen) return "game-menu";
  return screenOff ? "screen-off" : "main";
}

bool staticScreenClean() {
  if (pet.awaitingStarter()) return !starterDirty;
  if (galleryOpen && !galleryDetail) return !galleryDirty;
  if (battleOpen && battleResolved) return !battleDirty;
  if (kbOpen) return !keyboardDirty;
  if (helpOpen) return !helpDirty;
  if (clockOpen) return !clockDirty;
  if (cardOpen) return !cardDirty;
  if (gameMenuOpen) return !gameMenuDirty;
  return false;
}

void setup() {
  Serial.setRxBufferSize(8192);  // la transferencia a SD llega en bloques de 2 KB
  Serial.begin(115200);
  // CRITICO: sin esto, Serial.print BLOQUEA el juego cuando no hay un
  // monitor serie abierto en el host (el bufer TX del USB CDC se llena
  // y nadie lo vacia) -> con timeout 0 los mensajes se descartan
  Serial.setTxTimeoutMs(0);
  Serial.printf("TamaPoke fw v%s\n", FW_VERSION);
  loadLang();  // idioma guardado (ES por defecto)
  Wire.begin(IIC_SDA, IIC_SCL);
  // CST9217 (tactil), AXP2101 (PMU) y PCF85063 (RTC) comparten este bus I2C.
  // Red de seguridad para PMU/RTC (SensorLib NO respeta este timeout en el
  // tactil; el cuelgue del tactil dormido se resuelve gateando por INT, ver
  // handleTouch).
  Wire.setTimeOut(50);

  // CRITICO: encender la alimentacion del panel (BLDO1=OLED VDD 3.3V) ANTES de
  // inicializar el display. Si el PMU se reseteo (drenaje total), este rail
  // queda OFF y la pantalla se ve negra aunque el resto de la placa funcione.
  pmuEnablePanel();

  // QSPI a 80MHz (por defecto 40): el flush del framebuffer es el cuello de
  // botella del fps (~56ms a 40MHz). Si el panel mostrara basura, bajar a 40M.
  if (!gfx->begin(80000000)) Serial.println("gfx->begin() fallo");
  // Premier frame complet avant d'allumer l'AMOLED : aucun flash noir. Ce
  // splash apparait tout de suite pendant l'initialisation SD/audio/sprites.
  panel->setBrightness(0);
  gfx->fillScreen(C565(0x10,0x18,0x2e));
  // Composition centree dans le disque 466x466. Le contenu reel du logo est
  // decale d'un demi-pixel a gauche : +1 px donne le centre optique exact.
  drawBrandLogo(CX - BRAND_LOGO_W / 2 + 1, 96);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  // 8 caracteres x 6 px x taille 3 = 144 px : centre exact a x=161.
  gfx->setCursor(CX - 70, 284); gfx->print("PokeTama");
  gfx->setTextColor(C565(0xff,0x3b,0x45));
  gfx->setTextSize(3);
  // 9 caracteres x 6 px x taille 3 = 162 px : centre exact a x=152.
  gfx->setCursor(CX - 79, 326); gfx->print("Moretro3D");
  gfx->flush();
  panel->setBrightness(120);

  touch.setPins(TP_RESET, TP_INT);
  bool touchOk = false;
  for (int i = 0; i < 3 && !touchOk; i++) {  // a veces falla al primer intento
    touchOk = touch.begin(Wire, 0x5A, IIC_SDA, IIC_SCL);
    if (!touchOk) delay(150);
  }
  if (!touchOk) Serial.println("CST9217 no detectado");
  // begin() deja el chip en modo comando (lee la identidad y no sale);
  // hace falta un reset por hardware para que vuelva a reportar toques
  touch.reset();
  touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
  touch.setMirrorXY(true, true);  // el panel esta montado girado 180 grados
  // INT activo-bajo: salta cuando hay datos. Gatea las lecturas I2C (ver loop)
  pinMode(TP_INT, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(TP_INT), touchIsr, FALLING);

  pet.begin();
  Serial.printf("SAVE %s spec=%d lv=%u wins=%u losses=%u streak=%u\n",
                pet.saveLoadedFromNvs ? "loaded" : "created",
                pet.speciesId, pet.level(), pet.battleWins, pet.battleLosses, pet.battleStreak);
  sdBegin();

  // reloj real: aplica el tiempo que estuvo apagado
  rtcBegin();
  batBegin();
  pwrSetup();
  uint32_t e = rtcEpoch();
  if (e == 0) {
    rtcSetEpoch(1767225600UL);  // RTC virgen: semilla (la hora absoluta da igual,
    e = rtcEpoch();             // solo importan las diferencias)
    Serial.println("RTC sin hora: sembrado, sin progresion offline esta vez");
  }
  pet.syncClock(e);
  loadPowerSave();
  loadDarkMode();

  audioBegin();  // ES8311 + I2S + amplificador (suena un jingle de arranque)
  uint8_t audioHour=sceneHour();
  uint8_t audioTheme=(audioHour>=7 && audioHour<13) ? 0 : ((audioHour>=13 && audioHour<20) ? 1 : 2);
  audioPreloadMusic(audioTheme);  // réserve la PSRAM avant sprites/miniatures
  thumbs.load();

  // Laisse profiter de l'ecran de marque 1,5 seconde supplementaire une fois
  // l'initialisation terminee. Le framebuffer reste stable : aucun flash noir.
  delay(1500);

  lastInteract = millis();
  ensureMon();
  render();
  lastRender = millis();
  updateBrightness(lastRender);
}

// carga/descarga el sprite de SD cuando cambia la especie
void ensureMon() {
  if (pet.speciesId == monFor && monShinyFor == pet.shiny && !sdDirty) return;
  sdDirty = false;
  monFor = pet.speciesId;
  monShinyFor = pet.shiny;
  mon.unload();
  pmd.unload();
  beh.x = beh.targetX = 233;
  beh.mode = 0;
  beh.until = 0;
  if (pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    pmd.load(pet.speciesId, pet.shiny);          // principal: PMD
    if (!pmd.loaded) mon.load(pet.speciesId, pet.shiny);  // respaldo: B/N
  }
}

bool mainScreenReadyForAmbientSound() {
  if (audioMode() < SOUND_LOW || screenOff || dimStage > 0) return false;
  // La musique reste continue dans toutes les pages et tous les menus. Elle ne
  // doit pas etre rechargee ni redemarree lors du retour a l'accueil.
  if (pet.sleeping || pet.ceremony) return false;
  if (battleOpen) return false;
  if (pet.evolving() || pet.wantEvolveButton()) return false;
  return true;
}

void maybePlayAmbientSound(uint32_t now) {
  if (!mainScreenReadyForAmbientSound()) {
    // Repart presque immediatement quand l'interruption legitime se termine.
    nextAmbientSoundAt = 0;
    return;
  }
  if (nextAmbientSoundAt == 0) nextAmbientSoundAt = now + 120;
  if (now < nextAmbientSoundAt) return;
  uint8_t hour=sceneHour();
  uint8_t theme=(hour>=7 && hour<13) ? 0 : ((hour>=13 && hour<20) ? 1 : 2);
  audioAmbientPlay(theme);
  nextAmbientSoundAt = now + 10;
}

uint16_t renderIntervalMs() {
  if (screenOff) return 5000;

  if (battleOpen) return battleResolved ? 210 : 125;
  if (gameOpen || sackOpen) return 78;

  // Pages statiques pilotées par Dirty : faible latence sans redraw permanent.
  if (galleryOpen || cardOpen || kbOpen || clockOpen || helpOpen || gameMenuOpen)
    return powerSave ? 400 : 135;

  // Le framebuffer 466x466 est envoye en entier au panneau. A 68 ms, un
  // nouveau flush pouvait commencer juste avant la fin du precedent pendant
  // manger, laver ou une interaction tactile : certaines revisions AMOLED
  // montraient alors une image noire tres breve. On conserve des animations
  // fluides, mais avec une marge DMA sure entre deux images completes.
  if (!powerSave) return 110;
  if (dimStage >= 2) return 650;
  if (dimStage >= 1) return 250;
  return 120;
}

bool lightSleepAllowed(uint32_t now) {
  if (!powerSave || usbPresent() || audioBusy() || Serial.available()) return false;
  // Sur certaines revisions AMOLED, le light-sleep coupe le bus/panel pendant
  // quelques millisecondes. Cela ressemblait a un refresh noir. On ne dort
  // donc plus tant qu'un seul pixel est visible, meme en mode attenue.
  if (!screenOff) return false;
  if (wasPressed || gTouchIrq || now < ignoreTouchUntil) return false;
  if (gameOpen || sackOpen || battleOpen || bathUntil) return false;
  if (pet.awaitingStarter() || feedMenuUntil || confirmUntil || choiceKind || wildPromptUntil || petEventUntil) return false;
  if (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart()) return false;
  if (galleryOpen || cardOpen || kbOpen || clockOpen || helpOpen || gameMenuOpen) return false;
  return true;
}

uint16_t lightSleepMs(uint32_t now) {
  if (!lightSleepAllowed(now)) return 0;
  uint16_t maxMs = 750;
  uint16_t ri = renderIntervalMs();
  uint32_t sinceRender = now - lastRender;
  if (sinceRender < ri) {
    uint32_t untilRender = ri - sinceRender;
    if (untilRender < maxMs) maxMs = untilRender;
  }
  if (maxMs < 40) return 0;
  return maxMs;
}

void maybeLightSleep(uint32_t now) {
  uint16_t ms = lightSleepMs(now);
  if (!ms) return;

  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)TP_INT, 0);
  esp_light_sleep_start();

  if (digitalRead(TP_INT) == LOW || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0) {
    gTouchIrq = true;
  }
}

void loop() {
  uint32_t now = millis();
  uint32_t loopStart = now;
  pet.update(now);

  // avisa con un sonido cuando el bicho pasa a estar listo para evolucionar
  // (incluye el caso de cumplir al despertar). canEvolveNow es false durmiendo.
  static bool wasEvoReady = false;
  bool evoReady = pet.wantEvolveButton();
  if (evoReady && !wasEvoReady) sfxPlay(SFX_MEDAL);
  wasEvoReady = evoReady;
  handleTouch();
  handleSerial();
  ensureMon();
  setPowerCacheInterval(powerSave ? 10000UL : 2000UL);
  pet.ensureDailyGoals();
  maybeOfferWildEncounter(now);
  maybeOfferPetEvent(now);
  maybePlayAmbientSound(now);

  // Die Expeditionskarte bleibt sonst als statischer Screen stehen. Ein
  // sekundenweises Dirty-Render ist nur aktiv, waehrend ihr Countdown sichtbar ist.
  static uint32_t lastExpeditionCardTick = 0;
  if (cardOpen && cardPage == 7 && now - lastExpeditionCardTick >= 1000UL) {
    lastExpeditionCardTick = now;
    cardDirty = true;
  }

  // pulsacion corta del PWR: pantalla on/off
  static uint32_t lastPwr = 0;
  if (now - lastPwr > 60) {
    lastPwr = now;
    if (pwrShortPressed()) {
      bool wasOff = screenOff;
      screenOff = !screenOff;
      if (!screenOff) {
        lastInteract = now;
        if (wasOff) {
          ignoreTouchUntil = now + 900;
          swallowGesture = true;
          wasPressed = false;
          // Le framebuffer precedent reste visible jusqu'au nouveau frame
          // complet. Le reveil PWR ne montre donc jamais un ecran noir.
          markUiDirty();
          render();
          lastRender = millis();
        }
      }
    }
  }

  updateBrightness(now);

  // vuelca el autoguardado periodico SOLO con la pantalla atenuada/apagada o
  // durmiendo: la escritura a NVS congela ~1s ambos cores (caché de flash off),
  // y aqui no hay animacion que se corte ni dedo esperando respuesta. Con 90s
  // de inactividad la pantalla ya atenua, asi que se vuelca enseguida; el uso
  // activo persiste igual por los guardados de cada accion (comer/jugar/...).
  if (pet.savePending() && (screenOff || dimStage >= 1 || pet.sleeping)) {
    pet.flushSave();
  }

  // anota la hora real cada 30 s (se persiste en cada save del juego)
  static uint32_t lastClock = 0;
  uint32_t clockPollMs = powerSave ? 60000UL : 30000UL;
  if (now - lastClock > clockPollMs) {
    lastClock = now;
    uint32_t e = rtcEpoch();
    if (e) pet.lastSeenEpoch = e;
  }

  static bool expeditionWasActive = false;
  bool expeditionActiveNow = pet.expeditionActive(pet.lastSeenEpoch);
  if (expeditionWasActive && !expeditionActiveNow && pet.expeditionEndEpoch) {
    sfxPlay(SFX_EXPEDITION_FOUND);
    if (cardOpen && cardPage == 7) cardDirty = true;
  }
  expeditionWasActive = expeditionActiveNow;

  // latido de salud cada 5 min (para el soak test; se descarta si no hay monitor)
  static uint32_t lastHealth = 0;
  uint32_t healthMs = powerSave ? 600000UL : 300000UL;
  if (now - lastHealth > healthMs) {
    lastHealth = now;
    Serial.printf("HEALTH up=%lus heap=%u min=%u rMax=%lums lMax=%lums screen=%s\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(), ESP.getMinFreeHeap(),
                  (unsigned long)perfRenderMaxMs, (unsigned long)perfLoopMaxMs, screenName());
    perfRenderMaxMs = 0;
    perfLoopMaxMs = 0;
  }

  // Juego/combate usan intervalos conservadores para que el redibujado no pise
  // el envio DMA del frame anterior; las pantallas estaticas se saltan si no
  // estan "dirty".
  if (now - lastRender >= renderIntervalMs()) {
    lastRender = now;
    uint32_t rt0 = millis();
    render();
    perfRenderLastMs = millis() - rt0;
    if (perfRenderLastMs > perfRenderMaxMs) perfRenderMaxMs = perfRenderLastMs;
    perfRenderCount++;
  }

  maybeLightSleep(millis());
  perfLoopLastMs = millis() - loopStart;
  if (perfLoopLastMs > perfLoopMaxMs) perfLoopMaxMs = perfLoopLastMs;
  lastLoopStart = loopStart;
}

// brillo segun sueno + inactividad (proteccion del AMOLED)
void updateBrightness(uint32_t now) {
  // los eventos visibles despiertan la pantalla solos
  if (pet.evolving() || pet.ceremony || pet.eating() || pet.showHeart()) {
    lastInteract = now;
  }
  uint32_t idle = now - lastInteract;
  dimStage = (idle > 300000) ? 2 : (idle > 90000) ? 1 : 0;
  // Luminosite stable : l'etat USB du PMU peut osciller une fraction de
  // seconde pendant la charge. Le suivre ici provoquait des sauts visibles
  // pris pour des flashs noirs. Seuls sommeil/inactivite/PWR la modifient.
  uint8_t target = pet.sleeping ? 25 : 160;
  if (dimStage == 1) target = pet.sleeping ? 10 : 60;
  else if (dimStage == 2) target = 8;
  if (screenOff) target = 0;
  static uint8_t current = 255;
  if (target != current) {
    current = target;
    panel->setBrightness(target);
  }
}

// ---------- consola serie (provision de SD + depuracion) ----------

void handleSerial() {
  if (!Serial.available()) return;
  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;
  if (sdSerialCommand(line)) return;

  if (line == "PERF") {
    Serial.printf("screen=%s render=%lums max=%lums count=%lu skip=%lu loop=%lums loopMax=%lums interval=%u dirty ui=%d card=%d clock=%d help=%d kb=%d menu=%d battle=%d gallery=%d\n",
                  screenName(),
                  (unsigned long)perfRenderLastMs, (unsigned long)perfRenderMaxMs,
                  (unsigned long)perfRenderCount, (unsigned long)perfRenderSkipCount,
                  (unsigned long)perfLoopLastMs, (unsigned long)perfLoopMaxMs,
                  renderIntervalMs(), uiDirty, cardDirty, clockDirty, helpDirty,
                  keyboardDirty, gameMenuDirty, battleDirty, galleryDirty);
    perfRenderMaxMs = 0;
    perfLoopMaxMs = 0;
    Serial.println("DONE");
  } else if (line == "HATCH") {
    pet.eggTap(); pet.eggTap(); pet.eggTap();
    Serial.println("DONE");
  } else if (line.startsWith("SPEC ")) {
    int n = line.substring(5).toInt();
    if (n >= 1 && n <= DEX_COUNT) {
      pet.prevSpeciesId = pet.speciesId;
      pet.speciesId = n;
      Serial.printf("especie #%d %s\n", n, DEX_TBL[n].name);
    }
    Serial.println("DONE");
  } else if (line.startsWith("LVL ")) {
    pet.ageMinutes = (uint32_t)line.substring(4).toInt() * MINUTES_PER_LEVEL;
    Serial.println("DONE");
  } else if (line.startsWith("TIME ")) {
    uint32_t e = (uint32_t)line.substring(5).toInt();
    rtcSetEpoch(e);
    pet.setClock(e);
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line.startsWith("RTCSET ")) {  // solo RTC (simular apagados en pruebas)
    rtcSetEpoch((uint32_t)line.substring(7).toInt());
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "TIME") {
    Serial.printf("rtc=%u\n", rtcEpoch());
    Serial.println("DONE");
  } else if (line == "GAL") {
    galleryOpen = !galleryOpen;
    galleryDetail = 0;
    galleryDirty = true;
    if (!galleryOpen) galleryPmd.unload();
    Serial.println("DONE");
  } else if (line == "EGGS") {
    // simula 20 tiradas de huevo (no cambia el estado del juego)
    for (int i = 0; i < 20; i++) {
      int16_t d = pet.pickEggSpecies();
      Serial.printf("%d:%s(r%u) ", d, DEX_TBL[d].name, DEX_TBL[d].rarity);
    }
    Serial.println();
    Serial.println("DONE");
  } else if (line == "SHINY") {  // alterna shiny del actual (pruebas)
    pet.shiny = !pet.shiny;
    Serial.printf("shiny=%d\n", pet.shiny);
    Serial.println("DONE");
  } else if (line.startsWith("NICK ")) {
    pet.rename(line.substring(5).c_str());
    Serial.printf("nick=%s\n", pet.nick);
    Serial.println("DONE");
  } else if (line == "CAREDAY") {  // simula un dia nuevo cuidado (pruebas)
    pet.setClock(pet.lastSeenEpoch + 86400);
    pet.caress();
    Serial.printf("streak=%u bond=%u medals=0x%X\n", pet.streak, pet.bond, pet.medals);
    Serial.println("DONE");
  } else if (line == "BEEP") {
    sfxPlay(SFX_HATCH);  // prueba de audio
    Serial.println("DONE");
  } else if (line == "WIPE") {
    pet.factoryReset();     // borra NVS y reinicia -> partida nueva (eleccion de inicial)
    Serial.println("DONE");
    delay(100);
    ESP.restart();
  } else if (line == "REG") {
    Serial.printf("pokedex %u/386:", pet.registeredCount());
    for (int i = 1; i <= DEX_COUNT; i++)
      if (pet.isRegistered(i)) Serial.printf(" %d", i);
    Serial.println();
    Serial.println("DONE");
  } else if (line == "SAVEINFO") {
    Serial.printf("fw=%s save=%s createdBoot=%d spec=%d level=%u egg=%d starter=%d age=%lu\n",
                  FW_VERSION, pet.saveLoadedFromNvs ? "loaded" : "created",
                  pet.saveCreatedThisBoot, pet.speciesId, pet.level(), pet.isEgg(),
                  pet.awaitingStarter(), (unsigned long)pet.ageMinutes);
    Serial.printf("battle wins=%u losses=%u streak=%u best=%u nick=%s\n",
                  pet.battleWins, pet.battleLosses, pet.battleStreak,
                  pet.bestBattleStreak, pet.nick);
    Serial.printf("records ball=%u catch=%u memo=%u sack=%u\n",
                  pet.gameHi, pet.catchHi, pet.memoHi, pet.strHi);
    Serial.println("DONE");
  } else if (line == "HEALTH") {
    Serial.printf("up=%lus heap=%u min=%u sd=%d mon=%d\n",
                  (unsigned long)(millis() / 1000), ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(), sdReady, pmd.loaded || mon.loaded);
    Serial.println("DONE");
  } else if (line == "STATS") {
    Serial.printf("spec=%d nv=%u com=%u fel=%u ene=%u lim=%u desc=%u sd=%d mon=%d bat=%d usb=%d rtc=%u\n",
                  pet.speciesId, pet.level(), pet.fullness, pet.joy, pet.energy,
                  pet.hygiene, pet.careMistakes, sdReady, mon.loaded,
                  batPercent(), usbPresent(), rtcEpoch());
    Serial.printf("peso=%u fue=%u def=%u vel=%u genes=%u/%u/%u tr=%u/%u/%u baya=%d\n",
                  pet.weight, pet.atkStat(), pet.defStat(), pet.speStat(),
                  pet.geneAtk, pet.geneDef, pet.geneSpe,
                  pet.trAtk, pet.trDef, pet.trSpe, pet.berryKnown);
    Serial.printf("shiny=%d streak=%u/%u bond=%u medals=0x%X(%u) nick=%s\n",
                  pet.shiny, pet.streak, pet.bestStreak, pet.bond, pet.medals,
                  pet.totalMedals, pet.nick);
    Serial.println("DONE");
  }
}

// ---------- entrada tactil ----------

bool inPetZone(int16_t x, int16_t y) {
  return x > 110 && x < 356 && y > 95 && y < 310;
}

// el toque se resuelve al LEVANTAR el dedo para distinguir tap de deslizar
void handleTouch() {
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  if (now < ignoreTouchUntil) {
    gTouchIrq = false;
    wasPressed = false;
    return;
  }
  if (now - lastPoll < (powerSave && screenOff ? 80UL : 20UL)) return;  // 50 Hz activo; menos si pantalla apagada
  lastPoll = millis();
  // solo tocamos el bus si el chip aviso por INT o si el dedo sigue abajo (hay
  // que detectar el levantamiento). Leer el CST9217 dormido se colgaba ~1s y
  // congelaba el loop entero; SensorLib no respeta el timeout de Wire.
  if (!gTouchIrq && !wasPressed) return;
  gTouchIrq = false;
  int16_t x, y;
  bool pressed = touch.getPoint(&x, &y, 1) > 0;

  // Pantalla apagada por PWR: no despertar por cualquier roce accidental.
  // PWR corto sigue despertando al instante; touch requiere mantener pulsado.
  if (screenOff) {
    if (pressed && !wasPressed) {
      tX0 = tXl = x;
      tY0 = tYl = y;
      tStart = now;
      holdFired = false;
      wasPressed = true;
    } else if (pressed) {
      tXl = x;
      tYl = y;
      if (!holdFired && now - tStart >= 700UL &&
          abs(tXl - tX0) < 42 && abs(tYl - tY0) < 42) {
        screenOff = false;
        dimStage = 0;
        lastInteract = now;
        holdFired = true;
        swallowGesture = true;
        markUiDirty();
        lockTouchBrief(320);
        sfxPlay(SFX_TAP);
      }
    } else {
      wasPressed = false;
      holdFired = false;
    }
    return;
  }

  // saco de entrenamiento: cada toque cuenta al instante (aporrear rapido)
  if (sackOpen) {
    if (pressed && !wasPressed) {
      lastInteract = millis();
      if (y < 72) sackOpen = false;  // tocar arriba = abandonar
      else sackTap();
    }
    wasPressed = pressed;
    return;
  }

  if (pressed && !wasPressed) {  // empieza el gesto
    tX0 = tXl = x;
    tY0 = tYl = y;
    tStart = millis();
    holdFired = false;
    swallowGesture = (dimStage > 0) || screenOff;  // si estaba a oscuras, solo despierta
    screenOff = false;
    lastInteract = millis();
    if (!swallowGesture && gameOpen && gameMode == 0) {
      gameTap(x, y);         // ball: cuenta al tocar, no al soltar
      swallowGesture = true; // evita doble tap al levantar el dedo
    } else if (!swallowGesture && gameOpen && gameMode == 1) {
      catchTap(x, y);        // juego de reflejos: cuenta al tocar, no al soltar
      swallowGesture = true; // evita doble tap al levantar el dedo
    }
  } else if (pressed) {  // sigue apoyado
    tXl = x;
    tYl = y;
    // pulsacion larga sin moverse sobre el bicho -> dialogo de soltar
    if (!holdFired && !swallowGesture && !galleryOpen && !cardOpen && !kbOpen && !clockOpen && !helpOpen && millis() - tStart > 3000 &&
        abs(tXl - tX0) < 30 && abs(tYl - tY0) < 30 && inPetZone(tX0, tY0) &&
        !pet.isEgg() && !confirmUntil && !pet.ceremony) {
      confirmUntil = millis() + 10000;
      holdFired = true;
    }
  } else if (wasPressed) {  // levanta el dedo: resolver gesto
    lastInteract = millis();
    int dx = tXl - tX0, dy = tYl - tY0;
    uint32_t dt = millis() - tStart;
    if (!holdFired && !swallowGesture) {
      if (abs(dx) > 80 && abs(dy) < 70 && dt < 800) onSwipe(dx > 0 ? 1 : -1);
      else if (abs(dy) > 80 && abs(dx) < 70 && dt < 800) onSwipeV(dy > 0 ? 1 : -1);
      else if (dt < 1500 && abs(dx) < 40 && abs(dy) < 40) onTap(tX0, tY0);
    }
  }
  wasPressed = pressed;
}

// deslizar vertical: abre/cierra la ficha del bicho
void openClock();  // prototipo
int16_t boxDexAt(uint16_t index);
uint8_t boxPageCount();
uint8_t currentDayPhase();
StrId dayPhaseTextId(uint8_t phase);
void startCleanGame();
void startTypeGame();
void cleanTap(int16_t x, int16_t y);
void typeTap(int16_t x, int16_t y);
void expeditionCardTap(int16_t x, int16_t y);

void onSwipeV(int dir) {
  if (helpOpen) { helpOpen = false; clockOpen = true; clockDirty = true; lockTouchBrief(); sfxPlay(SFX_TAP); return; }
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (wildPromptUntil && millis() < wildPromptUntil) return;
  if (wildPromptUntil) wildPromptUntil = 0;
  if (gameMenuOpen) return;
  if (gameOpen || galleryOpen || kbOpen || sackOpen || battleOpen || pet.ceremony) return;
  if (clockOpen) { clockOpen = false; markUiDirty(); lockTouchBrief(); sfxPlay(SFX_TAP); return; }
  if (cardOpen) {
    if (dir < 0) { cardOpen = false; expeditionTrainChoiceOpen = false; markUiDirty(); lockTouchBrief(); sfxPlay(SFX_TAP); }  // arriba cierra la ficha
    return;
  }
  if (dir > 0) {                    // deslizar abajo: ajustar hora
    if (!confirmUntil && !feedMenuUntil) openClock();
  } else if (!pet.isEgg() && !confirmUntil && !feedMenuUntil) {
    cardOpen = true;                // deslizar arriba: ficha
    cardPage = 0;
    cardDirty = true;
    lockTouchBrief();
    sfxPlay(SFX_MENU);
  }
}

// deslizar: dir +1 = hacia la derecha
void onSwipe(int dir) {
  if (helpOpen) {
    if (dir < 0 && helpPage + 1 < HELP_PAGE_COUNT) { helpPage++; helpDirty = true; }
    else if (dir > 0 && helpPage > 0) { helpPage--; helpDirty = true; }
    sfxPlay(SFX_MENU);
    return;
  }
  if (pet.awaitingStarter()) return;  // bloqueado durante la eleccion de inicial
  if (wildPromptUntil && millis() < wildPromptUntil) return;
  if (wildPromptUntil) wildPromptUntil = 0;
  if (gameMenuOpen) return;
  if (gameOpen || kbOpen || clockOpen || battleOpen) return;
  if (cardOpen) {  // dentro de la ficha: cambiar paginas
    int p = (int)cardPage + (dir > 0 ? -1 : 1);  // izquierda avanza
    uint8_t old = cardPage;
    cardPage = p < 0 ? 0 : (p >= CARD_COUNT ? CARD_COUNT - 1 : p);
    if (cardPage != old) { expeditionTrainChoiceOpen = false; cardDirty = true; sfxPlay(SFX_MENU); }
    return;
  }
  if (!galleryOpen) {
    if (!pet.ceremony && !confirmUntil) {
      galleryOpen = true;
      galleryPage = 0;
      galleryDetail = 0;
      galleryDirty = true;
      lockTouchBrief();
      sfxPlay(SFX_MENU);
    }
    return;
  }
  if (galleryDetail) {  // en detalle: volver a la rejilla
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    lockTouchBrief();
    return;
  }
  int np = galleryPage - dir;  // deslizar a la izquierda avanza pagina
  int maxPage = galleryPageCount() - 1;
  if (np < 0) {                // retroceder desde la primera = salir
    galleryOpen = false;
    galleryPmd.unload();
    markUiDirty();
    lockTouchBrief();
    sfxPlay(SFX_TAP);
    return;
  }
  if (np > maxPage) np = maxPage;
  if (np != galleryPage) {
    galleryPage = np;
    galleryDirty = true;
    sfxPlay(SFX_MENU);
  }
}

struct GameMenuTile {
  int16_t x, y, w, h;
  uint8_t game;
};

static const GameMenuTile GAME_MENU_TILES[5] = {
  { 88, 156, 138, 58, 0 },
  { 240, 156, 138, 58, 1 },
  { 88, 226, 138, 58, 2 },
  { 240, 226, 138, 58, 3 },
  { 94, 296, 278, 62, 4 },
};

int8_t gameMenuHit(int16_t x, int16_t y) {
  for (uint8_t i = 0; i < 5; i++) {
    const GameMenuTile &t = GAME_MENU_TILES[i];
    if (x >= t.x && x <= t.x + t.w && y >= t.y && y <= t.y + t.h) return i;
  }
  return -1;
}

void startGameMenuChoice(uint8_t idx) {
  switch (GAME_MENU_TILES[idx].game) {
    case 0: startGame(); break;
    case 1: startCatchGame(); break;
    case 2: startMemoGame(); break;
    case 3: startCleanGame(); break;
    case 4: startTypeGame(); break;
  }
}

void onTap(int16_t x, int16_t y) {
  // Serial.printf("TOUCH %d %d\n", x, y);  // diagnostico (silenciado: satura el log)
  if (pet.awaitingStarter()) {
    if (!starterLanguageChosen) {
      static const int16_t LANG_X[2] = { 50, 242 };
      static const int16_t LANG_Y[3] = { 148, 224, 300 };
      for (uint8_t row = 0; row < 3; row++) {
        for (uint8_t col = 0; col < 2; col++) {
          if (x >= LANG_X[col] && x <= LANG_X[col] + 174 &&
              y >= LANG_Y[row] && y <= LANG_Y[row] + 58) {
            setLang((Lang)(row * 2 + col));
            starterLanguageChosen = true;
            starterGeneration = 0;
            starterDirty = true;
            markUiDirty();
            lockTouchBrief();
            sfxPlay(SFX_TAP);
            return;
          }
        }
      }
      return;
    }
    if (starterPreviewDex > 0) {
      // Page plein ecran : confirmer a droite, revenir a gauche.
      if (x >= 242 && x <= 382 && y >= 358 && y <= 408) {
        pet.chooseStarter(starterPreviewDex);
        starterPreviewDex = 0;
      } else if (x >= 84 && x <= 224 && y >= 358 && y <= 408) {
        starterPreviewDex = 0;
      }
      starterDirty = true;
      markUiDirty();
      lockTouchBrief();
      sfxPlay(SFX_TAP);
      return;
    }
    if (starterGeneration == 0) {
      // Retour langue centre en bas.
      if (x >= 96 && x <= 370 && y >= 368 && y <= 414) {
        starterLanguageChosen = false;
        starterDirty = true;
        markUiDirty();
        lockTouchBrief();
        sfxPlay(SFX_TAP);
        return;
      }
      for (uint8_t i = 0; i < 3; i++) {
        int gy = STARTER_GEN_Y + i * 82;
        if (x >= 82 && x <= 384 && y >= gy && y <= gy + STARTER_GEN_H) {
          starterGeneration = i + 1;
          starterDirty = true;
          markUiDirty();
          lockTouchBrief();
          sfxPlay(SFX_TAP);
          break;
        }
      }
    } else {
      // Retour generations centre en bas.
      if (x >= 96 && x <= 370 && y >= 368 && y <= 414) {
        starterGeneration = 0;
      } else {
        static const int BALL_X[3] = { 104, 233, 362 };
        for (uint8_t i = 0; i < 3; i++) {
          int dx = x - BALL_X[i], dy = y - STARTER_BALL_Y;
          if (dx * dx + dy * dy <= (STARTER_BALL_R + 12) * (STARTER_BALL_R + 12)) {
            starterPreviewDex = STARTER_DEX[starterGeneration - 1][i];
            speciesChirpPlay(starterPreviewDex);
            break;
          }
        }
      }
      starterDirty = true;
      markUiDirty();
      lockTouchBrief();
      sfxPlay(SFX_TAP);
    }
    return;
  }
  if (galleryOpen) {
    galleryTap(x, y);
    return;
  }
  if (kbOpen) {
    keyboardTap(x, y);
    return;
  }
  if (helpOpen) {
    helpTap(x, y);
    return;
  }
  if (clockOpen) {
    clockTap(x, y);
    return;
  }
  if (pet.ceremony) return;  // durante la despedida no hay botones
  if (cardOpen) {
    // Fleches tactiles en complement du glissement horizontal.
    int cardNavY = 354;
    if (y >= cardNavY && y <= cardNavY + 58 && x >= 76 && x <= 132) {
      if (cardPage > 0) {
        cardPage--;
        expeditionTrainChoiceOpen = false;
        cardDirty = true;
        sfxPlay(SFX_MENU);
      } else sfxPlay(SFX_DENY);
      lockTouchBrief();
      return;
    }
    if (y >= cardNavY && y <= cardNavY + 58 && x >= 334 && x <= 390) {
      if (cardPage + 1 < CARD_COUNT) {
        cardPage++;
        expeditionTrainChoiceOpen = false;
        cardDirty = true;
        sfxPlay(SFX_MENU);
      } else sfxPlay(SFX_DENY);
      lockTouchBrief();
      return;
    }
    if (x >= 136 && x <= 330 && y >= cardNavY && y <= cardNavY + 58) {
      cardOpen=false;
      markUiDirty();
      lockTouchBrief();
      sfxPlay(SFX_TAP);
      return;
    }
    if (cardPage == 0 && y < 84) openKeyboard();  // tocar el nombre = renombrar
    else if (cardPage == 3) {
      if (x >= 76 && x <= 170 && y >= 300 && y <= 350) {
        if (boxPage > 0) { boxPage--; cardDirty = true; }
        sfxPlay(SFX_TAP);
      } else if (x >= 296 && x <= 390 && y >= 300 && y <= 350) {
        uint8_t pages = boxPageCount();
        if (boxPage + 1 < pages) { boxPage++; cardDirty = true; }
        sfxPlay(SFX_TAP);
      } else {
        int col = (x - 84) / 78;
        int row = (y - 100) / 74;
        if (col >= 0 && col < 4 && row >= 0 && row < 2 &&
            x >= 84 + col * 78 && x <= 148 + col * 78 &&
            y >= 112 + row * 74 && y <= 176 + row * 74) {
          int16_t dex = boxDexAt((uint16_t)boxPage * 8 + row * 4 + col);
          if (dex > 0) {
            cardOpen = false;
            galleryOpen = true;
            galleryDetail = dex;
            galleryDirty = true;
            lockTouchBrief();
            galleryPmd.load(dex, pet.isShinyRegistered(dex));
            sfxPlay(SFX_TAP);
            speciesChirpPlay(dex);
          }
        } else if (y >= 400) {
          cardOpen = false;
          markUiDirty();
          lockTouchBrief();
        }
      }
    } else if (cardPage == 4 && y >= 264 && y <= 310 && x >= 82 && x <= 384) {
      cardOpen = false;
      markUiDirty();
      lockTouchBrief();
      startBattle();
    } else if (cardPage == 4 && y >= 312 && y <= 358 && x >= 82 && x <= 384) {
      cardOpen = false;            // boton ENTRENAR FUERZA
      markUiDirty();
      lockTouchBrief();
      startSack();
    } else if (cardPage == 7) {
      expeditionCardTap(x, y);
    } else if (y >= 400) {
      cardOpen = false;
      markUiDirty();
      lockTouchBrief();
      sfxPlay(SFX_TAP);
    }
    return;
  }
  if (gameOpen) {
    gameTap(x, y);
    return;
  }
  if (battleOpen) {
    battleTap(x, y);
    return;
  }
  if (petEventUntil && millis() < petEventUntil && inPetEventHit(x, y)) {
    acceptPetEvent();
    return;
  }
  if (gameMenuOpen) {
    int8_t hit = gameMenuHit(x, y);
    if (hit >= 0) { lockTouchBrief(140); startGameMenuChoice((uint8_t)hit); }
    else { gameMenuOpen = false; markUiDirty(); lockTouchBrief(); sfxPlay(SFX_TAP); }
    return;
  }
  if (wildPromptUntil) {
    if (millis() < wildPromptUntil) {
      bool fight = (x >= 93 && x <= 373 && y >= 226 && y <= 270);
      bool later = (x >= 93 && x <= 373 && y >= 278 && y <= 322);
      if (fight) {
        startBattleWith(wildPromptDex, wildPromptLevel);
      } else if (later) {
        wildPromptUntil = 0;
        scheduleNextWild(millis());
        sfxPlay(SFX_TAP);
      }
    } else {
      wildPromptUntil = 0;
    }
    return;
  }
  if (choiceKind) {          // dialogo de decision: boton accion (arriba) / mantener (abajo)
    bool b1 = (x >= 93 && x <= 373 && y >= 206 && y <= 258);  // accion
    bool b2 = (x >= 93 && x <= 373 && y >= 268 && y <= 320);  // mantener / quedaros
    if (choiceKind == 1) {                 // evolucion
      if (b1) { int16_t old = pet.speciesId; pet.evolve(); evoPmd.load(old, pet.shiny); }
      else if (b2) pet.declineEvolve();
    }
    choiceKind = 0;
    return;
  }
  if (confirmUntil) {        // dialogo "soltar?": SI / NO
    if (millis() < confirmUntil && x >= 118 && x <= 218 && y >= 252 && y <= 304) {
      pet.release();
    }
    confirmUntil = 0;
    return;
  }
  if (feedMenuUntil) {       // selector de comida
    if (millis() < feedMenuUntil && y >= 288 && y <= 352 && x >= 101 && x <= 365) {
      int item = (x - 101) / 66;
      if (item == 3) pet.feedCandy();
      else pet.feedBerry(item);
      sfxPlay(SFX_EAT);
    }
    feedMenuUntil = 0;
    return;
  }
  if (pet.isEgg()) {
    pet.eggTap();
    sfxPlay(SFX_TAP);
    return;
  }
  // boton de evolucion: abre el dialogo evolucionar/mantener
  if (pet.wantEvolveButton() && x >= EVO_BTN_X && x <= EVO_BTN_X + EVO_BTN_W &&
      y >= EVO_BTN_Y && y <= EVO_BTN_Y + EVO_BTN_H) {
    choiceKind = 1; choiceUntil = millis() + 12000;
    return;
  }
  for (int i = 0; i < 4; i++) {
    int dx = x - buttons[i].cx, dy = y - buttons[i].cy;
    int hit = buttons[i].hitRadius;
    if (dx * dx + dy * dy <= hit * hit) {
      Serial.printf("BTN %d\n", i);
      sfxPlay(SFX_TAP);
      if (i == 0) {
        if (!pet.sleeping) {
          feedMenuUntil = millis() + 6000;
          sfxPlay(SFX_MENU);
        }
      } else if (i == 1) {
        if (!pet.sleeping) {
          gameMenuOpen = true;
          gameMenuDirty = true;
          lockTouchBrief();
          sfxPlay(SFX_MENU);
        }
      } else if (i == 2) {
        pet.toggleLight();
      } else {
        startBath();
      }
      return;
    }
  }
  // tocar al bicho = caricia
  if (inPetZone(x, y)) {
    Serial.println("PET");
    uint8_t r = pet.interactPet(currentDayPhase() == 2);
    StrId msg = S_WAIT;
    if (r & PET_INTERACT_BOND) msg = S_BOND_GAIN;
    else if (r & PET_INTERACT_JOY) msg = S_HAPPY_FB;
    snprintf(petEventMsg, sizeof(petEventMsg), "%s", T(msg));
    petEventFeedbackUntil = millis() + 1600;
    if (!pet.sleeping) sfxPlay((r & PET_INTERACT_BOND) ? SFX_HEART : SFX_TAP);
    if (r != PET_INTERACT_NONE && audioMode() == SOUND_FULL) speciesChirpPlay(pet.speciesId);
  }
}

// ---------- render ----------

bool gNight = false;  // noche real (por hora) o durmiendo: lo fija render()
uint16_t inkColor() { return (darkMode || gNight) ? UI_INK_NIGHT : UI_INK; }

// ---------- escena de fondo: bioma del tipo + hora real del RTC ----------

#define HORIZON 232  // linea donde el cielo se encuentra con el suelo

uint16_t lerp565(uint16_t a, uint16_t b, int i, int n) {
  if (n <= 0) return a;
  int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
  int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
  return (uint16_t)((((ar + (br - ar) * i / n) << 11)) |
                    (((ag + (bg - ag) * i / n) << 5)) | (ab + (bb - ab) * i / n));
}

// hora del dia 0-23 (de la hora real cacheada cada 30s; 13 si no hay reloj)
int sceneHour() {
  uint32_t e = pet.lastSeenEpoch;
  return e ? (int)((e / 3600) % 24) : 13;
}

uint8_t currentDayPhase() {
  int h = sceneHour();
  if (h >= 6 && h < 12) return 0;
  if (h >= 12 && h < 18) return 1;
  if (h >= 18 && h < 22) return 2;
  return 3;
}

StrId dayPhaseTextId(uint8_t phase) {
  switch (phase) {
    case 0: return S_MORNING;
    case 2: return S_EVENING;
    case 3: return S_NIGHT;
    default: return S_DAY;
  }
}

// suelo de cada bioma de dia (de noche se mezcla hacia el azul nocturno)
static const uint16_t BIOME_SOIL[6] = {
  C565(0x7e, 0xc0, 0x7f),  // 0 pradera
  C565(0xdc, 0xca, 0x94),  // 1 playa (arena)
  C565(0x4f, 0x8a, 0x55),  // 2 bosque
  C565(0x8a, 0x55, 0x44),  // 3 volcan
  C565(0xa8, 0x90, 0x6a),  // 4 montana
  C565(0xe6, 0xee, 0xf5),  // 5 nieve
};

void drawClouds(uint32_t now, uint16_t col) {
  for (int k = 0; k < 2; k++) {
    int cx = (int)((now / 50 + k * 250) % 560) - 40;
    int cy = 70 + k * 34;
    gfx->fillCircle(cx, cy, 16, col);
    gfx->fillCircle(cx + 18, cy + 3, 13, col);
    gfx->fillCircle(cx - 15, cy + 4, 12, col);
  }
}

void drawScene(uint8_t biome, uint32_t now, bool night) {
  int h=sceneHour();
  // 4 phases explicites sur 24 h :
  // 06-07 lever, 08-17 jour, 18-19 coucher, 20-05 nuit.
  uint8_t phase = (h>=6 && h<8) ? 0 : (h>=8 && h<18) ? 1 : (h>=18 && h<20) ? 2 : 3;
  if (night) phase=3;

  uint16_t top,mid,bot;
  if (phase==0) {       // lever du soleil
    top=C565(0x3b,0x32,0x8f); mid=C565(0xb8,0x55,0x9b); bot=C565(0xff,0xb0,0x58);
  } else if (phase==1) {// jour
    top=C565(0x0d,0x68,0xb8); mid=C565(0x39,0xa7,0xe3); bot=C565(0xa8,0xdf,0xee);
  } else if (phase==2) {// coucher
    top=C565(0x52,0x20,0x72); mid=C565(0xd2,0x43,0x62); bot=C565(0xff,0x9a,0x38);
  } else {              // nuit
    top=C565(0x07,0x10,0x31); mid=C565(0x0e,0x25,0x5a); bot=C565(0x18,0x39,0x78);
  }

  // Bandes pixel-art douces, proches du rendu validé.
  for (int y=0;y<HORIZON;y+=8) {
    uint16_t c = y < HORIZON/2
      ? lerp565(top,mid,y,HORIZON/2)
      : lerp565(mid,bot,y-HORIZON/2,HORIZON/2);
    gfx->fillRect(0,y,466,8,c);
  }

  // Astres et ciel.
  if (phase==3) {
    gfx->fillCircle(366,76,25,C565(0xf5,0xef,0xc8));
    gfx->fillCircle(376,68,23,lerp565(top,mid,1,3)); // croissant
    for (auto &st:STARS) gfx->fillRect(st[0],st[1],3,3,UI_WHITE);
  } else if (phase==0) {
    gfx->fillCircle(358,HORIZON-18,32,C565(0xff,0xd0,0x58));
    drawClouds(now,C565(0xff,0xc0,0xb8));
  } else if (phase==1) {
    gfx->fillCircle(370,78,25,C565(0xff,0xe2,0x58));
    drawClouds(now,C565(0xf7,0xfb,0xff));
  } else {
    gfx->fillCircle(360,HORIZON-14,34,C565(0xff,0xc1,0x45));
    drawClouds(now,C565(0xeb,0x7b,0x76));
  }

  uint8_t b=biome<6?biome:0;
  uint16_t soil=BIOME_SOIL[b];
  if (phase==3) soil=lerp565(soil,C565(0x0b,0x16,0x34),10,16);
  else if (phase==2) soil=lerp565(soil,C565(0x72,0x3c,0x58),4,16);
  else if (phase==0) soil=lerp565(soil,C565(0x78,0x5c,0x80),3,16);

  // Habitat principal de l'espèce.
  if (b==1) { // eau/plage
    uint16_t sea=(phase==3)?C565(0x13,0x3d,0x68):C565(0x2e,0x83,0xb9);
    gfx->fillRect(0,HORIZON-38,466,38,sea);
    for(int i=0;i<4;i++){
      int yy=HORIZON-32+i*8;
      uint16_t wc=(phase==3)?C565(0x6b,0x8f,0xb8):C565(0xc8,0xf0,0xf8);
      gfx->fillRect((45+i*107+(now/80)%42)%430,yy,36,2,wc);
    }
  }

  gfx->fillRect(0,HORIZON,466,466-HORIZON,soil);
  uint16_t dk=lerp565(soil,C565(0x08,0x0e,0x1c),phase==3?11:7,16);

  if (b==2) { // forêt
    for(int tx: {42,92,362,418}) {
      gfx->fillTriangle(tx,HORIZON-62,tx-20,HORIZON,tx+20,HORIZON,dk);
      gfx->fillTriangle(tx,HORIZON-82,tx-15,HORIZON-35,tx+15,HORIZON-35,dk);
    }
  } else if (b==3) { // volcan
    gfx->fillTriangle(78,HORIZON,36,HORIZON+42,120,HORIZON+42,dk);
    gfx->fillTriangle(390,HORIZON,346,HORIZON+42,434,HORIZON+42,dk);
    for(int e=0;e<5;e++)
      gfx->fillRect(105+e*64,HORIZON+12+(e&1)*7,4,4,C565(0xff,0x75,0x25));
  } else if (b==4) { // montagne
    gfx->fillTriangle(120,HORIZON-54,34,HORIZON,206,HORIZON,dk);
    gfx->fillTriangle(330,HORIZON-48,235,HORIZON,430,HORIZON,dk);
  } else if (b==5) { // neige
    uint16_t snow=(phase==3)?C565(0xa8,0xbb,0xd8):C565(0xf0,0xf6,0xff);
    gfx->fillRect(0,HORIZON,466,466-HORIZON,snow);
    for(int f=0;f<12;f++){
      int fx=(f*43+now/55)%466;
      int fy=(f*71+now/26)%HORIZON;
      gfx->fillRect(fx,fy,3,3,snow);
    }
  } else if (b==0) { // prairie
    for(int gx: {70,156,306,398})
      gfx->fillTriangle(gx,HORIZON+4,gx-7,HORIZON+22,gx+7,HORIZON+22,dk);
  }
}

void drawStarterPokeball(int cx, int cy, int r) {
  (void)r;
  // Meme vraie Poke Ball pixel-art 16x16 que dans le reste du firmware.
  // Echelle x5 : 80 px, centree dans chacune des trois zones tactiles.
  drawMap(SPR_ICON_PLAY, 16, cx - 40, cy - 40, 5, false);
}

void drawStarterThumbCentered(const uint8_t *b, int16_t dex, int cx, int cy, int maxSize) {
  if (!b) return;
  uint8_t w=b[0], h=b[1], n=b[2];
  const uint8_t *pal=b+3;
  const uint8_t *data=pal+n*2;
  int minX=w, minY=h, maxX=-1, maxY=-1;
  for (uint8_t y=0; y<h; y++) for (uint8_t x=0; x<w; x++) {
    if (data[(uint16_t)y*w+x] == 0xFF) continue;
    minX=min(minX,(int)x); minY=min(minY,(int)y);
    maxX=max(maxX,(int)x); maxY=max(maxY,(int)y);
  }
  if (maxX < minX || maxY < minY) return;
  int visibleW=maxX-minX+1, visibleH=maxY-minY+1;
  // La fiche starter doit respirer sur l'ecran rond : taille volontairement
  // plus sobre que l'accueil et le combat.
  int target=145*spriteSizePercent(dex)/100;
  int scale=min(maxSize, target/max(visibleW,visibleH));
  if (scale < 2) scale=2;
  int x0=cx-visibleW*scale/2-minX*scale;
  int y0=cy-visibleH*scale/2-minY*scale;
  for (uint8_t y=0; y<h; y++) {
    for (uint8_t x=0; x<w; x++) {
      uint8_t idx=data[(uint16_t)y*w+x];
      if (idx==0xFF || idx>=n) continue;
      uint16_t color=(uint16_t)pal[idx*2] | ((uint16_t)pal[idx*2+1]<<8);
      gfx->fillRect(x0+x*scale,y0+y*scale,scale,scale,color);
    }
  }
}

void drawStarterCentered(const char *text, int y, uint8_t size, uint16_t color) {
  gfx->setTextColor(color);
  gfx->setTextSize(size);
  gfx->setCursor(CX - (int)strlen(text) * 3 * size, y);
  gfx->print(text);
}

// Premier demarrage : generation -> Pokeball -> popup du starter -> confirmation.
void renderStarterSelect() {
  if (!starterDirty) { perfRenderSkipCount++; return; }
  starterDirty = false;
  gfx->fillScreen(uiBg());

  if (!starterLanguageChosen) {
    drawStarterCentered("PokeTama", 48, 3, uiInk());
    drawStarterCentered("Moretro3D", 86, 2, C565(0xe8,0x32,0x3f));
    static const char *LANG_NAMES[6] = {
      "ESPANOL", "ENGLISH", "FRANCAIS", "DEUTSCH", "ITALIANO", "PORTUGUES"
    };
    static const int16_t LANG_X[2] = { 50, 242 };
    static const int16_t LANG_Y[3] = { 148, 224, 300 };
    for (uint8_t row=0; row<3; row++) {
      for (uint8_t col=0; col<2; col++) {
        uint8_t lang=row*2+col;
        gfx->fillRoundRect(LANG_X[col],LANG_Y[row],174,58,16,uiPanel());
        gfx->drawRoundRect(LANG_X[col],LANG_Y[row],174,58,16,UI_TRACK);
        gfx->setTextColor(uiInk()); gfx->setTextSize(2);
        gfx->setCursor(LANG_X[col]+87-strlen(LANG_NAMES[lang])*6,LANG_Y[row]+21);
        gfx->print(LANG_NAMES[lang]);
      }
    }
    gfx->flush();
    return;
  }

  if (starterGeneration == 0) {
    drawStarterCentered(T(S_CHOOSE_GENERATION), 92, 2, 0x0000);
    static const uint16_t GEN_COLORS[3] = { 0xF800, 0xFD20, 0x07E0 };
    for (uint8_t i = 0; i < 3; i++) {
      int gy = STARTER_GEN_Y + i * 82;
      gfx->fillRoundRect(82, gy, 302, STARTER_GEN_H, 18, lerp565(GEN_COLORS[i], uiPanel(), 3, 8));
      gfx->drawRoundRect(82, gy, 302, STARTER_GEN_H, 18, GEN_COLORS[i]);
      char generation[20];
      snprintf(generation, sizeof(generation), T(S_GENERATION_FMT), i + 1);
      drawStarterCentered(generation, gy + 22, 2, 0x0000);
    }
    gfx->fillRoundRect(96,368,274,46,14,UI_TRACK);
    gfx->setTextColor(uiContrastText(UI_TRACK)); gfx->setTextSize(2);
    const char *backText=T(S_LAN_BACK);
    gfx->setCursor(CX-(int)strlen(backText)*6,383); gfx->print(backText);
  } else {
    char generation[20];
    snprintf(generation, sizeof(generation), T(S_GENERATION_FMT), starterGeneration);
    drawStarterCentered(generation, 112, 2, 0x0000);
    static const int BALL_X[3] = { 104, 233, 362 };
    static const uint16_t STARTER_COLORS[3] = {
      C565(0x35,0xb8,0x58), // plante
      C565(0xef,0x45,0x3a), // feu
      C565(0x38,0x88,0xe8)  // eau
    };
    for (uint8_t i = 0; i < 3; i++) {
      drawStarterPokeball(BALL_X[i], STARTER_BALL_Y, STARTER_BALL_R);
      const char *starterName=dexName(STARTER_DEX[starterGeneration - 1][i]);
      gfx->setTextColor(STARTER_COLORS[i]);
      gfx->setTextSize(2);
      gfx->setCursor(BALL_X[i] - strlen(starterName) * 6, 286);
      gfx->print(starterName);
    }
    drawStarterCentered(T(S_TOUCH_POKEBALL), 330, 1, 0x0000);
    gfx->fillRoundRect(96,368,274,46,14,UI_TRACK);
    gfx->setTextColor(uiContrastText(UI_TRACK)); gfx->setTextSize(2);
    const char *backText=T(S_LAN_BACK);
    gfx->setCursor(CX-(int)strlen(backText)*6,383); gfx->print(backText);
  }

  if (starterPreviewDex > 0) {
    const DexEntry &de = DEX_TBL[starterPreviewDex];
    // Vraie fiche plein ecran, claire et sans carte posee au milieu.
    gfx->fillScreen(UI_WHITE);
    const uint8_t *th = thumbs.get(starterPreviewDex);
    drawStarterThumbCentered(th, starterPreviewDex, CX, 190, 8);
    drawStarterCentered(dexName(starterPreviewDex), 302, 3, de.accent);
    // Zone sure du rond 1,75 pouce : aucun coin de bouton n'est coupe.
    gfx->fillRoundRect(84,358,140,50,14,UI_TRACK);
    gfx->fillRoundRect(242,358,140,50,14,UI_BAR_OK);
    gfx->setTextSize(2);
    const char *backText=T(S_LAN_BACK);
    gfx->setTextColor(uiContrastText(UI_TRACK));
    gfx->setCursor(154-(int)strlen(backText)*6,375); gfx->print(backText);
    const char *okText=T(S_VALIDATE);
    gfx->setTextColor(uiContrastText(UI_BAR_OK));
    gfx->setCursor(312-(int)strlen(okText)*6,375); gfx->print(okText);
  }
  gfx->flush();
}

void render() {
  if (staticScreenClean()) { perfRenderSkipCount++; return; }
  if (pet.awaitingStarter()) {  // primera partida: elegir inicial (prioridad total)
    renderStarterSelect();
    return;
  }
  if (galleryOpen) {
    renderGallery();
    return;
  }
  if (gameOpen) {
    renderGame();
    return;
  }
  if (sackOpen) {
    renderSack();
    return;
  }
  if (battleOpen) {
    renderBattle();
    return;
  }
  if (kbOpen) {
    renderKeyboard();
    return;
  }
  if (helpOpen) {
    renderHelp();
    return;
  }
  if (clockOpen) {
    renderClock();
    return;
  }
  if (cardOpen) {
    renderCard();
    return;
  }
  int h = sceneHour();
  gNight = pet.sleeping || h < 6 || h >= 20;
  // drawScene cubre los 466x466 completos: sin fillScreen(NEGRO) previo para
  // que un flush DMA solapado nunca capture negro a medias (anti-parpadeo)
  drawScene(pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome, millis(), gNight);

  if (pet.ceremony) {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    const char *msg = (pet.ceremony == CER_FAREWELL) ? T(S_FAREWELL)
                      : (pet.ceremony == CER_RUNAWAY) ? T(S_RUNAWAY)
                                                      : T(S_GOODBYE);
    drawHeader(dexName(pet.speciesId), d.accent, msg);
    drawCeremony();
    gfx->flush();
    return;
  }

  if (pet.isEgg()) {
    drawHeader(T(S_EGG_HDR), inkColor(), eggMsg());
    int s = 5, x = CX - 16 * s, y = PET_CY - 16 * s;
    drawMap(SPR_EGG, SPRITE_H, x, y, s, false);
    if (pet.eggCracks() >= 1)
      for (auto &c : CRACK1) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggCracks() >= 2)
      for (auto &c : CRACK2) gfx->fillRect(x + c[0] * s, y + c[1] * s, s, s, INK_K);
    if (pet.eggRarity() >= R_RARO) {
      const char *rar = (pet.eggRarity() == R_LEGENDARIO) ? T(S_EGG_LEGEND) : T(S_EGG_RARE);
      gfx->setTextColor(pet.eggRarity() == R_LEGENDARIO ? UI_BAR_WARN : 0x4C98);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(rar) * 6, 316);
      gfx->print(rar);
    }
    char reg[24];
    snprintf(reg, sizeof(reg), T(S_POKEDEX_FMT), pet.registeredCount());
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : uiBg());
    gfx->setTextColor(inkColor());
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(reg) * 6, 348);
    gfx->print(reg);
  } else {
    const DexEntry &d = DEX_TBL[pet.speciesId];
    char name[28];
    const char *base = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
    snprintf(name, sizeof(name), "%s%s", pet.shiny ? "*" : "", base);
    drawHomeIdentity(name, pet.level(), gNight ? UI_INK_NIGHT : d.accent, statusMsg());
    // L'identité en haut remplace l'ancien badge qui encombrait le petit écran.

    drawPet();
    drawBath();
    drawPoops();
    // panel inferior: base limpia para barras y botones sobre el paisaje
    gfx->fillRect(0, 312, 466, 154, gNight ? UI_BG_NIGHT : uiBg());
    drawBars();
    drawButtons();
    drawCelebration();
    drawPetEvent();
    if (gameMenuOpen) drawGameMenu();
    if (pet.wantEvolveButton()) drawEvolveButton();
  }

  if (pet.sleeping) {
    gfx->setTextColor(UI_INK_NIGHT);
    gfx->setTextSize(3);
    gfx->setCursor(320, 130);
    gfx->print("Zz");
  }

  if (wildPromptUntil) {
    if (millis() > wildPromptUntil) wildPromptUntil = 0;
    else drawWildPrompt();
  }

  // selector de comida
  if (feedMenuUntil) {
    if (millis() > feedMenuUntil) {
      feedMenuUntil = 0;
    } else {
      gfx->fillRoundRect(101, 288, 264, 64, 14, uiPanel());
      gfx->drawRoundRect(101, 288, 264, 64, 14, inkColor());
      drawMap(SPR_ICON_FOOD, 16, 110, 296, 3, false);
      drawMap(SPR_ICON_BERRY_B, 16, 176, 296, 3, false);
      drawMap(SPR_ICON_BERRY_G, 16, 242, 296, 3, false);
      drawMap(SPR_ICON_CANDY, 16, 308, 296, 3, false);
    }
  }

  // dialogo "soltar?" (pulsacion larga sobre el bicho)
  if (confirmUntil) {
    if (millis() > confirmUntil) {
      confirmUntil = 0;
    } else {
      gfx->fillRoundRect(94, 168, 278, 152, 16, uiPanel());
      gfx->drawRoundRect(94, 168, 278, 152, 16, uiInk());
      char q[28];
      snprintf(q, sizeof(q), T(S_RELEASE_FMT), dexName(pet.speciesId));
      gfx->setTextColor(uiInk());
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(q) * 6, 196);
      gfx->print(q);
      gfx->fillRoundRect(118, 252, 100, 52, 12, UI_BAR_OK);
      gfx->setTextColor(UI_WHITE);
      gfx->setCursor(118 + (100 - (int)strlen(T(S_YES)) * 12) / 2, 270);
      gfx->print(T(S_YES));
      gfx->fillRoundRect(248, 252, 100, 52, 12, UI_BAR_BAD);
      gfx->setCursor(248 + (100 - (int)strlen(T(S_NO)) * 12) / 2, 270);
      gfx->print(T(S_NO));
    }
  }

  // dialogo de decision (evolucionar/mantener, despedirse/quedaros)
  if (choiceKind) {
    if (millis() > choiceUntil) choiceKind = 0;
    else drawChoiceDialog();
  }

  gfx->flush();
}

// ---------- minijuego: toques con la pokeball ----------

void drawGameMenu() {
  gameMenuDirty = false;
  gfx->fillRoundRect(78, 112, 310, 266, 18, uiPanel());
  gfx->drawRoundRect(78, 112, 310, 266, 18, uiInk());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  const char *title = T(S_GOAL_PLAY);
  gfx->setCursor(CX - strlen(title) * 9, 124);
  gfx->print(title);
  const char *labels[5] = { T(S_GAME_BALL), T(S_GAME_CATCH), T(S_GAME_MEMO), T(S_GAME_CLEAN), T(S_GAME_TYPE) };
  uint16_t cols[5] = { UI_BAR_BAD, UI_BAR_WARN, 0x4C98, UI_BAR_OK, 0xF3B7 };
  for (int i = 0; i < 5; i++) {
    const GameMenuTile &t = GAME_MENU_TILES[i];
    gfx->fillRoundRect(t.x, t.y, t.w, t.h, 14, cols[i]);
    gfx->drawRoundRect(t.x, t.y, t.w, t.h, 14, uiInk());
    gfx->setTextColor(uiContrastText(cols[i]));
    gfx->setTextSize(2);
    gfx->setCursor(t.x + (t.w - (int)strlen(labels[i]) * 12) / 2, t.y + (t.h - 16) / 2);
    gfx->print(labels[i]);
  }
}

void startGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameMenuOpen = false;
  gameOpen = true;
  gameMode = 0;
  gameOverUntil = 0;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  gameGain = 0;
  hitTime = 0;
  ballLastHitAt = 0;
  gamePetX = 233;
  respawnBall();
  sfxPlay(SFX_GAME_START);
}

void spawnCatchTarget() {
  catchX = 86 + random(294);
  catchY = 118 + random(206);
  catchIcon = random(3);
  uint32_t life = 980;
  uint32_t speedup = (uint32_t)gameScore * 35;
  if (speedup > 530) speedup = 530;
  life -= speedup;
  catchTargetUntil = millis() + life;
}

void startCatchGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameMenuOpen = false;
  gameOpen = true;
  gameMode = 1;
  gameOverUntil = 0;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  gameGain = 0;
  catchUntil = millis() + 20000;
  spawnCatchTarget();
  sfxPlay(SFX_GAME_START);
}

void startMemoRound() {
  if (memoLen < 14) memoSeq[memoLen++] = random(4);
  memoShow = 0;
  memoInput = 0;
  memoActivePad = -1;
  memoHintPad = -1;
  memoShowing = true;
  memoNextAt = millis() + 350;
  memoTurnUntil = 0;
}

void startMemoGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameMenuOpen = false;
  gameOpen = true;
  gameMode = 2;
  gameOverUntil = 0;
  gameScore = 0;
  gameNewHi = false;
  gameGain = 0;
  memoLen = 0;
  memoRounds = 0;
  memoFlashPad = -1;
  memoHintPad = -1;
  memoFlashUntil = memoFailUntil = memoTurnUntil = 0;
  startMemoRound();
  sfxPlay(SFX_GAME_START);
}

void spawnCleanSpot() {
  for (uint8_t i = 0; i < 4; i++) {
    if (cleanAlive[i]) continue;
    cleanX[i] = 88 + random(290);
    cleanY[i] = 122 + random(224);
    cleanAlive[i] = true;
    cleanActive++;
    return;
  }
}

void startCleanGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameMenuOpen = false;
  gameOpen = true;
  gameMode = 3;
  gameOverUntil = 0;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  gameGain = 0;
  cleanActive = 0;
  for (uint8_t i = 0; i < 4; i++) cleanAlive[i] = false;
  cleanUntil = millis() + 18000;
  cleanSpawnAt = millis();
  spawnCleanSpot();
  sfxPlay(SFX_GAME_START);
}

static const uint8_t TYPE_DEF_POOL[] = {
  TYPE_GRASS, TYPE_FIRE, TYPE_WATER, TYPE_ELECTRIC, TYPE_ROCK, TYPE_GROUND,
  TYPE_FLYING, TYPE_POISON, TYPE_PSYCHIC, TYPE_GHOST, TYPE_DRAGON, TYPE_ICE
};
static const uint8_t TYPE_COUNTER_POOL[] = {
  TYPE_FIRE, TYPE_WATER, TYPE_GRASS, TYPE_GROUND, TYPE_WATER, TYPE_WATER,
  TYPE_ELECTRIC, TYPE_PSYCHIC, TYPE_BUG, TYPE_GHOST, TYPE_ICE, TYPE_FIRE
};
static const uint8_t TYPE_OPTION_POOL[] = {
  TYPE_NORMAL, TYPE_FIRE, TYPE_WATER, TYPE_ELECTRIC, TYPE_GRASS, TYPE_ICE,
  TYPE_FIGHTING, TYPE_POISON, TYPE_GROUND, TYPE_FLYING, TYPE_PSYCHIC, TYPE_BUG,
  TYPE_ROCK, TYPE_GHOST, TYPE_DRAGON
};

void nextTypeQuestion() {
  uint8_t q = (uint8_t)random(sizeof(TYPE_DEF_POOL));
  typeEnemy = TYPE_DEF_POOL[q];
  uint8_t correct = TYPE_COUNTER_POOL[q];
  typeCorrect = (uint8_t)random(3);
  for (uint8_t i = 0; i < 3; i++) typeChoice[i] = TYPE_NONE;
  typeChoice[typeCorrect] = correct;
  for (uint8_t i = 0; i < 3; i++) {
    if (i == typeCorrect) continue;
    uint8_t cand;
    do {
      cand = TYPE_OPTION_POOL[random(sizeof(TYPE_OPTION_POOL))];
    } while (cand == correct || cand == typeChoice[0] || cand == typeChoice[1] || cand == typeChoice[2] ||
             battleTypeEffectPct(cand, typeEnemy, TYPE_NONE) > 100);
    typeChoice[i] = cand;
  }
  typeUntil = millis() + 4200;
}

void startTypeGame() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  gameMenuOpen = false;
  gameOpen = true;
  gameMode = 4;
  gameOverUntil = 0;
  gameScore = 0;
  gameMisses = 0;
  gameNewHi = false;
  gameGain = 0;
  nextTypeQuestion();
  sfxPlay(SFX_GAME_START);
}

void respawnBall() {
  ballX = 112 + random(242);
  ballY = 82;
  float sp = 3.35f + gameScore * 0.14f;
  if (sp > 8.4f) sp = 8.4f;
  ballVX = random(2) ? sp : -sp;
  ballVX += ((int)random(9) - 4) * 0.28f;
  ballVY = 2.05f;
}

int ballHitRadius() {
  if (gameScore >= 20) return 38;
  if (gameScore >= 8) return 44;
  return 50;
}

void gameTap(int16_t x, int16_t y) {
  if (gameMode == 1) {
    catchTap(x, y);
    return;
  }
  if (gameMode == 2) {
    memoTap(x, y);
    return;
  }
  if (gameMode == 3) {
    cleanTap(x, y);
    return;
  }
  if (gameMode == 4) {
    typeTap(x, y);
    return;
  }
  if (gameOverUntil) return;
  if (y < 72) {  // tocar la cabecera = abandonar sin premio
    gameOpen = false;
    sfxPlay(SFX_TAP);
    return;
  }
  float dx = ballX - x, dy = ballY - y;
  int hitRadius = ballHitRadius();
  if (dx * dx + dy * dy < hitRadius * hitRadius) {  // toque a la bola!
    uint32_t now = millis();
    if (now - ballLastHitAt < 260 || ballVY < -0.25f) return;
    gameScore++;
    sfxPlay(SFX_MINIGAME_OK);
    float lift = 4.65f + (gameScore > 16 ? 1.7f : gameScore * 0.11f);
    ballVY = -lift;
    float drift = 0.38f + (gameScore >= 6 ? 0.08f : 0.0f) + (gameScore >= 14 ? 0.10f : 0.0f);
    float chaos = ((int)random(17) - 8) * 0.26f;
    ballVX += dx * drift + chaos;
    if (random(100) < 28) ballVX = -ballVX * (0.72f + random(45) * 0.01f);
    if (ballVX > 11.5f) ballVX = 11.5f;
    if (ballVX < -11.5f) ballVX = -11.5f;
    hitX = ballX;
    hitY = ballY;
    hitTime = now;
    ballLastHitAt = now;
  }
}

void finishCatchGame() {
  gameNewHi = (gameScore > pet.catchHi);
  gameGain = pet.applyCatchResult(gameScore);
  sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverUntil = millis() + 4000;
}

void catchTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  if (y < 72) { gameOpen = false; sfxPlay(SFX_TAP); return; }
  int dx = x - catchX, dy = y - catchY;
  if (dx * dx + dy * dy <= 52 * 52) {
    gameScore++;
    hitX = catchX;
    hitY = catchY;
    hitTime = millis();
    sfxPlay(SFX_MINIGAME_OK);
    spawnCatchTarget();
  } else if (++gameMisses >= 3) {
    finishCatchGame();
  } else {
    sfxPlay(SFX_MINIGAME_BAD);
  }
}

void finishMemoGame() {
  gameScore = memoRounds;
  gameNewHi = (memoRounds > pet.memoHi);
  gameGain = pet.applyMemoResult(memoRounds);
  sfxPlay(gameNewHi && memoRounds > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverUntil = millis() + 4000;
}

int memoPadAt(int16_t x, int16_t y) {
  const int16_t px[4] = { 142, 324, 142, 324 };
  const int16_t py[4] = { 164, 164, 318, 318 };
  for (int i = 0; i < 4; i++) {
    int dx = x - px[i], dy = y - py[i];
    if (dx * dx + dy * dy <= 54 * 54) return i;
  }
  return -1;
}

void memoPadSound(uint8_t pad) {
  if (pad < 4) sfxPlay((uint8_t)(SFX_MEMO_PAD_0 + pad));
}

void memoTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  if (y < 72) { gameOpen = false; sfxPlay(SFX_TAP); return; }
  if (memoShowing || memoFailUntil || millis() < memoTurnUntil) return;
  int pad = memoPadAt(x, y);
  if (pad < 0) return;
  if (pad != memoSeq[memoInput]) {
    memoFlashPad = pad;
    memoFlashGood = false;
    memoFlashUntil = millis() + 620;
    memoHintPad = memoSeq[memoInput];
    memoFailUntil = memoFlashUntil;
    sfxPlay(SFX_MINIGAME_BAD);
    return;
  }
  memoFlashPad = pad;
  memoFlashGood = true;
  memoFlashUntil = millis() + 180;
  memoPadSound((uint8_t)pad);
  memoInput++;
  if (memoInput >= memoLen) {
    memoRounds++;
    if (memoLen >= 14) finishMemoGame();
    else startMemoRound();
  }
}

void finishCleanGame() {
  gameNewHi = (gameScore > pet.cleanHi);
  gameGain = pet.applyCleanResult(gameScore);
  sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverUntil = millis() + 4000;
}

void cleanTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  if (y < 72) { gameOpen = false; sfxPlay(SFX_TAP); return; }
  for (uint8_t i = 0; i < 4; i++) {
    if (!cleanAlive[i]) continue;
    int dx = x - cleanX[i], dy = y - cleanY[i];
    if (dx * dx + dy * dy <= 38 * 38) {
      cleanAlive[i] = false;
      if (cleanActive) cleanActive--;
      gameScore++;
      hitX = cleanX[i];
      hitY = cleanY[i];
      hitTime = millis();
      sfxPlay(SFX_MINIGAME_OK);
      return;
    }
  }
  if (++gameMisses >= 3) finishCleanGame();
  else sfxPlay(SFX_MINIGAME_BAD);
}

void finishTypeGame() {
  gameNewHi = (gameScore > pet.typeHi);
  gameGain = pet.applyTypeResult(gameScore);
  sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
  gameOverUntil = millis() + 4000;
}

void typeTap(int16_t x, int16_t y) {
  if (gameOverUntil) return;
  if (y < 72) { gameOpen = false; sfxPlay(SFX_TAP); return; }
  int idx = -1;
  for (int i = 0; i < 3; i++) {
    int by = 210 + i * 60;
    if (x >= 70 && x <= 396 && y >= by - 8 && y <= by + 56) idx = i;
  }
  if (idx < 0) return;
  if ((uint8_t)idx == typeCorrect) {
    gameScore++;
    sfxPlay(SFX_MINIGAME_OK);
    nextTypeQuestion();
  } else {
    if (++gameMisses >= 3) finishTypeGame();
    else sfxPlay(SFX_MINIGAME_BAD);
  }
}

void stepGame() {
  float grav = 1.14f + gameScore * 0.046f;
  if (gameScore >= 5) grav += 0.14f;
  if (gameScore >= 12) grav += 0.18f;
  if (grav > 2.10f) grav = 2.10f;
  ballVX += sinf((millis() + gameScore * 97) * 0.018f) * 0.11f;
  if (random(100) < 7) ballVX += ((int)random(7) - 3) * 0.22f;
  ballVY += grav;
  ballX += ballVX;
  ballY += ballVY;
  // rebote en la pared circular
  float dx = ballX - CX, dy = ballY - CY;
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 205) {
    float nx = dx / d, ny = dy / d;
    float dot = ballVX * nx + ballVY * ny;
    if (dot > 0) {
      ballVX = (ballVX - 2 * dot * nx) * 1.05f;
      ballVY = (ballVY - 2 * dot * ny) * 0.88f;
      ballVX += ((int)random(9) - 4) * 0.18f;
      sfxPlay(SFX_BALL_BOUNCE);
    }
    ballX = CX + nx * 205;
    ballY = CY + ny * 205;
  }
  if (ballY > 384) {  // al suelo
    if (++gameMisses >= 3) {
      gameNewHi = (gameScore > pet.gameHi);
      pet.playResult(gameScore);  // actualiza el record y da felicidad
      sfxPlay(gameNewHi && gameScore > 0 ? SFX_MEDAL : SFX_LEVEL);
      gameOverUntil = millis() + 4000;
    } else {
      respawnBall();
      sfxPlay(SFX_BALL_MISS);
    }
  }
  // el bicho la sigue por abajo
  float chase = (ballX - gamePetX) * 0.12f;
  if (chase > 7) chase = 7;
  if (chase < -7) chase = -7;
  gamePetX += chase;
}

// ---------- saco de entrenamiento (entrena la fuerza) ----------

void startSack() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony) return;
  sackOpen = true;
  sackUntil = millis() + 10000;
  sackOverUntil = 0;
  sackHits = 0;
  sackShake = 0;
  sackNewHi = false;
  sfxPlay(SFX_GAME_START);
}

void sackTap() {
  if (millis() >= sackUntil) return;  // ya termino el tiempo
  sackHits++;
  sackShake = 16;  // sacude el saco
  if ((sackHits & 1) == 1) sfxPlay(SFX_PLAY);
}

void drawGameScene();  // prototipo (definida mas abajo)
const char *battleTypeName(uint8_t type);
uint16_t battleTypeColor(uint8_t type);

void renderSack() {
  uint32_t now = millis();
  drawGameScene();  // fondo del habitat
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  // pantalla de resultado
  if (sackOverUntil) {
    if (now > sackOverUntil) { sackOpen = false; return; }
    char b[20];
    snprintf(b, sizeof(b), T(S_HITS_FMT), sackHits);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(b) * 12, 150);
    gfx->print(b);
    char g[18];
    snprintf(g, sizeof(g), T(S_STR_GAIN_FMT), sackGain);
    gfx->setTextColor(UI_BAR_BAD);
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(g) * 9, 210);
    gfx->print(g);
    gfx->setTextSize(2);
    if (sackNewHi && sackHits > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char r[18];
      snprintf(r, sizeof(r), T(S_RECORD_FMT), pet.strHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(r) * 6, 256);
      gfx->print(r);
    }
    gfx->flush();
    return;
  }

  // se acabaron los 10 s: aplicar entrenamiento
  if (now >= sackUntil) {
    sackNewHi = (sackHits > pet.strHi);
    sackGain = pet.trainStrength(sackHits);
    sfxPlay(sackNewHi ? SFX_MEDAL : SFX_PLAY);
    sackOverUntil = now + 3500;
    gfx->flush();
    return;
  }

  // aporreo activo
  sackShake *= 0.84f;
  int off = (int)(sackShake * sinf(now * 0.05f));
  int sx = CX + off, top = 86, sy = 150;
  gfx->fillRect(CX - 3, 56, 6, top - 56, ink);          // gancho/cuerda
  gfx->fillRect(sx - 4, top - 30, 8, 34, ink);          // cadena
  gfx->fillRoundRect(sx - 42, top, 84, 150, 26, C565(0xb5, 0x3a, 0x3a));  // saco
  gfx->fillRoundRect(sx - 42, top, 84, 22, 18, C565(0x7e, 0x28, 0x28));   // tapa
  gfx->drawRoundRect(sx - 42, top, 84, 150, 26, ink);
  gfx->fillRect(sx - 42, top + 70, 84, 4, C565(0x7e, 0x28, 0x28));        // costura

  // contador de golpes
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", sackHits);
  gfx->setTextColor(ink);
  gfx->setTextSize(6);
  gfx->setCursor(CX - strlen(buf) * 18, 268);
  gfx->print(buf);

  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_HIT_FAST)) * 6, 322);
  gfx->print(T(S_HIT_FAST));

  // barra de tiempo
  uint32_t left = sackUntil - now;
  int bw = 280, fw = (int)((uint32_t)bw * left / 10000);
  gfx->fillRoundRect(CX - bw / 2, 350, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 350, fw, 16, 5, UI_BAR_OK);

  gfx->flush();
}

// fondo del minijuego: hatibat del bicho (cielo por hora + suelo del bioma)
void drawGameScene() {
  int hh = sceneHour();
  bool night = darkMode || hh < 6 || hh >= 20;
  uint16_t top, bot;
  if (night)       { top = C565(0x0c, 0x12, 0x24); bot = C565(0x1e, 0x26, 0x46); }
  else if (hh < 8) { top = C565(0xd1, 0x6a, 0x86); bot = C565(0xf3, 0xb8, 0x7c); }
  else if (hh < 18){ top = C565(0x8f, 0xc8, 0xea); bot = C565(0xdc, 0xee, 0xe6); }
  else             { top = C565(0xc7, 0x5a, 0x4a); bot = C565(0xf0, 0xae, 0x64); }
  int hor = 376;
  for (int y = 0; y < hor; y += 8)
    gfx->fillRect(0, y, 466, 8, lerp565(top, bot, y, hor));
  if (night)
    for (auto &st : STARS) gfx->fillRect(st[0], st[1], 4, 4, UI_WHITE);
  uint8_t bio = pet.isEgg() ? 0 : DEX_TBL[pet.speciesId].biome;
  uint16_t soil = BIOME_SOIL[bio < 6 ? bio : 0];
  if (night) soil = lerp565(soil, C565(0x16, 0x1c, 0x30), 9, 16);
  gfx->fillRect(0, hor, 466, 466 - hor, soil);
}

void drawGameResult(const char *recordFmt, uint16_t record, StrId gainFmt) {
  drawGameScene();
  if (millis() > gameOverUntil) {
    gameOpen = false;
    return;
  }
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;
  char buf[22];
  snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - strlen(buf) * 12, 148);
  gfx->print(buf);
  char gain[18];
  snprintf(gain, sizeof(gain), T(gainFmt), gameGain);
  gfx->setTextColor(gainFmt == S_DEF_GAIN_FMT ? 0x4C98 : (gainFmt == S_HYG_GAIN_FMT ? UI_BAR_OK : UI_BAR_WARN));
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(gain) * 9, 204);
  gfx->print(gain);
  gfx->setTextSize(2);
  if (gameNewHi && gameScore > 0) {
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 256);
    gfx->print(T(S_NEW_RECORD));
  } else {
    char rec[20];
    snprintf(rec, sizeof(rec), recordFmt, record);
    gfx->setTextColor(ink);
    gfx->setCursor(CX - strlen(rec) * 6, 256);
    gfx->print(rec);
  }
  gfx->flush();
}

void renderCatchGame() {
  uint32_t now = millis();
  if (gameOverUntil) {
    drawGameResult(T(S_RECORD_FMT), pet.catchHi, S_SPD_GAIN_FMT);
    return;
  }
  if (now >= catchUntil || gameMisses >= 3) {
    finishCatchGame();
    return;
  }
  if (now > catchTargetUntil) {
    if (++gameMisses >= 3) {
      finishCatchGame();
      return;
    }
    spawnCatchTarget();
  }
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;
  gfx->setTextColor(ink);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_CATCH_TITLE)) * 9, 32);
  gfx->print(T(S_CATCH_TITLE));
  char score[16], rec[16];
  snprintf(score, sizeof(score), T(S_SCORE_FMT), gameScore);
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.catchHi);
  gfx->setTextSize(2);
  gfx->setCursor(50, 78);
  gfx->print(score);
  gfx->setCursor(294, 78);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }
  const char *const *icon = catchIcon == 0 ? SPR_ICON_FOOD : (catchIcon == 1 ? SPR_ICON_BERRY_B : SPR_ICON_BERRY_G);
  gfx->fillCircle(catchX, catchY, 34, UI_WHITE);
  gfx->drawCircle(catchX, catchY, 36, UI_BAR_WARN);
  drawMap(icon, 16, catchX - 24, catchY - 24, 3, false);
  int bw = 280;
  int fw = (int)((uint32_t)bw * (catchUntil - now) / 20000);
  if (fw < 0) fw = 0;
  gfx->fillRoundRect(CX - bw / 2, 362, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 362, fw, 16, 5, UI_BAR_OK);
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 220) gfx->drawCircle((int)hitX, (int)hitY, 42 + ht / 8, UI_BAR_WARN);
  gfx->flush();
}

void stepMemoGame() {
  uint32_t now = millis();
  if (memoFailUntil) {
    if (now >= memoFailUntil) {
      memoFailUntil = 0;
      memoHintPad = -1;
      finishMemoGame();
    }
    return;
  }
  if (!memoShowing || now < memoNextAt) return;
  if (memoActivePad >= 0) {
    memoActivePad = -1;
    memoShow++;
    if (memoShow >= memoLen) {
      memoShowing = false;
      memoInput = 0;
      memoTurnUntil = now + 520;
    } else {
      memoNextAt = now + 150;
    }
    return;
  }
  memoActivePad = memoSeq[memoShow];
  memoPadSound((uint8_t)memoActivePad);
  memoNextAt = now + 480;
}

void renderMemoGame() {
  if (gameOverUntil) {
    drawGameResult(T(S_RECORD_FMT), pet.memoHi, S_DEF_GAIN_FMT);
    return;
  }
  stepMemoGame();
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;
  char roundBuf[18], rec[16];
  snprintf(roundBuf, sizeof(roundBuf), T(S_ROUND_FMT), memoRounds + 1);
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.memoHi);
  gfx->setTextColor(ink);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_GAME_MEMO)) * 9, 34);
  gfx->print(T(S_GAME_MEMO));
  gfx->setTextSize(2);
  gfx->setCursor(60, 82);
  gfx->print(roundBuf);
  gfx->setCursor(310, 82);
  gfx->print(rec);
  const int16_t px[4] = { 142, 324, 142, 324 };
  const int16_t py[4] = { 164, 164, 318, 318 };
  const uint16_t col[4] = { UI_BAR_BAD, UI_BAR_WARN, 0x4C98, UI_BAR_OK };
  int active = memoShowing ? memoActivePad : (memoFailUntil ? memoHintPad : -1);
  for (int i = 0; i < 4; i++) {
    uint16_t fill = i == active ? lerp565(col[i], UI_WHITE, 5, 8) : col[i];
    gfx->fillCircle(px[i], py[i], 48, fill);
    gfx->drawCircle(px[i], py[i], 52, ink);
    if (i == active) {
      int pulse = 56 + (int)((millis() / 70) % 5);
      gfx->drawCircle(px[i], py[i], pulse, col[i]);
    }
    if (i == memoFlashPad && millis() < memoFlashUntil) {
      gfx->drawCircle(px[i], py[i], 60, memoFlashGood ? UI_BAR_OK : UI_BAR_BAD);
      gfx->drawCircle(px[i], py[i], 64, memoFlashGood ? UI_BAR_OK : UI_BAR_BAD);
    }
  }
  char phase[28];
  if (memoFailUntil) snprintf(phase, sizeof(phase), "%s", T(S_MEMO_WRONG));
  else if (memoShowing) snprintf(phase, sizeof(phase), "%s", T(S_MEMO_WATCH));
  else snprintf(phase, sizeof(phase), T(S_MEMO_TURN_FMT), memoInput + 1, memoLen);
  gfx->setTextColor(memoFailUntil ? UI_BAR_BAD : (memoShowing ? UI_BAR_WARN : UI_BAR_OK));
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(phase) * 6, 112);
  gfx->print(phase);
  gfx->flush();
}

void renderCleanGame() {
  uint32_t now = millis();
  if (gameOverUntil) {
    drawGameResult(T(S_RECORD_FMT), pet.cleanHi, S_HYG_GAIN_FMT);
    return;
  }
  if (now >= cleanUntil || gameMisses >= 3) {
    finishCleanGame();
    return;
  }
  if (now >= cleanSpawnAt) {
    if (cleanActive < 4) spawnCleanSpot();
    cleanSpawnAt = now + 720 - (gameScore > 12 ? 260 : gameScore * 20);
  }
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;
  gfx->setTextColor(ink);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_CLEAN_TITLE)) * 9, 32);
  gfx->print(T(S_CLEAN_TITLE));
  char score[16], rec[16];
  snprintf(score, sizeof(score), T(S_SCORE_FMT), gameScore);
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.cleanHi);
  gfx->setTextSize(2);
  gfx->setCursor(50, 78);
  gfx->print(score);
  gfx->setCursor(294, 78);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (!cleanAlive[i]) continue;
    gfx->fillCircle(cleanX[i], cleanY[i], 26, C565(0x8a, 0x66, 0x45));
    gfx->drawCircle(cleanX[i], cleanY[i], 28, UI_INK);
    gfx->fillCircle(cleanX[i] - 8, cleanY[i] - 8, 5, C565(0x62, 0x45, 0x2e));
    gfx->fillCircle(cleanX[i] + 10, cleanY[i] + 4, 6, C565(0x62, 0x45, 0x2e));
  }
  int bw = 280;
  int fw = (int)((uint32_t)bw * (cleanUntil - now) / 18000);
  if (fw < 0) fw = 0;
  gfx->fillRoundRect(CX - bw / 2, 362, bw, 16, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 362, fw, 16, 5, UI_BAR_OK);
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 220) gfx->drawCircle((int)hitX, (int)hitY, 42 + ht / 8, UI_BAR_OK);
  gfx->flush();
}

void renderTypeGame() {
  uint32_t now = millis();
  if (gameOverUntil) {
    drawGameResult(T(S_RECORD_FMT), pet.typeHi, S_ATK_GAIN_FMT);
    return;
  }
  if (now >= typeUntil) {
    if (++gameMisses >= 3) {
      finishTypeGame();
      return;
    }
    sfxPlay(SFX_MINIGAME_BAD);
    nextTypeQuestion();
  }
  drawGameScene();
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;
  gfx->setTextColor(ink);
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_TYPE_TITLE)) * 9, 32);
  gfx->print(T(S_TYPE_TITLE));
  char score[16], rec[16];
  snprintf(score, sizeof(score), T(S_SCORE_FMT), gameScore);
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.typeHi);
  gfx->setTextSize(2);
  gfx->setCursor(50, 78);
  gfx->print(score);
  gfx->setCursor(294, 78);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }

  const char *enemy = battleTypeName(typeEnemy);
  gfx->fillRoundRect(118, 126, 230, 54, 14, lerp565(battleTypeColor(typeEnemy), uiPanel(), 4, 8));
  gfx->drawRoundRect(118, 126, 230, 54, 14, ink);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(enemy) * 9, 143);
  gfx->print(enemy);

  for (int i = 0; i < 3; i++) {
    int bx = 88;
    int by = 210 + i * 60;
    const char *label = battleTypeName(typeChoice[i]);
    gfx->fillRoundRect(bx, by, 290, 48, 12, lerp565(battleTypeColor(typeChoice[i]), uiPanel(), 5, 8));
    gfx->drawRoundRect(bx, by, 290, 48, 12, ink);
    gfx->setTextColor(uiInk());
    gfx->setTextSize(2);
    gfx->setCursor(bx + (290 - (int)strlen(label) * 12) / 2, by + 17);
    gfx->print(label);
  }
  int bw = 280;
  int fw = (int)((uint32_t)bw * (typeUntil - now) / 4200);
  if (fw < 0) fw = 0;
  gfx->fillRoundRect(CX - bw / 2, 392, bw, 14, 5, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(CX - bw / 2, 392, fw, 14, 5, UI_BAR_OK);
  gfx->flush();
}

void renderGame() {
  // sin fillScreen(NEGRO): drawGameScene cubre los 466x466 completos. Si el
  // DMA del flush anterior aun lee el buffer, vera contenido valido (no negro
  // a medio pintar), que era el parpadeo a 25 fps.
  bool night = sceneHour() < 6 || sceneHour() >= 20;
  uint16_t ink = night ? UI_INK_NIGHT : UI_INK;

  if (gameMode == 1) {
    renderCatchGame();
    return;
  }
  if (gameMode == 2) {
    renderMemoGame();
    return;
  }
  if (gameMode == 3) {
    renderCleanGame();
    return;
  }
  if (gameMode == 4) {
    renderTypeGame();
    return;
  }

  if (gameOverUntil) {
    drawGameScene();
    if (millis() > gameOverUntil) {
      gameOpen = false;
      return;
    }
    char buf[22];
    snprintf(buf, sizeof(buf), T(S_SCORE_FMT), gameScore);
    gfx->setTextColor(ink);
    gfx->setTextSize(4);
    gfx->setCursor(CX - strlen(buf) * 12, 160);
    gfx->print(buf);
    gfx->setTextSize(2);
    if (gameNewHi && gameScore > 0) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setCursor(CX - strlen(T(S_NEW_RECORD)) * 6, 214);
      gfx->print(T(S_NEW_RECORD));
    } else {
      char rec[20];
      snprintf(rec, sizeof(rec), T(S_RECORD_FMT), pet.gameHi);
      gfx->setTextColor(ink);
      gfx->setCursor(CX - strlen(rec) * 6, 214);
      gfx->print(rec);
    }
    const char *msg = gameScore >= 10 ? T(S_GREAT_JOY) : T(S_PLUS_JOY);
    gfx->setTextColor(ink);
    gfx->setCursor(CX - strlen(msg) * 6, 250);
    gfx->print(msg);
    gfx->flush();
    return;
  }

  drawGameScene();
  stepGame();

  // marcador, record y vidas
  char buf[8];
  snprintf(buf, sizeof(buf), "%u", gameScore);
  gfx->setTextColor(ink);
  gfx->setTextSize(4);
  gfx->setCursor(CX - strlen(buf) * 12, 30);
  gfx->print(buf);
  char rec[12];
  snprintf(rec, sizeof(rec), T(S_REC_FMT), pet.gameHi);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(rec) * 6, 76);
  gfx->print(rec);
  for (int i = 0; i < 3; i++) {
    if (i < 3 - gameMisses) gfx->fillCircle(180 + i * 28, 104, 6, UI_BAR_BAD);
    else gfx->drawCircle(180 + i * 28, 104, 6, UI_TRACK);
  }

  if (pmd.loaded) {
    uint8_t act = (ballX > gamePetX + 4) ? PMD_WALKR : (ballX < gamePetX - 4) ? PMD_WALKL : PMD_IDLE;
    if (!pmd.has(act)) act = PMD_IDLE;
    drawPmdAct(act, (int)gamePetX, 394, millis(), true, false, 3);
  } else if (mon.loaded) {
    int s = (mon.h * 2 > 130) ? 1 : 2;
    int w = mon.w * s, h = mon.h * s;
    uint16_t fm = mon.frameMs ? mon.frameMs : 100;
    uint16_t fi = (millis() / fm) % mon.frames;
    const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
    int px = (int)gamePetX - w / 2, py = 394 - h;
    for (int r = 0; r < mon.h; r++)
      for (int c = 0; c < mon.w; c++) {
        uint8_t idx = fr[r * mon.w + c];
        if (idx == 0xFF) continue;
        gfx->fillRect(px + c * s, py + r * s, s, s, mon.pal[idx]);
      }
  }

  // anillo de impacto que se expande y desvanece (feedback suave del golpe)
  uint32_t ht = millis() - hitTime;
  if (hitTime && ht < 260) {
    int rad = 22 + (int)(ht / 6);
    gfx->drawCircle((int)hitX, (int)hitY, rad, C565(0xff, 0xe7, 0x9f));
    gfx->drawCircle((int)hitX, (int)hitY, rad - 2, C565(0xff, 0xd9, 0x8a));
  }

  // la pokeball
  drawMap(SPR_ICON_PLAY, 16, (int)ballX - 24, (int)ballY - 24, 3, false);

  gfx->flush();
}

// ---------- combate salvaje manual ----------

uint16_t battleHpFor(const BattleStats &stats) {
  return stats.hp ? stats.hp : (uint16_t)(30 + stats.level * 5 + stats.def);
}

BattleStats petBattleStats() {
  BattleStats stats = {};
  stats.level = pet.level();
  stats.atk = pet.atkStat();
  stats.def = pet.defStat();
  stats.spe = pet.speStat();
  stats.hp = 0;
  if (!pet.isEgg() && pet.speciesId >= 1 && pet.speciesId <= DEX_COUNT) {
    stats.type1 = DEX_TBL[pet.speciesId].type1;
    stats.type2 = DEX_TBL[pet.speciesId].type2;
  }
  return stats;
}

void scheduleNextWild(uint32_t now) {
  nextWildEligible = now + WILD_COOLDOWN_MS;
}

bool mainScreenReadyForWild() {
  if (screenOff || pet.awaitingStarter() || pet.isEgg() || pet.sleeping || pet.ceremony) return false;
  if (battleOpen || gameOpen || gameMenuOpen || sackOpen || cardOpen || galleryOpen || kbOpen || clockOpen || helpOpen) return false;
  if (feedMenuUntil || confirmUntil || choiceKind || bathUntil || petEventUntil) return false;
  if (pet.evolving() || pet.wantEvolveButton()) return false;
  return true;
}

void maybeOfferWildEncounter(uint32_t now) {
  if (nextWildEligible == 0) {
    scheduleNextWild(now);
    return;
  }
  if (wildPromptUntil) return;
  if (now - lastWildCheck < WILD_CHECK_MS) return;
  lastWildCheck = now;
  if (now < nextWildEligible) return;
  if (!mainScreenReadyForWild()) return;
  int16_t cand = pickWildSpecies((uint32_t)random(0x7FFFFFFF));
  uint8_t phase = currentDayPhase();
  uint8_t chance = (phase == 3) ? 4 : (phase == 0 ? 7 : 8);
  if (phase == 3 && cand >= 1 && cand <= DEX_COUNT) {
    const DexEntry &d = DEX_TBL[cand];
    if (d.type1 == TYPE_GHOST || d.type2 == TYPE_GHOST ||
        d.type1 == TYPE_POISON || d.type2 == TYPE_POISON) chance = 8;
  }
  if ((uint8_t)random(100) >= chance) return;

  wildPromptDex = cand;
  wildPromptLevel = wildLevelFor(pet.level(), (uint8_t)random(100));
  wildPromptUntil = now + WILD_PROMPT_MS;
  scheduleNextWild(now);
  sfxPlay(SFX_MENU);
}

void scheduleNextPetEvent(uint32_t now) {
  nextPetEventEligible = now + PET_EVENT_COOLDOWN_MS;
}

bool mainScreenReadyForPetEvent() {
  if (screenOff || pet.awaitingStarter() || pet.isEgg() || pet.sleeping || pet.ceremony) return false;
  if (battleOpen || gameOpen || gameMenuOpen || sackOpen || cardOpen || galleryOpen || kbOpen || clockOpen || helpOpen) return false;
  if (feedMenuUntil || confirmUntil || choiceKind || bathUntil || wildPromptUntil) return false;
  if (pet.evolving() || pet.wantEvolveButton()) return false;
  return true;
}

void maybeOfferPetEvent(uint32_t now) {
  if (nextPetEventEligible == 0) {
    scheduleNextPetEvent(now);
    return;
  }
  if (petEventUntil) {
    if (now > petEventUntil) petEventUntil = 0;
    return;
  }
  if (now - lastPetEventCheck < PET_EVENT_CHECK_MS) return;
  lastPetEventCheck = now;
  if (now < nextPetEventEligible) return;
  if (!mainScreenReadyForPetEvent()) return;
  uint8_t phase = currentDayPhase();
  uint8_t chance = (phase == 0) ? 14 : (phase == 3 ? 8 : 10);
  if ((uint8_t)random(100) >= chance) return;

  petEventType = (uint8_t)random(3);
  petEventUntil = now + PET_EVENT_PROMPT_MS;
  scheduleNextPetEvent(now);
  sfxPlay(SFX_EVENT_SPARKLE);
}

bool inPetEventHit(int16_t x, int16_t y) {
  int16_t ex = 366, ey = 286;
  int dx = x - ex, dy = y - ey;
  return dx * dx + dy * dy <= 44 * 44;
}

void acceptPetEvent() {
  uint8_t type = petEventType;
  petEventUntil = 0;
  scheduleNextPetEvent(millis());
  if (!pet.applyPetEvent(type)) return;
  StrId msg = S_EVENT_FOUND;
  if (type == PET_EVENT_HEART) msg = S_EVENT_PET;
  else if (type == PET_EVENT_SPARKLE) msg = S_EVENT_LUCKY;
  snprintf(petEventMsg, sizeof(petEventMsg), "%s", T(msg));
  petEventFeedbackUntil = millis() + 1800;
  sfxPlay(type == PET_EVENT_BERRY ? SFX_EAT : (type == PET_EVENT_SPARKLE ? SFX_EVENT_SPARKLE : SFX_HEART));
}

void closeBattle() {
  battleOpen = false;
  battleResolved = false;
  battleAttackMenuUntil = 0;
  battleCatchOffered = false;
  battleCatchTried = false;
  battleCatchDone = false;
  battleCatchSuccess = false;
  battleRespectCatch = false;
  battleCatchChance = 0;
  wildPmd.unload();
  markUiDirty();
  lockTouchBrief();
}

void startBattleWith(int16_t forcedDex, uint8_t forcedLevel) {
  if (!canStartWildBattle(pet.isEgg(), pet.sleeping, pet.ceremony)) return;
  wildPromptUntil = 0;
  scheduleNextWild(millis());
  if (forcedDex >= 1 && forcedDex <= DEX_COUNT) {
    battleDex = forcedDex;
    battleLevel = forcedLevel ? forcedLevel : wildLevelFor(pet.level(), (uint8_t)random(100));
  } else {
    uint32_t speciesRoll = (uint32_t)random(0x7FFFFFFF);
    uint8_t levelRoll = random(100);
    battleDex = pickWildSpecies(speciesRoll);
    battleLevel = wildLevelFor(pet.level(), levelRoll);
  }
  battlePlayer = petBattleStats();
  battleEnemy = wildBattleStats(battleDex, battleLevel);
  battlePlayerSex=(uint8_t)((pet.geneAtk+pet.geneDef+pet.geneSpe)&1);
  battleEnemySex=(uint8_t)random(2);
  battleEnemyShiny=(random(128)==0); // 1/128, independant de l'espece et de la generation
  battleShinyFxUntil=battleEnemyShiny ? millis()+1800 : 0;
  battleEnemy.hp = 0;
  battleResult = {};
  battleRun = beginBattleRuntime(battlePlayer, battleEnemy);
  battleTurn = {};
  battleReward = {};
  battleMsg[0] = 0;
  battleAttackMenuUntil = 0;
  battleLowHpWarned = false;
  battleCatchOffered = false;
  battleCatchTried = false;
  battleCatchDone = false;
  battleCatchSuccess = false;
  battleRespectCatch = false;
  battleCatchChance = 0;
  battleResolved = false;
  battleOpen = true;
  battleDirty = true;
  wildPmd.unload();
  wildPmd.load(battleDex, battleEnemyShiny);
  sfxPlay(battleEnemyShiny ? SFX_EVENT_SPARKLE : SFX_TAP);
  speciesChirpPlay(battleDex);
}

void startBattle() {
  startBattleWith(0, 0);
}

void finishBattle() {
  if (battleResolved) return;
  battleResolved = true;
  battleDirty = true;
  if (battleTurn.playerWon) {
    bool closeWin = battleRun.playerHp <= battleRun.playerMaxHp / 3;
    battleReward = pet.applyBattleWin(battleDex, closeWin);
    battleCatchOffered = true;
    battleRespectCatch = false;
    battleCatchChance = pet.catchChanceForWild(battleDex, battleLevel, battlePlayer.level, closeWin);
    sfxPlay(SFX_BATTLE_WIN);
  } else {
    battleReward = {};
    pet.applyBattleLoss();
    bool closeLoss = battleRun.enemyHp > 0 && battleRun.enemyHp * 100UL <= battleRun.enemyMaxHp * 30UL;
    battleCatchChance = closeLoss ? pet.respectCatchChanceForWild(battleDex, battleLevel, battlePlayer.level) : 0;
    battleCatchOffered = battleCatchChance > 0;
    battleRespectCatch = battleCatchOffered;
    sfxPlay(SFX_BATTLE_LOSS);
  }
}

void performBattleAction(BattleAction action) {
  if (battleResolved) return;
  battleAttackMenuUntil = 0;
  battleLastAction = action;
  battleTurn = stepBattle(battleRun, action, (uint8_t)random(100));
  if (battleTurn.restFailed) {
    snprintf(battleMsg, sizeof(battleMsg), T(S_NO_REST));
  } else if (battleTurn.counterReady) {
    snprintf(battleMsg, sizeof(battleMsg), T(S_COUNTER_READY));
  } else if (battleTurn.playerRested) {
    char healMsg[18];
    snprintf(healMsg, sizeof(healMsg), T(S_RESTED_FMT), battleTurn.playerHeal);
    snprintf(battleMsg, sizeof(battleMsg), "%s %s", healMsg, T(S_GUARD));
  } else if (battleTurn.playerDamage > 0) {
    if (battleTurn.playerTypePct > 100) snprintf(battleMsg, sizeof(battleMsg), "%s %u", T(S_EFFECTIVE), battleTurn.playerDamage);
    else if (battleTurn.playerTypePct < 100) snprintf(battleMsg, sizeof(battleMsg), "%s %u", T(S_NOT_EFFECTIVE), battleTurn.playerDamage);
    else snprintf(battleMsg, sizeof(battleMsg), T(S_HIT_FMT), battleTurn.playerDamage);
  } else if (battleTurn.enemyDodged) {
    snprintf(battleMsg, sizeof(battleMsg), T(S_ENEMY_DODGED));
  } else if (battleTurn.playerDodged) {
    snprintf(battleMsg, sizeof(battleMsg), T(S_DODGED));
  } else {
    snprintf(battleMsg, sizeof(battleMsg), T(S_MISSED));
  }
  if (battleTurn.battleEnded) {
    finishBattle();
    return;
  }
  if (!battleLowHpWarned && battleRun.playerHp > 0 && battleRun.playerHp <= battleRun.playerMaxHp * 3 / 10) {
    battleLowHpWarned = true;
    sfxPlay(SFX_LOW_HP);
  }
  if (battleTurn.restFailed) sfxPlay(SFX_DENY);
  else if (battleTurn.counterReady) sfxPlay(SFX_COUNTER);
  else if (battleTurn.playerRested) sfxPlay(SFX_REST);
  else if (action == BATTLE_ATTACK_QUICK) sfxPlay(SFX_ATTACK_QUICK);
  else if (action == BATTLE_ATTACK_HEAVY) sfxPlay(SFX_ATTACK_HEAVY);
  else if (battleTurn.playerDamage > 0 && battleTurn.playerTypePct > 100) sfxPlay(SFX_EFFECTIVE);
  else if (battleTurn.playerDamage > 0 && battleTurn.playerTypePct < 100) sfxPlay(SFX_WEAK_HIT);
  else if (battleTurn.enemyDamage > 0) sfxPlay(SFX_ENEMY_HIT);
  else sfxPlay(battleTurn.playerDamage > 0 ? SFX_PLAY : SFX_TAP);
}

void battleTap(int16_t x, int16_t y) {
  if (battleResolved) {
    if (battleCatchOffered && !battleCatchDone) {
      if (x >= 76 && x <= 224 && y >= 392 && y <= 448) {
        bool closeWin = battleRun.playerHp <= battleRun.playerMaxHp / 3;
        battleCatchTried = true;
        battleCatchDone = true;
        if (battleRespectCatch) {
          battleCatchSuccess = pet.tryRespectCatchWild(battleDex, battleLevel, battlePlayer.level, (uint8_t)random(100), battleEnemyShiny);
          battleCatchChance = pet.respectCatchChanceForWild(battleDex, battleLevel, battlePlayer.level);
        } else {
          battleCatchSuccess = pet.tryCatchWild(battleDex, battleLevel, battlePlayer.level, closeWin, (uint8_t)random(100), battleEnemyShiny);
          battleCatchChance = pet.catchChanceForWild(battleDex, battleLevel, battlePlayer.level, closeWin);
        }
        sfxPlay(battleCatchSuccess ? SFX_CATCH_OK : SFX_CATCH_FAIL);
        galleryDirty = true;
        battleDirty = true;
        return;
      }
      if (x >= 242 && x <= 390 && y >= 392 && y <= 448) {
        battleCatchDone = true;
        battleCatchTried = false;
        battleDirty = true;
        sfxPlay(SFX_TAP);
        return;
      }
      return;
    }
    if (x >= 118 && x <= 348 && y >= 392 && y <= 454) closeBattle();
    return;
  }
  if (battleAttackMenuUntil) {
    if (y >= 344 && y <= 410) {
      if (x >= 82 && x <= 178) { performBattleAction(BATTLE_ATTACK_QUICK); return; }
      if (x >= 190 && x <= 286) { performBattleAction(BATTLE_ATTACK); return; }
      if (x >= 298 && x <= 394) { performBattleAction(BATTLE_ATTACK_HEAVY); return; }
    }
    battleAttackMenuUntil=0;
    battleDirty=true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (x >= 72 && x <= 226 && y >= 334 && y <= 377) {
    battleAttackMenuUntil = 1;
    battleDirty=true;
    sfxPlay(SFX_TAP);
  } else if (x >= 240 && x <= 394 && y >= 334 && y <= 377) {
    performBattleAction(BATTLE_DODGE);
  } else if (x >= 72 && x <= 226 && y >= 383 && y <= 426) {
    performBattleAction(BATTLE_REST);
  } else if (x >= 240 && x <= 394 && y >= 383 && y <= 426) {
    // Sortie neutre : finishBattle() n'est pas appele, donc aucun gain ni malus.
    sfxPlay(SFX_TAP);
    closeBattle();
  }
}

void drawBattleHpBar(int x, int y, uint16_t cur, uint16_t maxHp, uint16_t color) {
  if (maxHp == 0) maxHp = 1;
  int w = 150;
  int fw = (int)((uint32_t)cur * w / maxHp);
  if (fw > w) fw = w;
  gfx->fillRoundRect(x, y, w, 14, 4, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(x, y, fw, 14, 4, color);
}

void battleRewardText(char *buf, size_t len) {
  if (battleReward.amount == 0) { buf[0] = 0; return; }
  StrId fmt = S_SPD_GAIN_FMT;
  if (battleReward.stat == BATTLE_REWARD_ATK) fmt = S_ATK_GAIN_FMT;
  else if (battleReward.stat == BATTLE_REWARD_DEF) fmt = S_DEF_GAIN_FMT;
  snprintf(buf, len, T(fmt), battleReward.amount);
}

void drawBattleButtonLabel(int x, int y, int w, const char *label) {
  int px = (int)strlen(label) * 12;
  if (px <= w - 8) {
    gfx->setTextSize(2);
    gfx->setCursor(x + (w - px) / 2, y);
    gfx->print(label);
  } else {
    gfx->setTextSize(1);
    gfx->setCursor(x + (w - (int)strlen(label) * 6) / 2, y + 3);
    gfx->print(label);
    gfx->setTextSize(2);
  }
}

const char *battleTypeName(uint8_t type) {
  static const char *const TYPE_NAMES[LANG_COUNT][19] = {
    { "", "NORMAL", "FUEGO", "AGUA", "ELEC", "PLANTA", "HIELO", "LUCHA", "VENENO", "TIERRA", "VUELO", "PSI", "BICHO", "ROCA", "FANT", "DRAGON", "SINIE", "ACERO", "HADA" },
    { "", "NORMAL", "FIRE", "WATER", "ELEC", "GRASS", "ICE", "FIGHT", "POISON", "GROUND", "FLY", "PSY", "BUG", "ROCK", "GHOST", "DRAGON", "DARK", "STEEL", "FAIRY" },
    { "", "NORMAL", "FEU", "EAU", "ELEC", "PLANTE", "GLACE", "COMBAT", "POISON", "SOL", "VOL", "PSY", "INSECT", "ROCHE", "SPECTRE", "DRAGON", "TENEBR", "ACIER", "FEE" },
    { "", "NORMAL", "FEUER", "WASSER", "ELEKTRO", "PFLANZE", "EIS", "KAMPF", "GIFT", "BODEN", "FLUG", "PSYCHO", "KAEFER", "GESTEIN", "GEIST", "DRACHE", "UNLICHT", "STAHL", "FEE" },
    { "", "NORMALE", "FUOCO", "ACQUA", "ELETTRO", "ERBA", "GHIACCIO", "LOTTA", "VELENO", "TERRA", "VOLANTE", "PSICO", "COLEOT", "ROCCIA", "SPETTRO", "DRAGO", "BUIO", "ACCIAIO", "FOLLETTO" },
    { "", "NORMAL", "FOGO", "AGUA", "ELETR", "PLANTA", "GELO", "LUTA", "VENENO", "TERRA", "VOO", "PSI", "INSETO", "PEDRA", "FANT", "DRAGAO", "SOMBRIO", "ACO", "FADA" },
  };
  if (type < 19) return TYPE_NAMES[gLang][type];
  switch (type) {
    case TYPE_NORMAL: return "NORMAL";
    case TYPE_FIRE: return "FIRE";
    case TYPE_WATER: return "WATER";
    case TYPE_ELECTRIC: return "ELEC";
    case TYPE_GRASS: return "GRASS";
    case TYPE_ICE: return "ICE";
    case TYPE_FIGHTING: return "FIGHT";
    case TYPE_POISON: return "POISON";
    case TYPE_GROUND: return "GROUND";
    case TYPE_FLYING: return "FLY";
    case TYPE_PSYCHIC: return "PSY";
    case TYPE_BUG: return "BUG";
    case TYPE_ROCK: return "ROCK";
    case TYPE_GHOST: return "GHOST";
    case TYPE_DRAGON: return "DRAGON";
    case TYPE_DARK: return "DARK";
    case TYPE_STEEL: return "STEEL";
    case TYPE_FAIRY: return "FAIRY";
  }
  return "";
}

uint16_t battleTypeColor(uint8_t type) {
  switch (type) {
    case TYPE_FIRE: return 0xEA87;
    case TYPE_WATER: return 0x4C98;
    case TYPE_ELECTRIC: return 0xBCA1;
    case TYPE_GRASS: return 0x3C49;
    case TYPE_ICE: return 0x5D99;
    case TYPE_FIGHTING: return 0xA2A5;
    case TYPE_POISON: return 0x8A73;
    case TYPE_GROUND: return 0xB447;
    case TYPE_FLYING: return 0x8D7F;
    case TYPE_PSYCHIC: return 0xD28F;
    case TYPE_BUG: return 0x7CC4;
    case TYPE_ROCK: return 0x9407;
    case TYPE_GHOST: return 0x6B33;
    case TYPE_DRAGON: return 0x5A5F;
    case TYPE_DARK: return 0x5ACB;
    case TYPE_STEEL: return 0xA534;
    case TYPE_FAIRY: return 0xF3B7;
    default: return 0x8C4D;
  }
}

void typeText(char *buf, size_t len, const DexEntry &d) {
  if (d.type2 == TYPE_NONE) snprintf(buf, len, "%s", battleTypeName(d.type1));
  else snprintf(buf, len, "%s %s", battleTypeName(d.type1), battleTypeName(d.type2));
}

int typeChipWidth(uint8_t type) {
  return (int)strlen(battleTypeName(type)) * 6 + 14;
}

void drawTypeChip(int x, int y, uint8_t type) {
  if (type == TYPE_NONE) return;
  const char *label = battleTypeName(type);
  int w = typeChipWidth(type);
  gfx->fillRoundRect(x, y, w, 16, 5, lerp565(battleTypeColor(type), uiPanel(), 5, 8));
  gfx->drawRoundRect(x, y, w, 16, 5, uiInk());
  gfx->setTextSize(1);
  gfx->setTextColor(uiInk());
  gfx->setCursor(x + 7, y + 5);
  gfx->print(label);
}

void drawTypeChips(int x, int y, const DexEntry &d, bool alignRight) {
  int w1 = typeChipWidth(d.type1);
  int w2 = d.type2 == TYPE_NONE ? 0 : typeChipWidth(d.type2);
  int total = w1 + (w2 ? 4 + w2 : 0);
  int sx = alignRight ? x - total : x;
  drawTypeChip(sx, y, d.type1);
  if (d.type2 != TYPE_NONE) drawTypeChip(sx + w1 + 4, y, d.type2);
  gfx->setTextSize(2);
}

void drawWildPrompt() {
  gfx->fillRoundRect(82, 156, 302, 178, 18, uiPanel());
  gfx->drawRoundRect(82, 156, 302, 178, 18, uiInk());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_WILD_Q)) * 9, 176);
  gfx->print(T(S_WILD_Q));
  char name[28];
  snprintf(name, sizeof(name), "%s Lv.%u", dexName(wildPromptDex), wildPromptLevel);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(name) * 6, 206);
  gfx->print(name);

  gfx->fillRoundRect(93, 226, 280, 44, 12, UI_BAR_BAD);
  gfx->fillRoundRect(93, 278, 280, 44, 12, UI_TRACK);
  gfx->setTextColor(UI_WHITE);
  gfx->setCursor(CX - strlen(T(S_FIGHT)) * 6, 240);
  gfx->print(T(S_FIGHT));
  gfx->setTextColor(uiContrastText(UI_TRACK));
  gfx->setCursor(CX - strlen(T(S_LATER)) * 6, 292);
  gfx->print(T(S_LATER));
}

void drawPetEvent() {
  uint32_t now = millis();
  if (petEventUntil && now > petEventUntil) petEventUntil = 0;
  if (!petEventUntil && (!petEventFeedbackUntil || now > petEventFeedbackUntil)) return;

  int16_t ex = 366, ey = 286;
  if (petEventUntil) {
    gfx->fillCircle(ex, ey, 32, UI_WHITE);
    gfx->drawCircle(ex, ey, 34, UI_BAR_WARN);
    if (petEventType == PET_EVENT_BERRY) {
      drawMap(SPR_ICON_BERRY_G, 16, ex - 24, ey - 24, 3, false);
    } else if (petEventType == PET_EVENT_HEART) {
      drawMap(SPR_HEART, 32, ex - 32, ey - 32, 2, false);
    } else {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(4);
      gfx->setCursor(ex - 12, ey - 18);
      gfx->print("*");
      gfx->fillCircle(ex - 14, ey + 12, 4, UI_WHITE);
      gfx->fillCircle(ex + 16, ey + 10, 4, UI_WHITE);
    }
  }
  if (petEventFeedbackUntil && now <= petEventFeedbackUntil) {
    gfx->setTextColor(UI_BAR_WARN);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(petEventMsg) * 6, 292);
    gfx->print(petEventMsg);
  }
}


void drawBattlePmd(PmdMon &m, int16_t dex, int cx, int groundY, int target, bool sil=false) {
  const PmdAct &a=m.acts[PMD_IDLE];
  if (!a.frames) return;
  // Bounding box visuel cible ~128px sur l'écran 466px.
  // En combat, une pose fixe garantit une silhouette et une taille strictement
  // stables. Les animations PMD continuent normalement sur l'accueil.
  uint8_t fi=0;
  const uint8_t *fr=a.data+(uint32_t)fi*a.w*a.h;
  int minC=a.w, maxC=-1, minR=a.h, maxR=-1;
  for (int r=0;r<a.h;r++) {
    for (int c=0;c<a.w;c++) {
      if (fr[r*a.w+c]==0xFF) continue;
      if (c<minC) minC=c;
      if (c>maxC) maxC=c;
      if (r<minR) minR=r;
      if (r>maxR) maxR=r;
    }
  }
  if (maxC<minC || maxR<minR) return;
  int visibleW=maxC-minC+1, visibleH=maxR-minR+1;
  target=target*spriteSizePercent(dex)/100;
  int maxDim=max(visibleW,visibleH);
  int drawW=max(1,visibleW*target/maxDim);
  int drawH=max(1,visibleH*target/maxDim);
  int x0=cx-drawW/2;
  int y0=groundY-drawH;
  // Rééchantillonnage nearest-neighbour : dimensions continues, pixels nets.
  // Cela évite que 43 px passent brutalement de x2 à x1 (cas Lokhlass).
  for (int dy=0;dy<drawH;dy++) {
    int r=minR+(int)((uint32_t)dy*visibleH/drawH);
    const uint8_t *row=fr+r*a.w;
    for (int dx=0;dx<drawW;dx++) {
      int c=minC+(int)((uint32_t)dx*visibleW/drawW);
      uint8_t pi=row[c];
      if (pi==0xFF) continue;
      gfx->drawPixel(x0+dx,y0+dy,sil?INK_K:m.pal[pi]);
    }
  }
}

void drawBattleThumb(const uint8_t *b,int16_t dex,int cx,int groundY,int target,bool sil=false) {
  uint8_t w=b[0],h=b[1],n=b[2];
  const uint8_t *pal=b+3;
  const uint8_t *data=pal+n*2;
  int minC=w,maxC=-1,minR=h,maxR=-1;
  for(int r=0;r<h;r++) for(int c=0;c<w;c++) if(data[r*w+c]!=0xFF) {
    minC=min(minC,c); maxC=max(maxC,c); minR=min(minR,r); maxR=max(maxR,r);
  }
  if(maxC<minC||maxR<minR) return;
  int visibleW=maxC-minC+1,visibleH=maxR-minR+1;
  target=target*spriteSizePercent(dex)/100;
  int maxDim=max(visibleW,visibleH);
  int drawW=max(1,visibleW*target/maxDim),drawH=max(1,visibleH*target/maxDim);
  int x0=cx-drawW/2,y0=groundY-drawH;
  for(int dy=0;dy<drawH;dy++) {
    int r=minR+(int)((uint32_t)dy*visibleH/drawH);
    for(int dx=0;dx<drawW;dx++) {
      int c=minC+(int)((uint32_t)dx*visibleW/drawW);
      uint8_t pi=data[r*w+c];
      if(pi==0xFF) continue;
      uint16_t color=sil?INK_K:(uint16_t)(pal[pi*2]|(pal[pi*2+1]<<8));
      gfx->drawPixel(x0+dx,y0+dy,color);
    }
  }
}

void drawBattleName(const char *name, uint8_t level, int x, int y, int maxW) {
  char line[28];
  snprintf(line,sizeof(line),"%s Lv.%u",name,(unsigned)level);
  int len=(int)strlen(line);
  uint8_t ts=(len*12<=maxW)?2:1;
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(ts);
  gfx->setCursor(x,y+(ts==1?3:0));
  gfx->print(line);
}

void drawBattleCaughtBall(int cx, int cy) {
  // Indicateur compact 27x29 : le PNG original reste intact pour les autres usages.
  constexpr int outW=27, outH=29;
  int left=cx-outW/2, top=cy-outH/2;
  for(int y=0;y<outH;y++) {
    int sy=(int)((uint32_t)y*BATTLE_BALL_H/outH);
    for(int x=0;x<outW;x++) {
      int sx=(int)((uint32_t)x*BATTLE_BALL_W/outW);
      uint16_t pos=(uint16_t)sy*BATTLE_BALL_W+sx;
      if(!(pgm_read_byte(&BATTLE_BALL_MASK[pos>>3])&(1<<(pos&7)))) continue;
      gfx->drawPixel(left+x,top+y,pgm_read_word(&BATTLE_BALL_PIXELS[pos]));
    }
  }
}

void drawBattleHpInfo(int x,int y,uint16_t cur,uint16_t maxHp,uint16_t color) {
  if (maxHp==0) maxHp=1;
  if (cur>maxHp) cur=maxHp;
  gfx->setTextColor(UI_INK);
  gfx->setTextSize(1);
  gfx->setCursor(x,y);
  gfx->print(T(S_HP));
  drawBattleHpBar(x+22,y-1,cur,maxHp,color);
  char hp[18];
  snprintf(hp,sizeof(hp),"%u/%u",(unsigned)cur,(unsigned)maxHp);
  gfx->setCursor(x+176,y);
  gfx->print(hp);
}

bool battleSpeciesGenderless(int16_t dex) {
  return dex==81||dex==82||dex==100||dex==101||dex==120||dex==121||dex==132||dex==137||
         dex==201||dex==233||dex==292||dex==337||dex==338||dex==343||dex==344||
         (dex>=374&&dex<=386)||(dex>=144&&dex<=151)||(dex>=243&&dex<=251);
}

void drawBattleSex(uint8_t sex,int x,int y) {
  if(sex>1) return;
  uint16_t c=sex?C565(0xf0,0x48,0xa0):C565(0x28,0xa8,0xf0);
  gfx->drawCircle(x,y,4,c);
  if(sex==0) {
    gfx->drawLine(x+3,y-3,x+8,y-8,c);
    gfx->drawFastHLine(x+5,y-8,4,c);
    gfx->drawFastVLine(x+8,y-8,4,c);
  } else {
    gfx->drawFastVLine(x,y+4,7,c);
    gfx->drawFastHLine(x-3,y+8,7,c);
  }
}

void drawBattleStatusBar(const char *name,uint8_t level,uint8_t sex,int x,int y,int w,
                         uint16_t cur,uint16_t maxHp,uint16_t nameColor) {
  if(maxHp==0) maxHp=1;
  if(cur>maxHp) cur=maxHp;
  uint16_t ink=C565(0x18,0x20,0x28), edge=C565(0x50,0x58,0x58), empty=C565(0xb8,0xc0,0xb0);
  uint8_t nameSize=strlen(name)<=9?2:1;
  gfx->setTextColor(nameColor); gfx->setTextSize(nameSize);
  gfx->setCursor(x+10,y+(nameSize==2?1:6)); gfx->print(name);
  char lv[12]; snprintf(lv,sizeof(lv),"Nv.%u",(unsigned)level);
  gfx->setTextColor(nameColor); gfx->setTextSize(1); gfx->setCursor(x+w-45,y+5); gfx->print(lv);
  drawBattleSex(sex,x+w-58,y+9);
  gfx->fillRect(x+8,y+24,w-16,30,ink);
  gfx->fillTriangle(x,y+39,x+8,y+24,x+8,y+54,ink);
  gfx->fillTriangle(x+w,y+39,x+w-8,y+24,x+w-8,y+54,ink);
  gfx->drawFastHLine(x+10,y+25,w-20,edge);
  gfx->setTextColor(C565(0xf5,0xd8,0x48)); gfx->setTextSize(1); gfx->setCursor(x+16,y+35); gfx->print(T(S_HP));
  int bx=x+48, by=y+33, bw=w-66;
  gfx->fillRect(bx,by,bw,12,edge); gfx->fillRect(bx+2,by+2,bw-4,8,empty);
  int fill=(int)((uint32_t)(bw-4)*cur/maxHp);
  uint16_t hpCol=cur*4>maxHp?UI_BAR_OK:(cur*8>maxHp?C565(0xf2,0xc2,0x30):C565(0xe8,0x40,0x38));
  if(fill>0) gfx->fillRect(bx+2,by+2,fill,8,hpCol);
  char hp[18]; snprintf(hp,sizeof(hp),"%u/%u",(unsigned)cur,(unsigned)maxHp);
  gfx->setTextColor(UI_WHITE); gfx->setTextSize(1); gfx->setCursor(x+w-78,y+47); gfx->print(hp);
}

uint8_t battleTerrainFor(uint8_t type1, uint8_t type2) {
  if (type1==TYPE_WATER || type1==TYPE_ICE || type2==TYPE_WATER || type2==TYPE_ICE) return 2;
  if (type1==TYPE_ROCK || type1==TYPE_GROUND || type1==TYPE_STEEL || type1==TYPE_FIRE ||
      type2==TYPE_ROCK || type2==TYPE_GROUND || type2==TYPE_STEEL || type2==TYPE_FIRE) return 1;
  return 0;
}

void drawBattleBackgroundBiome(uint8_t biome) {
  uint8_t phase=currentDayPhase();
  uint8_t sourcePhase=(phase==3)?2:(phase==1?0:1); // jour, après-midi, nuit
  const BattleBgRef &asset=BATTLE_BG_ASSETS[biome<6?biome:0][sourcePhase];
  uint32_t pixel=0;
  for(uint16_t i=0;i<asset.rleSize;i+=2) {
    uint16_t left=pgm_read_byte(&asset.rle[i]);
    uint8_t palette=pgm_read_byte(&asset.rle[i+1]);
    uint16_t color=pgm_read_word(&asset.pal[palette]);
    while(left) {
      uint16_t sx=pixel%BATTLE_BG_W;
      uint16_t sy=pixel/BATTLE_BG_W;
      uint16_t rowLeft=BATTLE_BG_W-sx;
      uint16_t take=left<rowLeft?left:rowLeft;
      int dy=(int)((uint32_t)sy*320/BATTLE_BG_H);
      int dy2=(int)((uint32_t)(sy+1)*320/BATTLE_BG_H);
      gfx->fillRect(-23+sx*2,dy,take*2,dy2-dy,color);
      pixel+=take;
      left-=take;
    }
  }
}

uint8_t battleGroundStyleFor(uint8_t type1,uint8_t type2) {
  if(type1==TYPE_ICE||type2==TYPE_ICE) return 5;
  if(type1==TYPE_WATER||type2==TYPE_WATER) return 1;
  // Les Pokemon Feu utilisent la plateforme herbe classique, pas le sol Ligue/volcan.
  if(type1==TYPE_FIRE||type2==TYPE_FIRE) return 0;
  if(type1==TYPE_GROUND||type2==TYPE_GROUND) return 2;
  if(type1==TYPE_ROCK||type2==TYPE_ROCK||type1==TYPE_STEEL||type2==TYPE_STEEL) return 4;
  if(type1==TYPE_GRASS||type2==TYPE_GRASS||type1==TYPE_BUG||type2==TYPE_BUG) return 0;
  return 0;
}

void drawBattleBaseFor(uint8_t style,bool enemy) {
  const uint16_t *pal=enemy?BATTLE_BASE_E_0_PAL:BATTLE_BASE_P_0_PAL;
  const uint8_t *rle=enemy?BATTLE_BASE_E_0_RLE:BATTLE_BASE_P_0_RLE;
  uint16_t rleSize=enemy?sizeof(BATTLE_BASE_E_0_RLE):sizeof(BATTLE_BASE_P_0_RLE);
  switch(style) {
    case 1: pal=enemy?BATTLE_BASE_E_1_PAL:BATTLE_BASE_P_1_PAL; rle=enemy?BATTLE_BASE_E_1_RLE:BATTLE_BASE_P_1_RLE; rleSize=enemy?sizeof(BATTLE_BASE_E_1_RLE):sizeof(BATTLE_BASE_P_1_RLE); break;
    case 2: pal=enemy?BATTLE_BASE_E_2_PAL:BATTLE_BASE_P_2_PAL; rle=enemy?BATTLE_BASE_E_2_RLE:BATTLE_BASE_P_2_RLE; rleSize=enemy?sizeof(BATTLE_BASE_E_2_RLE):sizeof(BATTLE_BASE_P_2_RLE); break;
    case 3: pal=enemy?BATTLE_BASE_E_3_PAL:BATTLE_BASE_P_3_PAL; rle=enemy?BATTLE_BASE_E_3_RLE:BATTLE_BASE_P_3_RLE; rleSize=enemy?sizeof(BATTLE_BASE_E_3_RLE):sizeof(BATTLE_BASE_P_3_RLE); break;
    case 4: pal=enemy?BATTLE_BASE_E_4_PAL:BATTLE_BASE_P_4_PAL; rle=enemy?BATTLE_BASE_E_4_RLE:BATTLE_BASE_P_4_RLE; rleSize=enemy?sizeof(BATTLE_BASE_E_4_RLE):sizeof(BATTLE_BASE_P_4_RLE); break;
    case 5: pal=enemy?BATTLE_BASE_E_5_PAL:BATTLE_BASE_P_5_PAL; rle=enemy?BATTLE_BASE_E_5_RLE:BATTLE_BASE_P_5_RLE; rleSize=enemy?sizeof(BATTLE_BASE_E_5_RLE):sizeof(BATTLE_BASE_P_5_RLE); break;
  }
  // 256 px agrandis x2 puis centrés : 23 px sont rognés de chaque côté
  // par le cadrage 466 px, sans déformer les pixels ni les deux plateformes.
  uint32_t pixel=0;
  for(uint16_t i=0;i<rleSize;i+=2) {
    uint16_t left=pgm_read_byte(&rle[i]);
    uint8_t palette=pgm_read_byte(&rle[i+1]);
    uint16_t color=palette==255?0:pgm_read_word(&pal[palette]);
    while(left) {
      uint16_t sx=pixel%BATTLE_BASE_W;
      uint16_t sy=pixel/BATTLE_BASE_W;
      uint16_t rowLeft=BATTLE_BASE_W-sx;
      uint16_t take=left<rowLeft?left:rowLeft;
      if(palette!=255) gfx->fillRect(-23+sx*2,102+sy*2,take*2,2,color);
      pixel+=take;
      left-=take;
    }
  }
}

void drawBattlePlatform(int x, int y, int w, int h, uint8_t terrain) {
  uint16_t dark = terrain==2 ? C565(0x1d,0x68,0xa4) : terrain==1 ? C565(0x6e,0x50,0x35) : C565(0x2d,0x75,0x36);
  uint16_t mid  = terrain==2 ? C565(0x35,0x9f,0xd2) : terrain==1 ? C565(0xa5,0x7a,0x4d) : C565(0x48,0xa8,0x48);
  uint16_t lite = terrain==2 ? C565(0x70,0xd4,0xea) : terrain==1 ? C565(0xd2,0xac,0x72) : C565(0x78,0xd0,0x5a);

  // Composition classique : butte adverse ouverte à droite et premier plan
  // du joueur ouvert à gauche. Les contours sont faits en marches de pixels.
  const bool enemy=(x>100);
  static const uint8_t inset[8]={64,43,27,14,5,0,8,25};
  int band=max(4,h/8);
  gfx->fillRect(x+(enemy?54:0),y+h-1,w-54,4,C565(0x91,0x98,0x8a));
  for(int row=0;row<8;row++) {
    int cut=inset[row];
    int px=enemy ? x+cut : x;
    int pw=max(2,w-cut);
    uint16_t col=(row==0||row>=6)?dark:(row<=2?lite:mid);
    gfx->fillRect(px,y+row*band,pw,band+1,col);
  }

  // Petits motifs carrés, sans anti-aliasing, propres à chaque biome.
  if (terrain==2) {
    for(int i=0;i<5;i++) {
      int px=x+28+i*34, py=y+14+(i&1)*17;
      gfx->fillRect(px,py,24,3,C565(0xdb,0xfa,0xff));
      gfx->fillRect(px+6,py+5,16,3,C565(0x25,0x82,0xbf));
    }
  } else if (terrain==1) {
    for(int i=0;i<7;i++) {
      int px=x+25+i*26,py=y+15+(i%3)*11;
      gfx->fillRect(px,py,10,7,C565(0x68,0x4a,0x32));
      gfx->fillRect(px+2,py,6,2,C565(0xee,0xca,0x8b));
      if(i&1) gfx->fillRect(px+8,py+5,5,4,C565(0x87,0x60,0x3d));
    }
  } else {
    // Liseré d'herbe irrégulier visible sur le bord, comme sur la référence.
    for(int i=0;i<18;i++) {
      int px=x+(enemy?48:0)+i*12, py=y+4+(i%4)*2;
      gfx->fillRect(px,py,4,9+(i%3)*3,C565(0x25,0x83,0x31));
      gfx->fillRect(px+4,py+5,5,4,C565(0x91,0xdc,0x63));
    }
  }
}

void drawBattleShinyEntrance(int cx,int cy) {
  if (!battleEnemyShiny || !battleShinyFxUntil || millis()>=battleShinyFxUntil) return;
  uint32_t elapsed=1800-(battleShinyFxUntil-millis());
  uint8_t phase=(uint8_t)((elapsed/90)&7);
  static const int8_t px[8]={-48,-31,0,35,51,31,-4,-38};
  static const int8_t py[8]={-10,-43,-55,-40,-4,31,42,26};
  for(uint8_t i=0;i<8;i++) {
    uint8_t pulse=(phase+i)&7;
    int x=cx+px[i]+(px[i]*pulse)/28;
    int y=cy+py[i]+(py[i]*pulse)/28;
    int r=2+(pulse<4?pulse:7-pulse);
    uint16_t color=(i&1)?C565(0xff,0xf2,0x68):C565(0x6e,0xe9,0xff);
    gfx->drawFastHLine(x-r,y,r*2+1,color);
    gfx->drawFastVLine(x,y-r,r*2+1,color);
    if(r>=4){ gfx->drawPixel(x-2,y-2,color); gfx->drawPixel(x+2,y+2,color); }
  }
}

void renderBattle() {
  if (battleResolved) battleDirty = false;
  const DexEntry &mine = DEX_TBL[pet.speciesId];
  const DexEntry &foe = DEX_TBL[battleDex];
  uint8_t biome=mine.biome<6?mine.biome:0;
  static const uint16_t skies[6]={C565(0xe8,0xf3,0xd9),C565(0xd9,0xf4,0xfa),C565(0xdf,0xef,0xd4),C565(0xed,0xd8,0xc6),C565(0xf1,0xe4,0xcf),C565(0xe9,0xf3,0xfa)};
  uint16_t sky=skies[biome];
  gfx->fillScreen(sky);
  drawBattleBackgroundBiome(biome);
  drawBattleBaseFor(battleGroundStyleFor(foe.type1,foe.type2),true);
  drawBattleBaseFor(battleGroundStyleFor(mine.type1,mine.type2),false);

  // Zone sûre du cercle : aucune pointe ni information ne touche les bords.
  uint8_t enemySex=battleSpeciesGenderless(battleDex)?2:battleEnemySex;
  uint8_t playerSex=battleSpeciesGenderless(pet.speciesId)?2:battlePlayerSex;
  uint8_t phase=currentDayPhase();
  uint16_t nameColor=(phase==3||biome==2||biome==3)?UI_WHITE:UI_INK;
  char enemyName[28];
  snprintf(enemyName,sizeof(enemyName),"%s%s",battleEnemyShiny?"*":"",dexName(battleDex));
  drawBattleStatusBar(enemyName,battleLevel,enemySex,82,66,190,battleRun.enemyHp,battleRun.enemyMaxHp,
                      battleEnemyShiny?UI_BAR_WARN:nameColor);
  if(pet.isCaught(battleDex)) drawBattleCaughtBall(102,137);
  drawBattleStatusBar(pet.nick[0]?pet.nick:dexName(pet.speciesId),battlePlayer.level,playerSex,220,234,220,battleRun.playerHp,battleRun.playerMaxHp,nameColor);

  // Sprites standardisés dans une boîte visuelle ~82 px, quelle que soit l'espèce.
  if (wildPmd.loaded) drawBattlePmd(wildPmd, battleDex, 354, 190, 84, false);
  else {
    const uint8_t *th=thumbs.get(battleDex);
    if (th) drawBattleThumb(th,battleDex,354,190,84,false);
  }
  drawBattleShinyEntrance(354,146);
  if (pmd.loaded) drawBattlePmd(pmd, pet.speciesId, 110, 302, 104, false);
  else {
    const uint8_t *th=thumbs.get(pet.speciesId);
    if (th) drawBattleThumb(th,pet.speciesId,110,302,104,false);
  }

  if (battleResolved) {
    // Carte de résultat compacte : plus de tours/dégâts empilés au centre.
    gfx->fillRoundRect(78, 326, 310, 122, 18, C565(0x12,0x1c,0x36));
    gfx->drawRoundRect(78, 326, 310, 122, 18,
                       battleTurn.playerWon ? UI_BAR_OK : UI_BAR_BAD);

    const char *res = battleTurn.playerWon ? T(S_WIN) : T(S_LOSS);
    gfx->setTextColor(battleTurn.playerWon ? UI_BAR_OK : UI_BAR_BAD);
    gfx->setTextSize(3);
    gfx->setCursor(CX - (int)strlen(res) * 9, 340);
    gfx->print(res);

    int infoY = 373;
    if (battleTurn.playerWon) {
      char reward[20];
      battleRewardText(reward, sizeof(reward));
      if (reward[0]) {
        gfx->setTextColor(UI_BAR_WARN);
        gfx->setTextSize(2);
        gfx->setCursor(CX - (int)strlen(reward) * 6, infoY);
        gfx->print(reward);
      }
    } else if (battleRespectCatch && battleCatchOffered && !battleCatchDone) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(1);
      gfx->setCursor(CX - (int)strlen(T(S_CLOSE_CHANCE)) * 3, infoY + 3);
      gfx->print(T(S_CLOSE_CHANCE));
    }

    if (battleCatchOffered && !battleCatchDone) {
      gfx->fillRoundRect(88, 394, 138, 44, 13, UI_BAR_OK);
      gfx->fillRoundRect(240, 394, 138, 44, 13, UI_TRACK);
      gfx->setTextColor(uiContrastText(UI_BAR_OK));
      drawBattleButtonLabel(88, 408, 138, T(S_CATCH_WILD));
      gfx->setTextColor(uiContrastText(UI_TRACK));
      drawBattleButtonLabel(240, 408, 138, T(S_LEAVE_WILD));
    } else {
      if (battleCatchDone && battleCatchTried) {
        const char *catchMsg = battleCatchSuccess ? T(S_CAUGHT_OK) : T(S_ESCAPED);
        gfx->setTextColor(battleCatchSuccess ? UI_BAR_OK : UI_BAR_BAD);
        gfx->setTextSize(1);
        gfx->setCursor(CX - (int)strlen(catchMsg) * 3, 391);
        gfx->print(catchMsg);
      }
      gfx->fillRoundRect(118, 398, 230, 42, 13, UI_BAR_OK);
      gfx->setTextColor(uiContrastText(UI_BAR_OK));
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(T(S_OK)) * 6, 411);
      gfx->print(T(S_OK));
    }
  } else {
    // Bandeau continu jusqu'aux bords : l'ecran rond masque naturellement ses
    // extremites et evite l'effet de petite boite posee sur le combat.
    gfx->fillRect(0, 320, 466, 146, C565(0x8e,0xa6,0xb9));
    gfx->drawFastHLine(0,320,466,C565(0xe8,0x3b,0x45));
    gfx->drawFastHLine(0,324,466,C565(0xe8,0x3b,0x45));

    if (battleMsg[0]) {
      gfx->setTextColor(UI_WHITE);
      gfx->setTextSize(1);
      int msgW=(int)strlen(battleMsg)*6;
      gfx->setCursor(CX-msgW/2,338);
      gfx->print(battleMsg);
    }

    if (battleAttackMenuUntil) {
      // Trois attaques fiables directement reliées au moteur existant.
      gfx->fillRoundRect(82, 350, 96, 52, 12, C565(0x1a,0x54,0x9a));
      gfx->fillRoundRect(190, 350, 96, 52, 12, UI_BAR_BAD);
      gfx->fillRoundRect(298, 350, 96, 52, 12, UI_BAR_WARN);

      gfx->setTextColor(uiContrastText(C565(0x1a,0x54,0x9a)));
      drawBattleButtonLabel(82,363,96,T(S_QUICK_ATTACK));
      gfx->setTextColor(uiContrastText(UI_BAR_BAD));
      drawBattleButtonLabel(190,363,96,T(S_NORMAL_ATTACK));
      gfx->setTextColor(uiContrastText(UI_BAR_WARN));
      drawBattleButtonLabel(298,363,96,T(S_HEAVY_ATTACK));

      gfx->setTextSize(1);
      gfx->setTextColor(UI_WHITE);
      gfx->setCursor(117,387); gfx->print("85%");
      gfx->setCursor(223,387); gfx->print("100%");
      gfx->setCursor(328,387); gfx->print("125%");
    } else {
      gfx->fillRoundRect(72,334,154,43,10,UI_BAR_BAD);
      gfx->fillRoundRect(240,334,154,43,10,C565(0x2d,0x73,0xb9));
      gfx->fillRoundRect(72,383,154,43,10,UI_BAR_WARN);
      gfx->fillRoundRect(240,383,154,43,10,UI_WHITE);

      gfx->setTextColor(uiContrastText(UI_BAR_BAD));
      drawBattleButtonLabel(72,347,154,T(S_ATTACK));
      gfx->setTextColor(uiContrastText(C565(0x2d,0x73,0xb9)));
      drawBattleButtonLabel(240,347,154,T(S_DODGE));
      char restLabel[18];
      snprintf(restLabel,sizeof(restLabel),"%s %u",T(S_REST),battleRun.restUsesLeft);
      gfx->setTextColor(uiContrastText(UI_BAR_WARN));
      drawBattleButtonLabel(72,396,154,restLabel);
      gfx->setTextColor(UI_INK);
      drawBattleButtonLabel(240,396,154,T(S_RUN_BATTLE));
    }
  }

  gfx->flush();
}

// ---------- ficha del bicho (deslizar vertical) ----------

void drawCardStat(int y, const char *label, uint16_t val, uint16_t maxBar, uint16_t color) {
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(96, y);
  gfx->print(label);
  char num[8];
  snprintf(num, sizeof(num), "%u", val);
  gfx->setCursor(330, y);
  gfx->print(num);
  int bw = 160;
  int fw = (int)val * bw / maxBar;
  if (fw > bw) fw = bw;
  gfx->fillRoundRect(150, y + 2, bw, 11, 3, UI_TRACK);
  if (fw > 2) gfx->fillRoundRect(150, y + 2, fw, 11, 3, color);
}

// ---------- ajuste de hora en pantalla (deslizar abajo) ----------
// El usuario pone su hora LOCAL a ojo; el firmware la usa tal cual, asi que
// no hay que gestionar zona horaria. Preserva el dia (no rompe racha/edad).

void openClock() {
  uint32_t e = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  clockH = (e / 3600) % 24;
  clockM = (e / 60) % 60;
  clockOpen = true;
  settingsPage = 0;
  clockDirty = true;
  lockTouchBrief();
  sfxPlay(SFX_MENU);
}

void applyClock() {
  uint32_t base = pet.lastSeenEpoch ? pet.lastSeenEpoch : rtcEpoch();
  uint32_t e = (base / 86400) * 86400 + (uint32_t)clockH * 3600 + (uint32_t)clockM * 60;
  rtcSetEpoch(e);
  pet.setClock(e);
  clockOpen = false;
  settingsPage = 0;
  markUiDirty();
  lockTouchBrief();
}

void drawClockBtn(int x, int y, const char *l) {
  gfx->fillRoundRect(x, y, 58, 58, 12, uiPanel());
  gfx->drawRoundRect(x, y, 58, 58, 12, uiInk());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(4);
  gfx->setCursor(x + 17, y + 15);
  gfx->print(l);
}

// pildoras de idioma centradas en y; rellena la activa
#define LANG_PILL_Y 296
#define LANG_PILL_H 30
#define LANG_PILL_X 336          // pildora de idioma (cicla los 6 al tocar)
#define LANG_PILL_W 96
#define SOUND_PILL_X 24
#define SOUND_PILL_W 116
#define PSAVE_PILL_X 150
#define PSAVE_PILL_W 176

const char *soundModeLabel() {
  switch (audioMode()) {
    case SOUND_FULL: return T(S_SND_FULL);
    case SOUND_MED: return T(S_SND_MED);
    case SOUND_LOW: return T(S_SND_LOW);
    default: return T(S_SND_OFF);
  }
}

uint8_t nextSoundMode() {
  switch (audioMode()) {
    case SOUND_FULL: return SOUND_MED;
    case SOUND_MED: return SOUND_LOW;
    case SOUND_LOW: return SOUND_OFF;
    default: return SOUND_FULL;
  }
}
static const char *const LANG_CODES[LANG_COUNT] = { "ES", "EN", "FR", "DE", "IT", "PT" };

const char *powerSaveLabel() {
  return powerSave ? T(S_PSAVE_ON) : T(S_PSAVE_OFF);
}

void drawStatusLine(int y, const char *label, const char *value, uint16_t valueColor) {
  gfx->setTextSize(1);
  gfx->setTextColor(uiSub());
  gfx->setCursor(94, y);
  gfx->print(label);
  gfx->setTextColor(valueColor);
  gfx->setCursor(172, y);
  gfx->print(value);
}

static const char *const HELP_WORD[LANG_COUNT] = { "AYUDA", "HELP", "AIDE", "HILFE", "AIUTO", "AJUDA" };
static const char *const HELP_OK[LANG_COUNT] = { "OK", "OK", "OK", "OK", "OK", "OK" };

static const char *const HELP_TITLES[LANG_COUNT][HELP_PAGE_COUNT] = {
  { "CUIDADO", "SUENO/ENERGIA", "MINIJUEGOS", "COMBATE 1", "COMBATE 2", "COLECCION", "EXTRAS", "EXPEDICION" },
  { "CARE", "SLEEP/ENERGY", "MINIGAMES", "BATTLE 1", "BATTLE 2", "COLLECTION", "EXTRAS", "EXPEDITION" },
  { "SOIN", "SOMMEIL/ENE", "MINI-JEUX", "COMBAT 1", "COMBAT 2", "COLLECTION", "EXTRAS", "EXPEDITION" },
  { "PFLEGE", "SCHLAF/ENERGIE", "MINISPIELE", "KAMPF 1", "KAMPF 2", "SAMMLUNG", "EXTRAS", "EXPEDITION" },
  { "CURA", "SONNO/ENERGIA", "MINIGIOCHI", "LOTTA 1", "LOTTA 2", "COLLEZIONE", "EXTRA", "SPEDIZIONE" },
  { "CUIDADO", "SONO/ENERGIA", "MINIJOGOS", "BATALHA 1", "BATALHA 2", "COLECAO", "EXTRAS", "EXPEDICAO" },
};

static const char *const HELP_LINES[LANG_COUNT][HELP_PAGE_COUNT][HELP_LINE_COUNT] = {
  {
    { "Comida baja = descuido.", "Jugar sube alegria.", "Bano limpia suciedad.", "Tocar da alegria/vinc.", "Peso alto te frena.", "Dulce alegra, engorda." },
    { "Dormir recupera energia.", "Durmiendo todo baja lento.", "Luz despierta o duerme.", "PWR corto apaga pantalla.", "Ahorro usa light sleep.", "Sin borrar conserva save." },
    { "Bola: toca la bola.", "Atrapa: toca iconos.", "Memo: repite secuencia.", "Limpia: toca manchas.", "Tipo: elige ventaja.", "Dan records y entreno." },
    { "Rapido: menos dano.", "Rival esquiva poco.", "Recibes algo menos dano.", "Fuerte: mas dano.", "Riesgo y contra mayor.", "No siempre conviene." },
    { "Esquivar evita dano.", "Si sale: Contra listo.", "Prox ataque pega mas.", "Ruhe/Descanso cura 2x.", "Tambien da Guardia.", "Tipos suben/bajan dano." },
    { "Pokedex: desliza lado.", "Criado y atrapado cuentan.", "10/25/50/100/151: marcos.", "Perfil: elige marco.", "Detalle conocido: chirp.", "SON TODO: toca pet." },
    { "Diario da metas diarias.", "Eventos salen raros.", "Batallas salvajes opc.", "Captura tras ganar.", "Rachas y medallas quedan.", "Sonido se ajusta abajo." },
    { "Expedicion: 15/30/60 min.", "Cuesta energia al salir.", "El bicho sigue disponible.", "Buen cuidado mejora premio.", "Recoge 1 objeto al volver.", "Objetos max. x3." },
  },
  {
    { "Low food = slip-up.", "Play raises joy.", "Bath cleans dirt.", "Petting gives joy/bond.", "High weight slows you.", "Candy cheers but fattens." },
    { "Sleep restores energy.", "Needs decay slower asleep.", "Light toggles sleep.", "Short PWR screen off.", "Power Save light-sleeps.", "No erase keeps saves." },
    { "Ball: tap the ball.", "Catch: tap icons.", "Memo: repeat sequence.", "Clean: tap stains.", "Type: pick advantage.", "Records and training." },
    { "Quick: lower damage.", "Enemy dodges less.", "You take less damage.", "Heavy: more damage.", "More risk/counterplay.", "Not always best." },
    { "Dodge avoids damage.", "Success: Counter ready.", "Next attack hits harder.", "Rest heals only 2x.", "Rest also gives Guard.", "Types change damage." },
    { "Pokedex: side swipe.", "Raised and caught count.", "10/25/50/100/151: frames.", "Profile: choose frame.", "Known detail: species chirp.", "SND ALL: tap pet." },
    { "Daily gives small goals.", "Events appear rarely.", "Wild battles are optional.", "Catch after winning.", "Streaks/medals persist.", "Sound is in settings." },
    { "Expedition: 15/30/60 min.", "Energy is spent at start.", "Pet stays available.", "Care and bond improve finds.", "Claim 1 item when back.", "Items hold max x3." },
  },
  {
    { "Faim basse = erreur.", "Jouer monte la joie.", "Bain nettoie.", "Caresse donne lien/joie.", "Poids haut ralentit.", "Bonbon rend gros." },
    { "Sommeil rend energie.", "Besoins baissent moins.", "Lumiere dort/reveille.", "PWR court eteint ecran.", "Eco utilise light sleep.", "Sans erase garde save." },
    { "Balle: touche la balle.", "Attrape: touche icones.", "Memo: repete sequence.", "Nettoie: touche taches.", "Type: choisis avantage.", "Records et entrainement." },
    { "Rapide: degats bas.", "Ennemi esquive moins.", "Tu subis moins.", "Fort: degats hauts.", "Risque plus grand.", "Pas toujours meilleur." },
    { "Esquive evite degats.", "Succes: Contre pret.", "Prochaine attaque plus.", "Repos soigne 2 fois.", "Repos donne Garde.", "Types changent degats." },
    { "Pokedex: glisse cote.", "Eleve et capture comptent.", "10/25/50/100/151: cadres.", "Profil: choisis cadre.", "Detail connu: chirp.", "SON TOUT: touche pet." },
    { "Quotidien donne buts.", "Events rares.", "Combats sauvages option.", "Capture apres victoire.", "Series/medailles restent.", "Son dans reglages." },
    { "Expedition: 15/30/60 min.", "Energie payee au depart.", "Le pet reste disponible.", "Soin/lien aide le butin.", "Prends 1 objet au retour.", "Objets max x3." },
  },
  {
    { "Food 0 = Patzer.", "Spielen hebt Freude.", "Bad reinigt Hygiene.", "Streicheln gibt Bond.", "Hohes Gewicht bremst.", "Candy freut, macht dick." },
    { "Schlaf gibt Energie.", "Needs sinken langsamer.", "Licht: schlafen/wach.", "PWR kurz: Screen aus.", "Sparen nutzt Light Sleep.", "Ohne Erase bleibt Save." },
    { "Ball: Ball antippen.", "Fangen: Icons treffen.", "Memo: Folge merken.", "Putzen: Flecken tippen.", "Typ: Vorteil waehlen.", "Gibt Rekorde/Training." },
    { "Schnell: weniger Schaden.", "Gegner weicht selten aus.", "Du kassierst weniger.", "Stark: mehr Schaden.", "Mehr Risiko/Gegendruck.", "Nicht immer beste Wahl." },
    { "Ausweichen meidet Schaden.", "Klappt es: Konter bereit.", "Naechster Angriff staerker.", "Ruhen heilt nur 2x.", "Ruhen gibt auch Schutz.", "Typen aendern Schaden." },
    { "Pokedex: seitlich wischen.", "Aufz./gefangen zaehlen.", "10/25/50/100/151: Rahmen.", "Profil: Rahmen waehlen.", "Bekanntes Detail: Chirp.", "TON VIEL: Pet tippen." },
    { "Taeglich gibt Ziele.", "Events sind selten.", "Wildkampf ist optional.", "Fangen nach Sieg.", "Serien/Medaillen bleiben.", "Ton unten einstellen." },
    { "Expedition: 15/30/60 Min.", "Kostet beim Start Energie.", "Pet bleibt verfuegbar.", "Pflege/Bond verbessert Fund.", "Fund danach einsammeln.", "Items maximal x3." },
  },
  {
    { "Cibo 0 = errore.", "Gioca aumenta gioia.", "Bagno pulisce.", "Carezza da legame.", "Peso alto rallenta.", "Dolce rallegra, ingrassa." },
    { "Sonno da energia.", "Bisogni calano meno.", "Luce dorme/sveglia.", "PWR corto spegne schermo.", "Risparmio usa light sleep.", "Senza erase salva." },
    { "Palla: tocca palla.", "Prendi: tocca icone.", "Memo: ripeti sequenza.", "Pulisci: tocca macchie.", "Tipo: scegli vantaggio.", "Record e allenamento." },
    { "Rapido: meno danni.", "Nemico schiva meno.", "Subisci meno danni.", "Forte: piu danni.", "Piu rischio.", "Non sempre migliore." },
    { "Schiva evita danni.", "Successo: contro pronto.", "Prox attacco piu forte.", "Riposo cura solo 2x.", "Riposo da Guardia.", "Tipi cambiano danni." },
    { "Pokedex: scorri lato.", "Allevato e preso contano.", "10/25/50/100/151: cornici.", "Profilo: scegli cornice.", "Dettaglio noto: chirp.", "SON TUTTO: tocca pet." },
    { "Quotidiano da obiettivi.", "Eventi rari.", "Lotte selvatiche opz.", "Cattura dopo vittoria.", "Serie/medaglie restano.", "Audio nei settaggi." },
    { "Spedizione: 15/30/60 min.", "Energia spesa alla partenza.", "Il pet resta disponibile.", "Cura/legame migliora premio.", "Ritira 1 oggetto al ritorno.", "Oggetti max x3." },
  },
  {
    { "Comida 0 = falha.", "Jogar sobe alegria.", "Banho limpa.", "Carinho da vinculo.", "Peso alto atrasa.", "Doce alegra, engorda." },
    { "Sono da energia.", "Necessidades caem menos.", "Luz dorme/acorda.", "PWR curto apaga tela.", "Poupanca usa light sleep.", "Sem erase guarda save." },
    { "Bola: toque na bola.", "Pegar: toque icones.", "Memo: repita sequencia.", "Limpa: toque manchas.", "Tipo: escolha vantagem.", "Recordes e treino." },
    { "Rapido: dano menor.", "Rival desvia menos.", "Voce recebe menos.", "Forte: dano maior.", "Mais risco.", "Nem sempre melhor." },
    { "Desviar evita dano.", "Sucesso: contra pronto.", "Prox ataque mais forte.", "Descanso cura so 2x.", "Descanso da Guarda.", "Tipos mudam dano." },
    { "Pokedex: deslize lado.", "Criado e apanhado contam.", "10/25/50/100/151: molduras.", "Perfil: escolha moldura.", "Detalhe conhecido: chirp.", "SOM TODO: toque pet." },
    { "Diario da metas.", "Eventos sao raros.", "Batalha selvagem opc.", "Captura apos vitoria.", "Series/medalhas ficam.", "Som nos ajustes." },
    { "Expedicao: 15/30/60 min.", "Energia gasta ao sair.", "Pet fica disponivel.", "Cuidado/laco melhora premio.", "Recolhe 1 item ao voltar.", "Itens max x3." },
  },
};

void renderHelp() {
  helpDirty = false;
  gfx->fillScreen(uiBg());
  uint8_t lang = (gLang < LANG_COUNT) ? (uint8_t)gLang : (uint8_t)LANG_EN;
  if (helpPage >= HELP_PAGE_COUNT) helpPage = 0;

  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  const char *h = HELP_WORD[lang];
  gfx->setCursor(CX - strlen(h) * 9, 36);
  gfx->print(h);

  gfx->setTextSize(2);
  gfx->setTextColor(UI_BAR_BAD);
  const char *title = HELP_TITLES[lang][helpPage];
  gfx->setCursor(CX - strlen(title) * 6, 76);
  gfx->print(title);

  gfx->setTextColor(uiInk());
  for (uint8_t i = 0; i < HELP_LINE_COUNT; i++) {
    const char *line = HELP_LINES[lang][helpPage][i];
    gfx->setCursor(CX - strlen(line) * 6, 116 + i * 34);
    gfx->print(line);
  }

  char pg[12];
  snprintf(pg, sizeof(pg), "%u/%u", helpPage + 1, HELP_PAGE_COUNT);
  gfx->setTextColor(uiSub());
  gfx->setCursor(CX - strlen(pg) * 6, 340);
  gfx->print(pg);

  if (helpPage > 0) {
    gfx->fillRoundRect(84, 370, 62, 42, 12, uiPanel());
    gfx->drawRoundRect(84, 370, 62, 42, 12, uiInk());
    gfx->setTextColor(uiInk());
    gfx->setTextSize(2);
    gfx->setCursor(103, 384);
    gfx->print("<<");
  }
  gfx->fillRoundRect(154, 370, 158, 42, 12, UI_BAR_OK);
  gfx->setTextColor(uiContrastText(UI_BAR_OK));
  gfx->setTextSize(2);
  const char *ok = HELP_OK[lang];
  gfx->setCursor(154 + (158 - (int)strlen(ok) * 12) / 2, 384);
  gfx->print(ok);
  if (helpPage + 1 < HELP_PAGE_COUNT) {
    gfx->fillRoundRect(320, 370, 62, 42, 12, uiPanel());
    gfx->drawRoundRect(320, 370, 62, 42, 12, uiInk());
    gfx->setTextColor(uiInk());
    gfx->setCursor(339, 384);
    gfx->print(">>");
  }
  gfx->flush();
}

void openHelp() {
  helpPage = 0;
  helpOpen = true;
  clockOpen = false;
  helpDirty = true;
  lockTouchBrief();
  sfxPlay(SFX_MENU);
}

void helpTap(int16_t x, int16_t y) {
  if (y >= 364 && y <= 420) {
    if (x >= 84 && x <= 146 && helpPage > 0) {
      helpPage--;
      helpDirty = true;
      sfxPlay(SFX_MENU);
      return;
    }
    if (x >= 320 && x <= 382 && helpPage + 1 < HELP_PAGE_COUNT) {
      helpPage++;
      helpDirty = true;
      sfxPlay(SFX_MENU);
      return;
    }
    if (x >= 154 && x <= 312) {
      helpOpen = false;
      clockOpen = true;
      clockDirty = true;
      lockTouchBrief();
      sfxPlay(SFX_TAP);
      return;
    }
  }
  if (x > CX && helpPage + 1 < HELP_PAGE_COUNT) { helpPage++; helpDirty = true; sfxPlay(SFX_MENU); return; }
  if (x < CX && helpPage > 0) { helpPage--; helpDirty = true; sfxPlay(SFX_MENU); return; }
}

const char *frameTemplateName(uint8_t frame) {
  static const StrId NAMES[] = {
    S_FRAME_NONE, S_FRAME_CLASSIC, S_FRAME_POKEBALL,
    S_FRAME_GOLD, S_FRAME_SILVER, S_FRAME_NEON
  };
  return frame < 6 ? T(NAMES[frame]) : T(S_FRAME_NONE);
}

void drawSettingsRow(int x, int y, int w, int h, const char *label, const char *value, bool selected=false) {
  uint16_t fill = selected ? C565(0x43,0x2f,0x75) : uiPanel();
  gfx->fillRoundRect(x,y,w,h,12,fill);
  gfx->drawRoundRect(x,y,w,h,12, selected ? C565(0xa8,0x7d,0xff) : uiLine());
  int valueLen=(value && value[0]) ? (int)strlen(value) : 1;
  uint8_t textScale=((int)strlen(label)*12 + valueLen*12 + 46 <= w) ? 2 : 1;
  int textY=y+(h-8*textScale)/2;
  gfx->setTextColor(selected ? UI_WHITE : uiInk());
  gfx->setTextSize(textScale);
  gfx->setCursor(x+14,textY);
  gfx->print(label);
  if (value && value[0]) {
    int vw=(int)strlen(value)*6*textScale;
    uint16_t valueInk=darkMode ? C565(0xd8,0xdf,0xf2) : C565(0x32,0x38,0x44);
    gfx->setTextColor(selected ? UI_WHITE : valueInk);
    gfx->setCursor(x+w-vw-14,textY);
    gfx->print(value);
  } else {
    uint16_t valueInk=darkMode ? C565(0xd8,0xdf,0xf2) : C565(0x32,0x38,0x44);
    gfx->setTextColor(selected ? UI_WHITE : valueInk);
    gfx->setCursor(x+w-18*textScale,textY);
    gfx->print(">");
  }
}

static const char *displayFrameLabel() {
  static const char *const L[LANG_COUNT]={"MARCO","FRAME","CADRE","RAHMEN","CORNICE","MOLDURA"};
  return L[gLang];
}

static const char *displayBoxLabel() {
  static const char *const L[LANG_COUNT]={"CAJA","BOX","BOITE","BOX","BOX","CAIXA"};
  return L[gLang];
}

void drawSettingsTitle(const char *title);

void drawBoxBackgroundAsset(uint8_t style,int x0,int y0,int scale) {
  if(style>=BOX_BG_COUNT) style=0;
  const BoxBgRef &asset=BOX_BG_ASSETS[style];
  uint32_t pixel=0;
  for(uint16_t i=0;i<asset.rleSize;i+=2) {
    uint8_t count=pgm_read_byte(&asset.rle[i]);
    uint16_t color=pgm_read_word(&asset.pal[pgm_read_byte(&asset.rle[i+1])]);
    while(count--) {
      int x=(int)(pixel%BOX_BG_W), y=(int)(pixel/BOX_BG_W);
      gfx->fillRect(x0+x*scale,y0+y*scale,scale,scale,color);
      pixel++;
    }
  }
}

void renderFrameSettings() {
  clockDirty=false;
  gfx->fillScreen(uiBg());

  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  const char *title=displayFrameLabel();
  gfx->setCursor(CX-(int)strlen(title)*9,38);
  gfx->print(title);

  // MODE : rouge coordonné à la Poké Ball en thème sombre.
  uint16_t modeFill = darkMode ? C565(0x8f,0x0f,0x19) : uiPanel();
  uint16_t modeLine = darkMode ? C565(0xff,0x28,0x38) : uiLine();
  gfx->fillRoundRect(64,88,338,48,12,modeFill);
  gfx->drawRoundRect(64,88,338,48,12,modeLine);
  gfx->setTextColor(darkMode ? UI_WHITE : uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(78,100);
  gfx->print(T(S_MODE_LABEL));
  const char *modeTxt = darkMode ? T(S_DARK) : T(S_LIGHT);
  gfx->setCursor(388-(int)strlen(modeTxt)*12,100);
  gfx->print(modeTxt);

  // Grande prévisualisation réelle du cadre.
  gfx->fillRoundRect(76,154,314,196,20,uiPanel2());
  gfx->drawRoundRect(76,154,314,196,20,
                     darkMode ? C565(0xff,0x28,0x38) : uiLine());
  drawCollectionFrame(CX,244,76,pet.collectionFrame);

  // Poké Ball propre, sans débordement du blanc.
  drawCleanPokeball(CX,244,30);

  uint8_t unlocked=pet.unlockedCollectionFrameCount();
  const char *fn=frameTemplateName(pet.collectionFrame);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX-(int)strlen(fn)*6,310);
  gfx->print(fn);

  char count[18];
  snprintf(count,sizeof(count),"%u/%u",(unsigned)(pet.collectionFrame+1),(unsigned)unlocked);
  gfx->setTextColor(uiSub());
  gfx->setTextSize(1);
  gfx->setCursor(CX-(int)strlen(count)*3,334);
  gfx->print(count);

  // Flèches de choix.
  gfx->fillRoundRect(82,218,52,52,14,uiPanel());
  gfx->drawRoundRect(82,218,52,52,14,
                     darkMode ? C565(0xff,0x28,0x38) : uiLine());
  gfx->fillRoundRect(332,218,52,52,14,uiPanel());
  gfx->drawRoundRect(332,218,52,52,14,
                     darkMode ? C565(0xff,0x28,0x38) : uiLine());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(98,229); gfx->print("<");
  gfx->setCursor(348,229); gfx->print(">");

  char langVal[10];
  snprintf(langVal,sizeof(langVal),"%s",LANG_CODES[gLang]);
  drawSettingsRow(64,352,164,38,T(S_LANG_LABEL),langVal,false);
  drawSettingsRow(238,352,164,38,T(S_POWER_SAVE_LABEL),powerSave ? T(S_ON) : T(S_OFF),powerSave);

  gfx->fillRoundRect(128,398,210,40,14,UI_BAR_OK);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(CX-12,410);
  gfx->print("OK");
  gfx->flush();
}

void renderDisplaySettings() {
  clockDirty=false; gfx->fillScreen(uiBg());
  drawSettingsTitle(T(S_DISPLAY_LABEL));
  drawSettingsRow(64,82,338,48,T(S_MODE_LABEL),darkMode?T(S_DARK):T(S_LIGHT),darkMode);
  drawSettingsRow(64,144,338,48,displayFrameLabel(),frameTemplateName(pet.collectionFrame),false);
  char boxVal[12]; snprintf(boxVal,sizeof(boxVal),"%u/16",(unsigned)(pet.boxBackground+1));
  drawSettingsRow(64,206,338,48,displayBoxLabel(),boxVal,false);
  char langVal[10]; snprintf(langVal,sizeof(langVal),"%s",LANG_CODES[gLang]);
  drawSettingsRow(64,268,164,42,T(S_LANG_LABEL),langVal,false);
  drawSettingsRow(238,268,164,42,T(S_POWER_SAVE_LABEL),powerSave?T(S_ON):T(S_OFF),powerSave);
  gfx->fillRoundRect(128,342,210,46,14,UI_BAR_OK);
  gfx->setTextColor(UI_WHITE); gfx->setTextSize(2); gfx->setCursor(CX-12,356); gfx->print("OK");
  gfx->flush();
}

void renderBoxBackgroundSettings() {
  clockDirty=false; gfx->fillScreen(uiBg());
  drawSettingsTitle(displayBoxLabel());
  gfx->fillRoundRect(72,82,322,270,18,uiPanel2());
  gfx->drawRoundRect(72,82,322,270,18,uiLine());
  drawBoxBackgroundAsset(boxBgPreview,83,104,2);
  gfx->fillRoundRect(82,188,52,52,14,uiPanel());
  gfx->fillRoundRect(332,188,52,52,14,uiPanel());
  gfx->setTextColor(uiInk()); gfx->setTextSize(3);
  gfx->setCursor(98,199); gfx->print("<"); gfx->setCursor(348,199); gfx->print(">");
  char count[12]; snprintf(count,sizeof(count),"%u/16",(unsigned)(boxBgPreview+1));
  gfx->fillRoundRect(180,314,106,28,9,uiPanel());
  gfx->setTextColor(uiInk()); gfx->setTextSize(1);
  gfx->setCursor(CX-(int)strlen(count)*3,324); gfx->print(count);
  gfx->fillRoundRect(128,374,210,46,14,UI_BAR_OK);
  gfx->setTextColor(UI_WHITE); gfx->setTextSize(2);
  const char *valid=T(S_VALIDATE); gfx->setCursor(CX-(int)strlen(valid)*6,388); gfx->print(valid);
  gfx->flush();
}

void drawSettingsTitle(const char *title) {
  gfx->setTextColor(uiInk()); gfx->setTextSize(3);
  gfx->setCursor(CX-(int)strlen(title)*9,38); gfx->print(title);
}

void renderTimeSettings() {
  clockDirty=false; gfx->fillScreen(uiBg());
  uint16_t timeInk=darkMode ? UI_WHITE : UI_INK;
  gfx->setTextColor(timeInk); gfx->setTextSize(3);
  gfx->setCursor(CX-(int)strlen(T(S_SET_TIME))*9,62); gfx->print(T(S_SET_TIME));
  char t[8]; snprintf(t,sizeof(t),"%02d:%02d",clockH,clockM);
  gfx->setTextColor(timeInk); gfx->setTextSize(5);
  gfx->setCursor(CX-75,120); gfx->print(t);
  gfx->setTextColor(timeInk); gfx->setTextSize(1);
  gfx->setCursor(116,196); gfx->print(T(S_HOUR));
  gfx->setCursor(290,196); gfx->print(T(S_MIN));
  drawClockBtn(90,216,"-"); drawClockBtn(158,216,"+");
  drawClockBtn(248,216,"-"); drawClockBtn(316,216,"+");
  gfx->fillRoundRect(108,342,250,50,14,UI_BAR_OK);
  gfx->setTextColor(UI_WHITE); gfx->setTextSize(2);
  gfx->setCursor(CX-(int)strlen(T(S_VALIDATE))*6,358); gfx->print(T(S_VALIDATE));
  gfx->flush();
}

void renderSoundSettings() {
  clockDirty=false; gfx->fillScreen(uiBg());
  drawSettingsTitle(T(S_SOUND_LABEL));
  static const StrId modes[4]={S_SND_FULL,S_SND_MED,S_SND_LOW,S_SND_OFF};
  static const uint8_t values[4]={SOUND_FULL,SOUND_MED,SOUND_LOW,SOUND_OFF};
  for(uint8_t i=0;i<4;i++) {
    bool active=audioMode()==values[i];
    drawSettingsRow(64,92+i*62,338,48,T(modes[i]),active ? T(S_ACTIVE) : "",active);
  }
  gfx->fillRoundRect(128,358,210,46,14,UI_BAR_OK);
  gfx->setTextColor(UI_WHITE); gfx->setTextSize(2);
  gfx->setCursor(CX-12,372); gfx->print("OK");
  gfx->flush();
}

void renderResetSettings() {
  clockDirty=false; gfx->fillScreen(uiBg());
  drawSettingsTitle(T(S_RESET));
  gfx->fillRoundRect(58,104,350,156,18,darkMode ? C565(0x42,0x0d,0x14) : C565(0xff,0xe6,0xe8));
  gfx->drawRoundRect(58,104,350,156,18,C565(0xff,0x35,0x45));
  gfx->setTextColor(darkMode ? UI_WHITE : C565(0x75,0x08,0x12));
  const char *warning=T(S_RESET_WARNING);
  uint8_t warnScale=strlen(warning)>28 ? 1 : 2;
  gfx->setTextSize(warnScale);
  gfx->setCursor(CX-(int)strlen(warning)*3*warnScale,136); gfx->print(warning);
  gfx->setTextSize(1); gfx->setTextColor(darkMode ? UI_WHITE : uiInk());
  gfx->setCursor(196,184); gfx->print("microSD OK");
  gfx->fillRoundRect(72,300,322,54,15,C565(0xd8,0x18,0x2b));
  gfx->setTextColor(UI_WHITE); gfx->setTextSize(2);
  const char *confirm=T(S_RESET_CONFIRM);
  gfx->setCursor(CX-(int)strlen(confirm)*6,318); gfx->print(confirm);
  gfx->fillRoundRect(128,374,210,44,14,uiPanel());
  gfx->drawRoundRect(128,374,210,44,14,uiLine());
  gfx->setTextColor(uiInk());
  gfx->setCursor(CX-(int)strlen(T(S_LAN_CANCEL))*6,388); gfx->print(T(S_LAN_CANCEL));
  gfx->flush();
}

void renderClock() {
  if (settingsPage==1) { renderDisplaySettings(); return; }
  if (settingsPage==2) { renderSoundSettings(); return; }
  if (settingsPage==3) { renderTimeSettings(); return; }
  if (settingsPage==4) { renderResetSettings(); return; }
  if (settingsPage==5) { renderFrameSettings(); return; }
  if (settingsPage==6) { renderBoxBackgroundSettings(); return; }

  clockDirty=false;
  gfx->fillScreen(uiBg());

  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  const char *title=T(S_SETTINGS);
  gfx->setCursor(CX-(int)strlen(title)*9,34);
  gfx->print(title);

  // Heure compacte.
  drawSettingsRow(58,88,350,50,T(S_SET_TIME),"",false);

  // Paramètres principaux sous forme de cartes propres.
  drawSettingsRow(58,148,350,50,T(S_DISPLAY_LABEL),"",false);
  drawSettingsRow(58,208,350,50,T(S_SOUND_LABEL),"",false);
  drawSettingsRow(58,268,350,50,T(S_RESET),"",false);

  gfx->fillRoundRect(58,342,154,46,14,uiPanel());
  gfx->drawRoundRect(58,342,154,46,14,uiLine());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  const char *hw=HELP_WORD[gLang];
  gfx->setCursor(58+(154-(int)strlen(hw)*12)/2,356);
  gfx->print(hw);

  gfx->fillRoundRect(226,342,182,46,14,UI_BAR_OK);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  gfx->setCursor(299,352);
  gfx->print("OK");
  gfx->flush();
}

void clockTap(int16_t x,int16_t y) {
  if (settingsPage==2) {
    if (y>=350 && y<=420) {
      settingsPage=0; clockDirty=true; sfxPlay(SFX_TAP); lockTouchBrief(); return;
    }
    if (x>=54 && x<=412 && y>=82 && y<=342) {
      uint8_t row=(uint8_t)((y-82)/62);
      static const uint8_t values[4]={SOUND_FULL,SOUND_MED,SOUND_LOW,SOUND_OFF};
      if(row<4) { audioSetMode(values[row]); clockDirty=true; if(audioEnabled()) sfxPlay(SFX_LEVEL); return; }
    }
    return;
  }
  if (settingsPage==3) {
    if (y>=204 && y<=286) {
      if (x>=80 && x<154) clockH=(clockH+23)%24;
      else if (x>=154 && x<232) clockH=(clockH+1)%24;
      else if (x>=238 && x<312) clockM=(clockM+59)%60;
      else if (x>=312 && x<386) clockM=(clockM+1)%60;
      clockDirty=true; return;
    }
    if (x>=96 && x<=370 && y>=330 && y<=404) { applyClock(); return; }
    return;
  }
  if (settingsPage==4) {
    if (x>=116 && x<=350 && y>=364 && y<=430) {
      settingsPage=0; clockDirty=true; sfxPlay(SFX_TAP); lockTouchBrief(); return;
    }
    if (x>=60 && x<=406 && y>=286 && y<=366) {
      pet.factoryReset(); delay(120); ESP.restart(); return;
    }
    return;
  }
  if (settingsPage==6) {
    if (x>=112 && x<=354 && y>=362 && y<=432) {
      pet.setBoxBackground(boxBgPreview); settingsPage=1; clockDirty=true;
      sfxPlay(SFX_TAP); lockTouchBrief(); return;
    }
    if (y>=168 && y<=260) {
      if (x>=68 && x<=154) boxBgPreview=boxBgPreview==0?BOX_BG_COUNT-1:boxBgPreview-1;
      else if (x>=312 && x<=398) boxBgPreview=(uint8_t)((boxBgPreview+1)%BOX_BG_COUNT);
      else return;
      pet.boxBackground=boxBgPreview; // application immédiate, sans redémarrage
      cardDirty=true;
      clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
    }
    return;
  }
  if (settingsPage==5) {
    if (x>=118 && x<=348 && y>=388 && y<=448) {
      settingsPage=1; clockDirty=true; sfxPlay(SFX_TAP); lockTouchBrief(); return;
    }
    uint8_t count=pet.unlockedCollectionFrameCount();
    if (y>=204 && y<=282 && count>0) {
      if (x>=68 && x<=150) {
        uint8_t next=pet.collectionFrame==0 ? count-1 : pet.collectionFrame-1;
        pet.setCollectionFrame(next); clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
      }
      if (x>=316 && x<=398) {
        uint8_t next=(uint8_t)((pet.collectionFrame+1)%count);
        pet.setCollectionFrame(next); clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
      }
    }
    return;
  }
  if (settingsPage==1) {
    if (x>=118 && x<=348 && y>=330 && y<=410) {
      settingsPage=0; clockDirty=true; sfxPlay(SFX_TAP); lockTouchBrief(); return;
    }
    if (x>=54 && x<=412 && y>=72 && y<=140) {
      setDarkMode(!darkMode); clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
    }
    if (x>=54 && x<=412 && y>=138 && y<=200) {
      settingsPage=5; clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
    }
    if (x>=54 && x<=412 && y>=200 && y<=262) {
      boxBgPreview=pet.boxBackground; settingsPage=6; clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
    }
    if (y>=260 && y<=326) {
      if (x>=54 && x<=232) setLang((Lang)((gLang+1)%LANG_COUNT));
      else if (x>=232 && x<=412) setPowerSave(!powerSave);
      clockDirty=true; sfxPlay(SFX_MENU); lockTouchBrief(); return;
    }
    return;
  }

  if (x>=48 && x<=418 && y>=78 && y<=142) { settingsPage=3; clockDirty=true; sfxPlay(SFX_MENU); return; }
  if (x>=48 && x<=418 && y>=142 && y<=202) { settingsPage=1; clockDirty=true; sfxPlay(SFX_MENU); return; }
  if (x>=48 && x<=418 && y>=202 && y<=262) { settingsPage=2; clockDirty=true; sfxPlay(SFX_MENU); return; }
  if (x>=48 && x<=418 && y>=262 && y<=326) { settingsPage=4; clockDirty=true; sfxPlay(SFX_MENU); return; }
  if (x>=48 && x<=220 && y>=332 && y<=400) { openHelp(); return; }
  if (x>=220 && x<=420 && y>=332 && y<=400) { clockOpen=false; settingsPage=0; markUiDirty(); lockTouchBrief(); return; }
}

// llama + numero de racha arriba a la izquierda
void drawStreakBadge() {
  if (pet.streak < 1) return;
  int x = 26, y = 16;
  gfx->fillTriangle(x + 8, y, x + 1, y + 17, x + 15, y + 17, UI_BAR_BAD);
  gfx->fillTriangle(x + 8, y + 7, x + 4, y + 17, x + 12, y + 17, UI_BAR_WARN);
  char s[6];
  snprintf(s, sizeof(s), "%u", pet.streak);
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x + 22, y + 2);
  gfx->print(s);
}

// banner temporal: medalla nueva o hito de racha
void drawCelebration() {
  const char *l1 = nullptr, *l2 = nullptr;
  char buf[20];
  if (pet.showMedal()) {
    for (int i = 0; i < MED_COUNT; i++)
      if (pet.newMedal & (1 << i)) { l2 = medalName(i); break; }
    l1 = T(S_MEDAL_BANNER);
  } else if (pet.showDexReward()) {
    l1 = T(S_NEW_FRAME);
    l2 = T((StrId)(S_RANK_TRAINER + pet.collectionRank()));
  } else if (pet.showMilestone()) {
    snprintf(buf, sizeof(buf), T(S_STREAK_DAYS_FMT), pet.streak);
    l1 = T(S_GREAT);
    l2 = buf;
  }
  if (!l1) return;
  gfx->fillRoundRect(73, 150, 320, 96, 16, UI_BAR_WARN);
  gfx->drawRoundRect(73, 150, 320, 96, 16, uiInk());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(l1) * 9, 176);
  gfx->print(l1);
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(l2) * 6, 212);
  gfx->print(l2);
}

uint16_t collectionFrameColor(uint8_t frame) {
  static const uint16_t COLORS[] = {
    UI_TRACK,
    C565(0x8c,0x65,0xd9), // classique violet
    C565(0xff,0x1e,0x2d), // pokeball rouge vif
    C565(0xf1,0xc2,0x45), // or
    C565(0xc4,0xcc,0xda), // argent
    C565(0x47,0xdc,0xff), // neon cyan
  };
  return COLORS[frame < 6 ? frame : 0];
}

void drawFrameMiniBall(int cx,int cy,int r) {
  uint16_t red   = C565(0xff,0x1e,0x2d);
  uint16_t black = C565(0x0b,0x0c,0x10);
  uint16_t white = C565(0xf8,0xf8,0xf6);
  uint16_t grey  = C565(0xd4,0xd8,0xe0);

  // Disque rouge complet.
  gfx->fillCircle(cx,cy,r,red);

  // Demi-disque blanc réellement contenu dans le cercle.
  // Pour chaque ligne Y du bas, on calcule la demi-largeur du cercle.
  for (int dy=0; dy<=r; dy++) {
    int yy = cy + dy;
    int xx = (int)sqrtf((float)(r*r - dy*dy));
    gfx->drawFastHLine(cx-xx, yy, xx*2+1, white);
  }

  // Ceinture noire parfaitement contenue entre les bords du cercle.
  int band = max(3, r/5);
  for (int dy=-band/2; dy<=band/2; dy++) {
    int yy = cy + dy;
    int rr = r*r - dy*dy;
    if (rr < 0) continue;
    int xx = (int)sqrtf((float)rr);
    gfx->drawFastHLine(cx-xx, yy, xx*2+1, black);
  }

  // Bouton central.
  int outer = max(4, r/3);
  int mid   = max(3, outer-2);
  int inner = max(1, r/7);
  gfx->fillCircle(cx,cy,outer,black);
  gfx->fillCircle(cx,cy,mid,white);
  gfx->fillCircle(cx,cy,inner,grey);

  // Contour final.
  gfx->drawCircle(cx,cy,r,black);
}

void drawCleanPokeball(int cx,int cy,int r) {
  uint16_t red   = C565(0xff,0x1e,0x2d);
  uint16_t black = C565(0x0b,0x0c,0x10);
  uint16_t white = C565(0xf8,0xf8,0xf6);
  uint16_t grey  = C565(0xd4,0xd8,0xe0);

  gfx->fillCircle(cx,cy,r,red);

  // Bas blanc sans aucun débordement hors du cercle.
  for (int dy=0; dy<=r; dy++) {
    int yy = cy + dy;
    int xx = (int)sqrtf((float)(r*r - dy*dy));
    gfx->drawFastHLine(cx-xx, yy, xx*2+1, white);
  }

  // Ceinture centrale noire contenue.
  int band = max(5, r/7);
  for (int dy=-band/2; dy<=band/2; dy++) {
    int yy = cy + dy;
    int rr = r*r - dy*dy;
    if (rr < 0) continue;
    int xx = (int)sqrtf((float)rr);
    gfx->drawFastHLine(cx-xx, yy, xx*2+1, black);
  }

  // Bouton central en trois niveaux.
  int outer = max(8, r/4);
  gfx->fillCircle(cx,cy,outer,black);
  gfx->fillCircle(cx,cy,max(5,outer-3),white);
  gfx->fillCircle(cx,cy,max(2,r/10),grey);

  // Contour externe noir.
  gfx->drawCircle(cx,cy,r,black);
  gfx->drawCircle(cx,cy,r-1,black);
}


void drawFrameSpark(int x,int y,uint16_t c) {
  gfx->drawFastHLine(x-4,y,9,c);
  gfx->drawFastVLine(x,y-4,9,c);
  gfx->fillCircle(x,y,1,UI_WHITE);
}

// Anneau segmenté : visuellement plus riche que le simple cercle violet,
// tout en restant extrêmement léger pour l'ESP32.
void drawSegmentedFrameRing(int cx,int cy,int radius,uint16_t c1,uint16_t c2) {
  const int SEG=24;
  for (int i=0;i<SEG;i++) {
    float a0=(2.0f*PI*i)/SEG;
    float a1=(2.0f*PI*(i+0.62f))/SEG;
    int x0=cx+(int)(cosf(a0)*radius);
    int y0=cy+(int)(sinf(a0)*radius);
    int x1=cx+(int)(cosf(a1)*radius);
    int y1=cy+(int)(sinf(a1)*radius);
    gfx->drawLine(x0,y0,x1,y1,(i&1)?c2:c1);
  }
}

// Cadres conçus pour la vraie zone du Pokémon visible sur l'écran rond.
void drawCollectionFrame(int cx,int cy,int radius,uint8_t frame) {
  if (frame==0) return;

  if (frame==1) { // CLASSIQUE : segments violet/bleu + points lumineux
    uint16_t violet=C565(0x8c,0x65,0xd9);
    uint16_t blue=C565(0x5d,0x8f,0xff);
    drawSegmentedFrameRing(cx,cy,radius,violet,blue);
    gfx->drawCircle(cx,cy,radius-6,lerp565(violet,uiBg(),2,5));
    for (int i=0;i<4;i++) {
      float a=i*(PI/2.0f)+PI/4.0f;
      int px=cx+(int)(cosf(a)*radius);
      int py=cy+(int)(sinf(a)*radius);
      gfx->fillCircle(px,py,4,violet);
      gfx->fillCircle(px,py,2,UI_WHITE);
    }
  } else if (frame==2) { // POKEBALL : rouge / blanc / noir propre
    uint16_t red   = C565(0xff,0x1e,0x2d);
    uint16_t white = C565(0xf8,0xf8,0xf6);
    uint16_t black = C565(0x0b,0x0c,0x10);

    // Anneau rouge principal avec segments blancs contrôlés.
    drawSegmentedFrameRing(cx,cy,radius,red,white);
    gfx->drawCircle(cx,cy,radius-4,black);
    gfx->drawCircle(cx,cy,radius-8,red);

    // 4 Poké Balls propres autour de l'anneau.
    drawFrameMiniBall(cx,cy-radius,8);
    drawFrameMiniBall(cx+radius,cy,8);
    drawFrameMiniBall(cx,cy+radius,8);
    drawFrameMiniBall(cx-radius,cy,8);
  } else if (frame==3) { // OR
    uint16_t gold=C565(0xf1,0xc2,0x45);
    uint16_t amber=C565(0xc7,0x80,0x18);
    drawSegmentedFrameRing(cx,cy,radius,gold,amber);
    gfx->drawCircle(cx,cy,radius-6,amber);
    for (int i=0;i<8;i++) {
      float a=i*(PI/4.0f);
      drawFrameSpark(cx+(int)(cosf(a)*radius),
                     cy+(int)(sinf(a)*radius),gold);
    }
  } else if (frame==4) { // ARGENT
    uint16_t silver=C565(0xc4,0xcc,0xda);
    uint16_t steel=C565(0x68,0x76,0x8d);
    drawSegmentedFrameRing(cx,cy,radius,silver,steel);
    gfx->drawCircle(cx,cy,radius-5,steel);
    gfx->drawCircle(cx,cy,radius-9,silver);
    for (int i=0;i<4;i++) {
      float a=i*(PI/2.0f)+PI/4.0f;
      int px=cx+(int)(cosf(a)*(radius-2));
      int py=cy+(int)(sinf(a)*(radius-2));
      gfx->fillCircle(px,py,4,steel);
      gfx->fillCircle(px,py,2,UI_WHITE);
    }
  } else { // NEON
    uint16_t cyan=C565(0x47,0xdc,0xff);
    uint16_t purple=C565(0xb2,0x63,0xff);
    drawSegmentedFrameRing(cx,cy,radius+2,cyan,purple);
    gfx->drawCircle(cx,cy,radius-3,cyan);
    gfx->drawCircle(cx,cy,radius-7,purple);
    for (int i=0;i<8;i++) {
      float a=i*(PI/4.0f);
      gfx->fillCircle(cx+(int)(cosf(a)*(radius+2)),
                      cy+(int)(sinf(a)*(radius+2)),2,(i&1)?purple:cyan);
    }
  }
}

// medallas en la ficha: badge con etiqueta, color si conseguida
void drawMedalBadge(int x, int y, int i) {
  bool got = pet.hasMedal(1 << i);
  gfx->fillRoundRect(x, y, 100, 24, 6, got ? UI_BAR_OK : UI_TRACK);
  if (!got) gfx->drawRoundRect(x, y, 100, 24, 6, UI_TRACK);
  gfx->setTextColor(got ? UI_BG_DAY : 0x9492);
  gfx->setTextSize(2);
  gfx->setCursor(x + (100 - (int)strlen(medalLabel(i)) * 12) / 2, y + 5);
  gfx->print(medalLabel(i));
}

// pagina 0: perfil (retrato grande, identidad, racha, vinculo, baya)
void renderCardProfile() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  const char *nm = pet.nick[0] ? pet.nick : dexName(pet.speciesId);
  char head[26];
  snprintf(head, sizeof(head), T(S_NAME_FMT), pet.shiny ? "*" : "", nm, pet.level());
  gfx->setTextColor(d.accent);
  // auto-encoge: a tamano 3 los nombres largos no caben en la franja estrecha de
  // arriba de la pantalla redonda, asi que se cortaban por el borde
  int hlen = strlen(head);
  int hts = (hlen <= 11) ? 3 : 2;
  gfx->setTextSize(hts);
  gfx->setCursor(CX - hlen * (hts == 3 ? 9 : 6), hts == 3 ? 34 : 40);
  gfx->print(head);
  if (pet.nick[0]) {  // especie real bajo el apodo
    gfx->setTextColor(uiSub());
    gfx->setTextSize(2);
    const char *speciesName = dexName(pet.speciesId);
    gfx->setCursor(CX - (strlen(speciesName) + 2) * 6, 64);
    gfx->printf("(%s)", speciesName);
  }

  // retrato grande animado mit dem aktuell ausgewaehlten Sammlerrahmen
  // Le cadre entoure la silhouette, pas le point d'ancrage situe aux pieds.
  drawCollectionFrame(CX, 146, 98, pet.collectionFrame);
  if (pmd.loaded) drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);

  // racha con llama
  int sx = 138, sy = 224;
  gfx->fillTriangle(sx + 8, sy, sx + 1, sy + 18, sx + 15, sy + 18, UI_BAR_BAD);
  gfx->fillTriangle(sx + 8, sy + 7, sx + 4, sy + 18, sx + 12, sy + 18, UI_BAR_WARN);
  char rl[30];
  snprintf(rl, sizeof(rl), T(S_STREAK_FMT), pet.streak, pet.bestStreak);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(sx + 24, sy + 2);
  gfx->print(rl);

  drawCardStat(258, T(S_VIN), pet.bond, 100, C565(0xd4, 0x52, 0x7e));

  const char *berry = !pet.berryKnown ? T(S_BERRY_UNK)
                      : pet.lovesBerry(0) ? T(S_BERRY_RED)
                      : pet.lovesBerry(1) ? T(S_BERRY_BLUE)
                                          : T(S_BERRY_GREEN);
  char info[40];
  snprintf(info, sizeof(info), T(S_INFO_FMT), berry,
           (unsigned long)(pet.ageMinutes / 1440));
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(info) * 6, 296);
  gfx->print(info);

  gfx->setTextColor(uiSub());
  gfx->setCursor(CX - strlen(T(S_RENAME_HINT)) * 6, 326);
  gfx->print(T(S_RENAME_HINT));


}

StrId personalityNameId(PetPersonality p) {
  switch (p) {
    case PERS_PLAYFUL: return S_PERS_PLAYFUL;
    case PERS_BRAVE: return S_PERS_BRAVE;
    case PERS_CALM: return S_PERS_CALM;
    case PERS_LAZY: return S_PERS_LAZY;
    default: return S_PERS_BALANCED;
  }
}

StrId personalityHintId(PetPersonality p) {
  switch (p) {
    case PERS_PLAYFUL: return S_PERS_PLAYFUL_HINT;
    case PERS_BRAVE: return S_PERS_BRAVE_HINT;
    case PERS_CALM: return S_PERS_CALM_HINT;
    case PERS_LAZY: return S_PERS_LAZY_HINT;
    default: return S_PERS_BALANCED_HINT;
  }
}

uint16_t personalityColor(PetPersonality p) {
  switch (p) {
    case PERS_PLAYFUL: return UI_BAR_WARN;
    case PERS_BRAVE: return UI_BAR_BAD;
    case PERS_CALM: return 0x4C98;
    case PERS_LAZY: return 0xB3C8;
    default: return UI_BAR_OK;
  }
}

static constexpr uint8_t REC_BALL   = 0;
static constexpr uint8_t REC_CATCH  = 1;
static constexpr uint8_t REC_MEMO   = 2;
static constexpr uint8_t REC_CLEAN  = 3;
static constexpr uint8_t REC_TYPE   = 4;
static constexpr uint8_t REC_BATTLE = 5;

// Petits sprites pixel-art 16x16 intégrés au firmware.
// '.' = transparent, K=noir, R=rouge, W=blanc, B=bleu,
// Y=jaune, G=vert, P=violet, O=orange, S=gris.
static const char *const REC_SPRITES[6][16] = {
  { // Poké Ball
    ".....KKKKKK.....",
    "...KKRRRRRRKK...",
    "..KRRRRRRRRRRK..",
    ".KRRRRRRRRRRRRK.",
    ".KRRRRRRRRRRRRK.",
    "KRRRRRRRRRRRRRRK",
    "KRRRRRKKKKRRRRRK",
    "KKKKKKKWWKKKKKKK",
    "KWWWWWKWWKWWWWWK",
    "KWWWWWWKKWWWWWWK",
    "KWWWWWWWWWWWWWWK",
    ".KWWWWWWWWWWWWK.",
    ".KWWWWWWWWWWWWK.",
    "..KWWWWWWWWWWK..",
    "...KKWWWWWWKK...",
    ".....KKKKKK....."
  },
  { // Capture / cible
    "......YYYY......",
    "....YYKKKKYY....",
    "...YKKWWWWKKY...",
    "..YKWBBBBBBWKY..",
    ".YKWBBYYYYBBWKY.",
    ".YKWBYYKKYYBWKY.",
    "YKWBYYKWWKYYBWKY",
    "YKWBYKWRRWKYBWKY",
    "YKWBYKWRRWKYBWKY",
    "YKWBYYKWWKYYBWKY",
    ".YKWBYYKKYYBWKY.",
    ".YKWBBYYYYBBWKY.",
    "..YKWBBBBBBWKY..",
    "...YKKWWWWKKY...",
    "....YYKKKKYY....",
    "......YYYY......"
  },
  { // Mémo / carnet
    "...BBBBBBBBBB...",
    "..BKKKKKKKKKKB..",
    "..BWWWWWWWWWWB..",
    "..BWBKKKKKKKWB..",
    "..BWWWWWWWWWWB..",
    "..BWBKKKKKKKWB..",
    "..BWWWWWWWWWWB..",
    "..BWBKKKKKKKWB..",
    "..BWWWWWWWWWWB..",
    "..BWBKKKKKKKWB..",
    "..BWWWWWWWWWWB..",
    "..BWBKKKKKKKWB..",
    "..BWWWWWWWWWWB..",
    "..BKKKKKKKKKKB..",
    "...BBBBBBBBBB...",
    "................"
  },
  { // Nettoyage / étincelles
    ".......Y........",
    ".......Y........",
    ".....YYYYY......",
    ".......Y........",
    ".......Y.....W..",
    "..W..........W..",
    "..W........WWW..",
    "WWWWW.........W.",
    "..W.............",
    "..W....Y........",
    ".......Y........",
    ".....YYYYY......",
    ".......Y........",
    ".......Y........",
    "................",
    "................"
  },
  { // Type / bouclier
    "....PPPPPPPP....",
    "...PKKKKKKKKP...",
    "..PKBBBBBBBBKP..",
    "..PKBBBBBBBBKP..",
    "..PKBBBWWBBBK...",
    "..PKBBBWWBBBK...",
    "..PKBBBWWBBBK...",
    "...KBBBWWBBBK...",
    "...KBBBWWBBBK...",
    "...KBBBWWBBK....",
    "....KBBWWBBK....",
    "....KBBWWBK.....",
    ".....KBWWK......",
    "......KWWK......",
    ".......KK.......",
    "................"
  },
  { // Combat / épées croisées
    "KK..........KK..",
    ".KK........KK...",
    "..KK......KK....",
    "...KK....KK.....",
    "....KK..KK......",
    ".....KKKK.......",
    "......KK........",
    ".....KKKK.......",
    "....KK..KK......",
    "...KK....KK.....",
    "..KK......KK....",
    ".OK........KO...",
    "OO..........OO..",
    ".O..........O...",
    "................",
    "................"
  }
};

uint16_t recordSpriteColor(char c) {
  switch (c) {
    case 'K': return UI_INK;
    case 'R': return UI_BAR_BAD;
    case 'W': return UI_WHITE;
    case 'B': return C565(0x42, 0x86, 0xd8);
    case 'Y': return UI_BAR_WARN;
    case 'G': return UI_BAR_OK;
    case 'P': return C565(0xa8, 0x55, 0xc9);
    case 'O': return C565(0xe0, 0x87, 0x2a);
    case 'S': return UI_TRACK;
    default:  return UI_WHITE;
  }
}

// Dessin par segments horizontaux : bien moins d'appels graphiques qu'un pixel
// après l'autre, et rendu net "sprite" à l'échelle 1.
void drawRecordSprite(int x, int y, uint8_t icon, uint8_t scale = 1) {
  if (icon > REC_BATTLE || scale == 0) return;
  for (int row = 0; row < 16; row++) {
    const char *line = REC_SPRITES[icon][row];
    int col = 0;
    while (col < 16) {
      char c = line[col];
      if (c == '.') { col++; continue; }
      int run = 1;
      while (col + run < 16 && line[col + run] == c) run++;
      gfx->fillRect(x + col * scale, y + row * scale, run * scale, scale, recordSpriteColor(c));
      col += run;
    }
  }
}

void drawPersonalityRecord(int x,int y,const char *label,uint16_t val,uint16_t color,uint8_t icon) {
  const int w=160,h=48;
  gfx->fillRoundRect(x,y,w,h,8,uiPanel());
  gfx->drawRoundRect(x,y,w,h,8,color);

  // Fond clair derrière le sprite : aspect "badge" propre.
  gfx->fillRoundRect(x+8,y+12,24,24,6,lerp565(color, uiPanel(), 1, 5));
  drawRecordSprite(x+12,y+16,icon);

  gfx->setTextColor(color);
  gfx->setTextSize(1);
  gfx->setCursor(x+40,y+7);
  gfx->print(label);

  char num[8];
  snprintf(num,sizeof(num),"%u",val);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(x+40,y+23);
  gfx->print(num);
}

// Page Caractere : uniquement la personnalite et ses deux indicateurs.
void renderCardPersonality() {
  PetPersonality pers=pet.personality(); const char *title=T(S_PERSONALITY); const char *name=T(personalityNameId(pers)); const char *hint=T(personalityHintId(pers)); uint16_t col=personalityColor(pers);
  gfx->setTextColor(uiInk()); gfx->setTextSize(3); gfx->setCursor(CX-strlen(title)*9,38); gfx->print(title);
  gfx->fillRoundRect(62,78,342,70,16,col); gfx->setTextColor(uiContrastText(col)); int nts=(strlen(name)<=10)?3:2; gfx->setTextSize(nts); gfx->setCursor(CX-strlen(name)*(nts==3?9:6),nts==3?96:103); gfx->print(name); gfx->setTextSize(2); gfx->setCursor(CX-strlen(hint)*6,128); gfx->print(hint);
  drawCardStat(180,T(S_VIN),pet.bond,100,C565(0xd4,0x52,0x7e));
  drawCardStat(226,T(S_BAR_JOY),pet.joy,100,UI_BAR_WARN);
}

// Page dediee aux records : six cases larges dans la zone sure du cercle.
void renderCardRecords() {
  const char *title=T(S_RECORDS);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX-(int)strlen(title)*9,44);
  gfx->print(title);
  drawPersonalityRecord(68,94,T(S_GAME_BALL),pet.gameHi,UI_BAR_OK,REC_BALL);
  drawPersonalityRecord(238,94,T(S_GAME_CATCH),pet.catchHi,UI_BAR_WARN,REC_CATCH);
  drawPersonalityRecord(68,154,T(S_GAME_MEMO),pet.memoHi,0x4C98,REC_MEMO);
  drawPersonalityRecord(238,154,T(S_GAME_CLEAN),pet.cleanHi,UI_BAR_OK,REC_CLEAN);
  drawPersonalityRecord(68,214,T(S_GAME_TYPE),pet.typeHi,0xF3B7,REC_TYPE);
  drawPersonalityRecord(238,214,T(S_BATTLE),pet.bestBattleStreak,UI_BAR_BAD,REC_BATTLE);
}

StrId dailyGoalLabelId(uint8_t goalType) {
  switch (goalType) {
    case DAILY_GOAL_CARE: return S_GOAL_CARE;
    case DAILY_GOAL_PLAY: return S_GOAL_PLAY;
    case DAILY_GOAL_BATTLE: return S_GOAL_BATTLE;
    case DAILY_GOAL_CATCH: return S_GOAL_CATCH;
    case DAILY_GOAL_MEMO: return S_GOAL_MEMO;
    default: return S_GOAL_CARE;
  }
}

uint16_t dailyGoalColor(uint8_t goalType) {
  switch (goalType) {
    case DAILY_GOAL_CARE: return C565(0xd4, 0x52, 0x7e);
    case DAILY_GOAL_PLAY: return UI_BAR_WARN;
    case DAILY_GOAL_BATTLE: return UI_BAR_BAD;
    case DAILY_GOAL_CATCH: return UI_BAR_OK;
    case DAILY_GOAL_MEMO: return 0x4C98;
    default: return UI_INK;
  }
}

uint16_t dailyTextColor() {
  return darkMode ? UI_WHITE : UI_INK;
}

void drawDailyGoalRow(int y, uint8_t idx) {
  uint8_t type = pet.dailyGoalType[idx];
  uint8_t target = pet.dailyGoalTarget(type);
  uint8_t progress = pet.dailyGoalProgress[idx] > target ? target : pet.dailyGoalProgress[idx];
  bool done = pet.dailyGoalComplete(idx);
  uint16_t col = dailyGoalColor(type);
  gfx->fillRoundRect(58, y, 350, 52, 12, done ? col : uiPanel());
  gfx->drawRoundRect(58, y, 350, 52, 12, col);
  gfx->setTextSize(2);
  gfx->setTextColor(dailyTextColor());
  gfx->setCursor(82, y + 18);
  gfx->print(T(dailyGoalLabelId(type)));

  char prog[12];
  snprintf(prog, sizeof(prog), "%u/%u", progress, target);
  gfx->setCursor(286, y + 18);
  gfx->print(done ? T(S_DONE) : prog);
  if (done) {
    gfx->fillCircle(374, y + 26, 12, UI_BG_DAY);
    gfx->setTextColor(dailyTextColor());
    gfx->setCursor(368, y + 18);
    gfx->print("v");
  }
}

// pagina 2: objetivos diarios
void renderCardDaily() {
  pet.ensureDailyGoals();
  gfx->setTextColor(dailyTextColor());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_DAILY)) * 9, 44);
  gfx->print(T(S_DAILY));
  const char *phase = T(dayPhaseTextId(currentDayPhase()));
  gfx->setTextColor(dailyTextColor());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(phase) * 6, 74);
  gfx->print(phase);

  for (uint8_t i = 0; i < DAILY_GOAL_COUNT; i++) {
    drawDailyGoalRow(104 + i * 70, i);
  }
}

#define BOX_ROWS 8

bool boxComesBefore(int16_t a, int16_t b) {
  if (boxSort == 1) {
    const DexEntry &da = DEX_TBL[a];
    const DexEntry &db = DEX_TBL[b];
    if (da.type1 != db.type1) return da.type1 < db.type1;
    if (da.type2 != db.type2) return da.type2 < db.type2;
  } else if (boxSort == 2) {
    bool ra = pet.isRegistered(a);
    bool rb = pet.isRegistered(b);
    if (ra != rb) return ra;
  }
  return a < b;
}

uint16_t boxBuildList(int16_t *out) {
  uint16_t n = 0;
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++)
    if (pet.isCaught(dex)) out[n++] = dex;
  for (uint16_t i = 1; i < n; i++) {
    int16_t v = out[i];
    int j = i - 1;
    while (j >= 0 && boxComesBefore(v, out[j])) {
      out[j + 1] = out[j];
      j--;
    }
    out[j + 1] = v;
  }
  return n;
}

uint8_t boxPageCount() {
  uint16_t count = pet.caughtCount();
  uint8_t pages = (count + BOX_ROWS - 1) / BOX_ROWS;
  return pages > 0 ? pages : 1;
}

int16_t boxDexAt(uint16_t index) {
  int16_t list[DEX_COUNT];
  uint16_t n = boxBuildList(list);
  return index < n ? list[index] : 0;
}

const char *boxSortLabel() {
  if (boxSort == 1) return T(S_SORT_TYPE);
  if (boxSort == 2) return T(S_SORT_RAISED);
  return T(S_SORT_DEX);
}

void renderCardBox() {
  uint8_t pages = boxPageCount();
  if (boxPage >= pages) boxPage = pages - 1;

  gfx->fillRoundRect(108,24,250,72,15,uiPanel());
  gfx->drawRoundRect(108,24,250,72,15,uiLine());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_BOX)) * 9, 36);
  gfx->print(T(S_BOX));

  char caught[24];
  snprintf(caught, sizeof(caught), T(S_CAUGHT_COUNT_FMT), pet.caughtCount());
  gfx->setTextSize(2);
  gfx->setTextColor(uiInk());
  gfx->setCursor(CX-(int)strlen(caught)*6, 76);
  gfx->print(caught);

  if (pet.caughtCount() == 0) {
    gfx->fillRoundRect(82, 178, 302, 72, 16, uiPanel());
    gfx->drawRoundRect(82, 178, 302, 72, 16, UI_TRACK);
    gfx->setTextColor(uiSub());
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_NO_CATCHES)) * 6, 207);
    gfx->print(T(S_NO_CATCHES));
    return;
  }

  // Le décor personnalisé de la Boîte s'arrête à la séparation. La navigation
  // générale reprend ensuite le fond standard, clair ou sombre selon le thème.
  gfx->fillRect(0,355,466,111,uiBg());
  gfx->fillRect(0,350,466,5,UI_INK);

  // Grille 4x2 de mini-sprites captures, adaptee au cercle 1,75 pouce.
  for (uint8_t i = 0; i < BOX_ROWS; i++) {
    int16_t dex = boxDexAt((uint16_t)boxPage * BOX_ROWS + i);
    if (dex <= 0) break;
    const DexEntry &d = DEX_TBL[dex];
    int col = i % 4, row = i / 4;
    int x = 84 + col * 78, y = 112 + row * 74;
    gfx->fillRoundRect(x, y, 64, 64, 10, uiPanel());
    gfx->drawRoundRect(x, y, 64, 64, 10, d.accent);
    const uint8_t *thumb = thumbs.get(dex);
    if (thumb) drawStarterThumbCentered(thumb, dex, x + 32, y + 31, 2);
    if (pet.isShinyRegistered(dex)) {
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(1);
      gfx->setCursor(x + 51, y + 5);
      gfx->print("*");
    }
  }
  uint16_t prevBg = boxPage > 0 ? UI_TRACK : C565(0xe4, 0xe8, 0xee);
  uint16_t nextBg = boxPage + 1 < pages ? UI_TRACK : C565(0xe4, 0xe8, 0xee);
  gfx->fillRoundRect(76, 306, 94, 38, 11, prevBg);
  gfx->fillRoundRect(296, 306, 94, 38, 11, nextBg);
  gfx->setTextSize(3);
  gfx->setTextColor(uiContrastText(prevBg));
  gfx->setCursor(111, 315);
  gfx->print("<");
  gfx->setTextColor(uiContrastText(nextBg));
  gfx->setCursor(331, 315);
  gfx->print(">");
  char pg[12];
  snprintf(pg, sizeof(pg), T(S_PAGE_FMT), boxPage + 1, pages);
  gfx->setTextColor(uiSub());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(pg) * 6, 318);
  gfx->print(pg);
}

// pagina 3: combate (4 barras + botones)
void renderCardStats() {
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_BATTLE)) * 9, 48);
  gfx->print(T(S_BATTLE));

  drawCardStat(102, T(S_STAT_ATK), pet.atkStat(), 260, UI_BAR_BAD);
  drawCardStat(142, T(S_STAT_DEF), pet.defStat(), 260, 0x4C98);
  drawCardStat(182, T(S_STAT_SPE), pet.speStat(), 260, UI_BAR_WARN);

  char wl[20], bs[18], bb[16];
  snprintf(wl, sizeof(wl), T(S_WL_FMT), pet.battleWins, pet.battleLosses);
  snprintf(bs, sizeof(bs), T(S_BSTREAK_FMT), pet.battleStreak);
  snprintf(bb, sizeof(bb), T(S_BBEST_FMT), pet.bestBattleStreak);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(74, 226);
  gfx->print(wl);
  gfx->setCursor(210, 226);
  gfx->print(bs);
  gfx->setCursor(334, 226);
  gfx->print(bb);

  gfx->fillRoundRect(96, 264, 274, 40, 11, 0x4C98);
  gfx->setTextColor(uiContrastText(0x4C98));
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_WILD_BATTLE)) * 6, 277);
  gfx->print(T(S_WILD_BATTLE));

  // boton: saco de entrenamiento de fuerza
  gfx->fillRoundRect(96, 312, 274, 40, 11, UI_BAR_BAD);
  gfx->setTextColor(uiContrastText(UI_BAR_BAD));
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_TRAIN_STR)) * 6, 325);
  gfx->print(T(S_TRAIN_STR));
}

// pagina 2: medallas con etiqueta descriptiva
void renderCardMedals() {
  int got = 0;
  for (int i = 0; i < MED_COUNT; i++)
    if (pet.hasMedal(1 << i)) got++;
  char head[20];
  snprintf(head, sizeof(head), T(S_MEDALS_FMT), got, MED_COUNT);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(head) * 9, 48);
  gfx->print(head);

  for (int i = 0; i < MED_COUNT; i++) {
    int x = 42 + (i % 2) * 196, y = 104 + (i / 2) * 54;
    bool g = pet.hasMedal(1 << i);
    gfx->fillRoundRect(x, y, 186, 44, 10, g ? UI_BAR_OK : UI_TRACK);
    gfx->setTextColor(g ? UI_BG_DAY : 0x8410);
    gfx->setTextSize(2);
    const char *desc = medalDesc(i);
    gfx->setCursor(x + (186 - (int)strlen(desc) * 12) / 2, y + 14);
    gfx->print(desc);
  }
}

// pagina 3: progreso (nivel, evolucion, descuidos) — saca a la luz mecanicas
// que antes eran invisibles (cuanto falta para subir/evolucionar y por que)
void renderCardProgress() {
  const DexEntry &d = DEX_TBL[pet.speciesId];
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_PROGRESS)) * 9, 44);
  gfx->print(T(S_PROGRESS));

  // nivel grande
  char lv[10];
  snprintf(lv, sizeof(lv), T(S_LVL_FMT), pet.level());
  gfx->setTextSize(5);
  gfx->setCursor(CX - strlen(lv) * 15, 86);
  gfx->print(lv);

  // barra de progreso al siguiente nivel (1 nivel = 60 min de juego)
  bool maxLevel = pet.level() >= MAX_LEVEL;
  uint8_t into = maxLevel ? MINUTES_PER_LEVEL : pet.ageMinutes % MINUTES_PER_LEVEL;
  int bx = 93, bw = 280, by = 158, bh = 22;
  gfx->fillRoundRect(bx, by, bw, bh, 6, UI_TRACK);
  int fw = (bw - 4) * into / MINUTES_PER_LEVEL;
  if (fw > 0) gfx->fillRoundRect(bx + 2, by + 2, fw, bh - 4, 5, UI_BAR_OK);
  char nx[26];
  if (maxLevel) snprintf(nx, sizeof(nx), "MAX");
  else snprintf(nx, sizeof(nx), T(S_NEXT_LVL_FMT), MINUTES_PER_LEVEL - into, pet.level() + 1);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(nx) * 6, by + 32);
  gfx->print(nx);

  // estado de evolucion
  gfx->setTextColor(uiSub());
  gfx->setCursor(CX - strlen(T(S_EVO_LABEL)) * 6, 230);
  gfx->print(T(S_EVO_LABEL));
  char evoBuf[28];
  const char *evo;
  uint16_t evoCol = darkMode ? UI_WHITE : UI_INK;
  if (d.evolvesTo == 0) {
    evo = T(S_FINAL_FORM);
  } else if (d.evolveLevel == 0) {
    // Pierre / échange / bonheur / beauté : aucun faux niveau.
    evo = T(S_SPECIAL_EVOLUTION);
    evoCol = UI_BAR_WARN;
  } else {
    int needed = d.evolveLevel;
    if (pet.level() >= needed) {
      evo = T(S_EVO_READY);
      evoCol = UI_BAR_OK;
    } else {
      snprintf(evoBuf, sizeof(evoBuf), T(S_EVO_IN_FMT), needed - pet.level());
      evo = evoBuf;
    }
  }
  gfx->setTextColor(evoCol);
  gfx->setCursor(CX - strlen(evo) * 6, 256);
  gfx->print(evo);

}

StrId expeditionItemText(ExpeditionItem item) {
  switch (item) {
    case EXP_ITEM_SNACK: return S_ITEM_SNACK;
    case EXP_ITEM_ENERGY: return S_ITEM_ENERGY;
    case EXP_ITEM_CARE: return S_ITEM_CARE;
    case EXP_ITEM_TRAIN: return S_ITEM_TRAIN;
    default: return S_WAIT;
  }
}

uint16_t expeditionItemColor(ExpeditionItem item) {
  switch (item) {
    case EXP_ITEM_SNACK: return UI_BAR_WARN;
    case EXP_ITEM_ENERGY: return 0x4C98;
    case EXP_ITEM_CARE: return UI_BAR_OK;
    case EXP_ITEM_TRAIN: return UI_BAR_BAD;
    default: return UI_TRACK;
  }
}

uint32_t expeditionNowEpoch() {
  uint32_t nowEpoch = rtcEpoch();
  return nowEpoch ? nowEpoch : pet.lastSeenEpoch;
}

void drawExpeditionItem(int x, int y, ExpeditionItem item) {
  const int w = 172, h = 54;
  uint8_t count = pet.itemCounts[item];
  uint16_t col = expeditionItemColor(item);
  uint16_t cardBg = count ? uiPanel() : C565(0xe4, 0xe8, 0xee);
  uint16_t itemInk = uiContrastText(cardBg);
  gfx->fillRoundRect(x, y, w, h, 9, cardBg);
  gfx->drawRoundRect(x, y, w, h, 9, count ? col : UI_TRACK);
  gfx->fillCircle(x + 22, y + 27, 12, count ? col : UI_TRACK);
  if (item == EXP_ITEM_ENERGY) gfx->fillRect(x + 20, y + 17, 5, 20, UI_WHITE);
  else if (item == EXP_ITEM_CARE) gfx->fillCircle(x + 22, y + 22, 4, UI_WHITE);
  else if (item == EXP_ITEM_TRAIN) gfx->fillRect(x + 16, y + 25, 12, 4, UI_WHITE);

  const char *label = T(expeditionItemText(item));
  gfx->setTextSize(1);
  gfx->setTextColor(itemInk);
  gfx->setCursor(x + 42, y + 16);
  gfx->print(label);
  char amount[6];
  snprintf(amount, sizeof(amount), "x%u", count);
  gfx->setTextSize(2);
  gfx->setCursor(x + 136, y + 29);
  gfx->print(amount);
}

void renderExpeditionTrainChoice() {
  gfx->fillRoundRect(58, 118, 350, 190, 14, uiPanel());
  gfx->drawRoundRect(58, 118, 350, 190, 14, uiInk());
  const char *title = T(S_ITEM_TRAIN);
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(title) * 6, 136);
  gfx->print(title);

  const StrId labels[3] = { S_TRAIN_ATK, S_TRAIN_DEF, S_TRAIN_SPE };
  const uint8_t values[3] = { pet.trAtk, pet.trDef, pet.trSpe };
  const uint16_t cols[3] = { UI_BAR_BAD, 0x4C98, UI_BAR_WARN };
  for (uint8_t i = 0; i < 3; i++) {
    int x = 74 + i * 108;
    bool usable = values[i] < 100;
    uint16_t trainBg = usable ? cols[i] : UI_TRACK;
    gfx->fillRoundRect(x, 172, 102, 66, 9, trainBg);
    gfx->setTextColor(uiContrastText(trainBg));
    gfx->setTextSize(2);
    const char *label = T(labels[i]);
    gfx->setCursor(x + (102 - (int)strlen(label) * 12) / 2, 184);
    gfx->print(label);
    if (usable) {
      gfx->setTextSize(1);
      gfx->setCursor(x + 37, 210);
      gfx->print("+2");
    } else {
      const char *maxed = T(S_ITEM_MAXED);
      gfx->setTextSize(1);
      gfx->setCursor(x + (102 - (int)strlen(maxed) * 6) / 2, 210);
      gfx->print(maxed);
    }
  }
  gfx->setTextColor(uiSub());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_BACK)) * 6, 268);
  gfx->print(T(S_BACK));
}

void renderCardExpedition() {
  uint32_t nowEpoch = expeditionNowEpoch();
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - strlen(T(S_EXPEDITION)) * 9, 42);
  gfx->print(T(S_EXPEDITION));

  if (pet.expeditionReady(nowEpoch)) {
    char found[34];
    snprintf(found, sizeof(found), T(S_FOUND_ITEM_FMT), T(expeditionItemText((ExpeditionItem)pet.expeditionRewardItem)));
    gfx->setTextColor(UI_BAR_OK);
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(found) * 6, 78);
    gfx->print(found);
    gfx->fillRoundRect(98, 98, 270, 48, 11, UI_BAR_OK);
    gfx->setTextColor(uiContrastText(UI_BAR_OK));
    gfx->setTextSize(2);
    gfx->setCursor(CX - strlen(T(S_EXP_CLAIM)) * 6, 111);
    gfx->print(T(S_EXP_CLAIM));
  } else if (pet.expeditionActive(nowEpoch)) {
    uint32_t left = (pet.expeditionEndEpoch - nowEpoch + 59UL) / 60UL;
    char back[28];
    snprintf(back, sizeof(back), T(S_EXP_IN_FMT), (unsigned)left);
    gfx->setTextColor(darkMode ? UI_WHITE : 0x4C98);
    gfx->setTextSize(3);
    gfx->setCursor(CX - strlen(back) * 9, 86);
    gfx->print(back);
    gfx->setTextColor(uiSub());
    gfx->setTextSize(1);
    gfx->setCursor(CX - 90, 116);
    gfx->print(T(S_WAIT));
  } else {
    const uint8_t minutes[3] = { 15, 30, 60 };
    const StrId labels[3] = { S_EXP_15, S_EXP_30, S_EXP_60 };
    const int xs[3] = { 50, 180, 310 };
    for (uint8_t i = 0; i < 3; i++) {
      uint8_t cost = Pet::expeditionEnergyCost(minutes[i]);
      bool available = pet.canStartExpedition(minutes[i], nowEpoch);
      uint16_t col = i == 0 ? UI_BAR_OK : i == 1 ? 0x4C98 : UI_BAR_BAD;
      uint16_t expBg = available ? col : UI_TRACK;
      gfx->fillRoundRect(xs[i], 94, 106, 54, 9, expBg);
      gfx->setTextColor(uiContrastText(expBg));
      gfx->setTextSize(2);
      const char *label = T(labels[i]);
      gfx->setCursor(xs[i] + (106 - (int)strlen(label) * 12) / 2, 104);
      gfx->print(label);
      char costText[12];
      snprintf(costText, sizeof(costText), "-%u ENE", cost);
      gfx->setTextSize(1);
      gfx->setCursor(xs[i] + (106 - (int)strlen(costText) * 6) / 2, 130);
      gfx->print(costText);
    }
    if (pet.expeditionInventoryFull()) {
      gfx->setTextColor(UI_BAR_BAD);
      gfx->setTextSize(1);
      gfx->setCursor(CX - strlen(T(S_INV_FULL)) * 3, 76);
      gfx->print(T(S_INV_FULL));
    } else if (pet.energy < 12) {
      char need[24];
      snprintf(need, sizeof(need), T(S_NEED_ENE_FMT), 12);
      gfx->setTextColor(UI_BAR_BAD);
      gfx->setTextSize(1);
      gfx->setCursor(CX - strlen(need) * 3, 76);
      gfx->print(need);
    }
  }

  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_INVENTORY)) * 6, 174);
  gfx->print(T(S_INVENTORY));
  drawExpeditionItem(50, 190, EXP_ITEM_SNACK);
  drawExpeditionItem(244, 190, EXP_ITEM_ENERGY);
  drawExpeditionItem(50, 254, EXP_ITEM_CARE);
  drawExpeditionItem(244, 254, EXP_ITEM_TRAIN);
  if (expeditionTrainChoiceOpen) renderExpeditionTrainChoice();
}

int8_t expeditionItemAt(int16_t x, int16_t y) {
  if ((y < 190 || y > 244) && (y < 254 || y > 308)) return -1;
  bool left = x >= 50 && x <= 222;
  bool right = x >= 244 && x <= 416;
  if (!left && !right) return -1;
  uint8_t row = y >= 254 ? 1 : 0;
  return row * 2 + (right ? 1 : 0);
}

void expeditionCardTap(int16_t x, int16_t y) {
  if (expeditionTrainChoiceOpen) {
    if (y >= 172 && y <= 238 && x >= 74 && x <= 398) {
      int8_t stat = (x - 74) / 108;
      if (stat > TRAIN_STAT_SPE || !pet.useExpeditionItem(EXP_ITEM_TRAIN, stat)) sfxPlay(SFX_DENY);
      else sfxPlay(SFX_ITEM_USE);
      expeditionTrainChoiceOpen = false;
      cardDirty = true;
      lockTouchBrief();
      return;
    }
    expeditionTrainChoiceOpen = false;
    cardDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  if (y >= 396) {
    cardOpen = false;
    markUiDirty();
    lockTouchBrief();
    sfxPlay(SFX_TAP);
    return;
  }

  uint32_t nowEpoch = expeditionNowEpoch();
  if (pet.expeditionReady(nowEpoch)) {
    if (x >= 98 && x <= 368 && y >= 98 && y <= 146) {
      ExpeditionItem item = pet.claimExpedition(nowEpoch);
      if (item != EXP_ITEM_NONE) sfxPlay(SFX_EXPEDITION_CLAIM);
      else sfxPlay(SFX_DENY);
      cardDirty = true;
      lockTouchBrief();
    }
    return;
  }
  if (!pet.expeditionActive(nowEpoch) && y >= 94 && y <= 148) {
    const uint8_t minutes[3] = { 15, 30, 60 };
    int idx = x >= 50 && x <= 156 ? 0 : x >= 180 && x <= 286 ? 1 : x >= 310 && x <= 416 ? 2 : -1;
    if (idx >= 0 && pet.startExpedition(minutes[idx], nowEpoch, (uint8_t)random(100), (uint8_t)random(3))) {
      sfxPlay(SFX_EXPEDITION_START);
      cardDirty = true;
      lockTouchBrief();
    } else {
      sfxPlay(SFX_DENY);
    }
    return;
  }

  int8_t item = expeditionItemAt(x, y);
  if (item < 0) return;
  ExpeditionItem selected = (ExpeditionItem)item;
  if (pet.itemCounts[selected] == 0) {
    sfxPlay(SFX_DENY);
    return;
  }
  if (selected == EXP_ITEM_TRAIN) {
    expeditionTrainChoiceOpen = true;
    cardDirty = true;
    sfxPlay(SFX_MENU);
  } else if (pet.useExpeditionItem(selected)) {
    cardDirty = true;
    sfxPlay(SFX_ITEM_USE);
    lockTouchBrief();
  }
}

void renderCard() {
  cardDirty = false;
  gfx->fillScreen(uiBg());
  // Mode cover : 600x432, centré et rogné par l'écran rond. Le décor déborde
  // volontairement à gauche, à droite et en haut pour ne laisser aucune marge.
  if (cardPage == 3) drawBoxBackgroundAsset(pet.boxBackground,-67,-20,4);
  if (cardPage == 0) renderCardProfile();
  else if (cardPage == 1) renderCardPersonality();
  else if (cardPage == 2) renderCardDaily();
  else if (cardPage == 3) renderCardBox();
  else if (cardPage == 4) renderCardStats();
  else if (cardPage == 5) renderCardMedals();
  else if (cardPage == 6) renderCardProgress();
  else if (cardPage == 7) renderCardExpedition();
  else renderCardRecords();

  // Pages remontées + vrai bouton RETOUR.
  int navY = 360;
  uint16_t navInk = cardPage == 2 ? dailyTextColor() : uiInk();
  uint16_t navSub = cardPage == 2 ? dailyTextColor() : uiSub();
  uint16_t prevCol = cardPage > 0 ? navInk : navSub;
  uint16_t nextCol = cardPage + 1 < CARD_COUNT ? navInk : navSub;
  gfx->fillRoundRect(76, navY, 56, 42, 12, uiPanel());
  gfx->drawRoundRect(76, navY, 56, 42, 12, prevCol);
  gfx->fillRoundRect(334, navY, 56, 42, 12, uiPanel());
  gfx->drawRoundRect(334, navY, 56, 42, 12, nextCol);
  gfx->setTextSize(3);
  gfx->setTextColor(prevCol);
  gfx->setCursor(96, navY + 11); gfx->print("<");
  gfx->setTextColor(nextCol);
  gfx->setCursor(354, navY + 11); gfx->print(">");

  gfx->fillRoundRect(136, navY, 194, 42, 12, uiPanel());
  gfx->drawRoundRect(136, navY, 194, 42, 12, uiLine());
  gfx->setTextColor(navInk);
  gfx->setTextSize(2);
  const char *cardBack=T(S_LAN_BACK);
  gfx->setCursor(CX-(int)strlen(cardBack)*6,navY + 14);
  gfx->print(cardBack);
  gfx->flush();
}

// ---------- teclado para renombrar ----------

static const char KB_KEYS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ.-";  // 28 + DEL + OK = 30
#define KB_COLS 6
#define KB_X 40
#define KB_Y 150
#define KB_W 64
#define KB_H 52

void openKeyboard() {
  kbOpen = true;
  keyboardDirty = true;
  lockTouchBrief();
  strncpy(nameBuf, pet.nick, sizeof(nameBuf) - 1);
  nameBuf[sizeof(nameBuf) - 1] = 0;
  nameLen = strlen(nameBuf);
  sfxPlay(SFX_MENU);
}

void renderKeyboard() {
  keyboardDirty = false;
  gfx->fillScreen(uiBg());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(T(S_NAME)) * 6, 56);
  gfx->print(T(S_NAME));
  // buffer actual
  gfx->fillRoundRect(83, 84, 300, 40, 8, uiPanel());
  gfx->drawRoundRect(83, 84, 300, 40, 8, uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(95, 94);
  gfx->print(nameLen ? nameBuf : "_");

  for (int i = 0; i < 30; i++) {
    int x = KB_X + (i % KB_COLS) * KB_W, y = KB_Y + (i / KB_COLS) * KB_H;
    bool special = (i >= 28);
    gfx->fillRoundRect(x, y, KB_W - 6, KB_H - 6, 6, special ? UI_BAR_WARN : uiPanel());
    gfx->drawRoundRect(x, y, KB_W - 6, KB_H - 6, 6, uiInk());
    gfx->setTextColor(uiInk());
    gfx->setTextSize(2);
    if (i < 28) {
      gfx->setCursor(x + KB_W / 2 - 9, y + KB_H / 2 - 10);
      gfx->print(KB_KEYS[i]);
    } else {
      const char *lab = (i == 28) ? "<-" : "OK";
      gfx->setCursor(x + KB_W / 2 - 15, y + KB_H / 2 - 10);
      gfx->print(lab);
    }
  }
  gfx->flush();
}

void keyboardTap(int16_t x, int16_t y) {
  int col = (x - KB_X) / KB_W, row = (y - KB_Y) / KB_H;
  if (col < 0 || col >= KB_COLS || row < 0 || row >= 5) return;
  int i = row * KB_COLS + col;
  if (i >= 30) return;
  sfxPlay(SFX_TAP);
  if (i == 28) {  // borrar
    if (nameLen) nameBuf[--nameLen] = 0;
    keyboardDirty = true;
  } else if (i == 29) {  // OK
    pet.rename(nameBuf);
    kbOpen = false;
    cardDirty = true;
    lockTouchBrief();
  } else if (nameLen < sizeof(nameBuf) - 1) {
    nameBuf[nameLen++] = KB_KEYS[i];
    nameBuf[nameLen] = 0;
    keyboardDirty = true;
  }
}

// ---------- galeria pokedex ----------

#define GAL_X 89
#define GAL_Y 94
#define GAL_CELL 72

bool galleryDexVisible(int16_t dex) {
  if (dex < 1 || dex > DEX_COUNT) return false;
  if (galleryFilter == 1) return pet.isRegistered(dex);
  if (galleryFilter == 2) return pet.isCaught(dex);
  return true;
}

uint16_t galleryFilteredCount() {
  if (galleryFilter == 0) return DEX_COUNT;
  uint16_t count = 0;
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++)
    if (galleryDexVisible(dex)) count++;
  return count;
}

int galleryPageCount() {
  uint16_t count = galleryFilteredCount();
  int pages = (count + 15) / 16;
  return pages > 0 ? pages : 1;
}

int16_t galleryDexAt(uint16_t index) {
  for (int16_t dex = 1; dex <= DEX_COUNT; dex++) {
    if (!galleryDexVisible(dex)) continue;
    if (index == 0) return dex;
    index--;
  }
  return 0;
}

// dibuja una miniatura centrada en su celda; sil=true la pinta en tinta
void drawThumb(const uint8_t *b, int x, int y, int s, bool sil) {
  uint8_t w = b[0], h = b[1], n = b[2];
  const uint8_t *pal = b + 3;
  const uint8_t *d = pal + n * 2;
  int minX=w, minY=h, maxX=-1, maxY=-1;
  for (int r=0;r<h;r++) for (int c=0;c<w;c++) {
    if (d[r*w+c] == 0xFF) continue;
    minX=min(minX,c); minY=min(minY,r); maxX=max(maxX,c); maxY=max(maxY,r);
  }
  if (maxX < minX || maxY < minY) return;
  int visibleW=maxX-minX+1, visibleH=maxY-minY+1;
  int ox = x + (GAL_CELL - visibleW * s) / 2 - minX*s;
  int oy = y + (GAL_CELL - visibleH * s) / 2 - minY*s;
  for (int r = 0; r < h; r++) {
    for (int c = 0; c < w; c++) {
      uint8_t idx = d[r * w + c];
      if (idx == 0xFF) continue;
      uint16_t col = sil ? INK_K : (uint16_t)(pal[idx * 2] | (pal[idx * 2 + 1] << 8));
      gfx->fillRect(ox + c * s, oy + r * s, s, s, col);
    }
  }
}

void renderGallery() {
  if (galleryDetail) {  // vista detalle: se redibuja siempre (animada)
    gfx->fillScreen(uiBg());
    const DexEntry &d = DEX_TBL[galleryDetail];
    bool reg = pet.isRegistered(galleryDetail);
    bool caught = pet.isCaught(galleryDetail);
    bool known = reg || caught;
    char head[24];
    snprintf(head, sizeof(head), "N.%03u %s%s", displayedDexNumber(galleryDetail),
             pet.isShinyRegistered(galleryDetail) ? "*" : "", known ? dexName(galleryDetail) : "???");
    gfx->setTextColor(known ? d.accent : UI_INK);
    int glen = strlen(head);
    int gts = (glen <= 13) ? 3 : 2;  // auto-encoge nombres largos (no caben a t3)
    gfx->setTextSize(gts);
    gfx->setCursor(CX - glen * (gts == 3 ? 9 : 6), gts == 3 ? 56 : 60);
    gfx->print(head);
    if (known) {
      char types[24];
      typeText(types, sizeof(types), d);
      gfx->setTextColor(battleTypeColor(d.type1));
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(types) * 6, 94);
      gfx->print(types);
    }
    if (galleryPmd.loaded) {
      // animado y a color si se conoce; silueta estatica si no (estilo "?")
      drawPmdActM(galleryPmd, PMD_IDLE, CX, 300, known ? millis() : 0, true, !known, 6, galleryDetail);
    } else {
      const uint8_t *t = thumbs.get(galleryDetail);
      if (t) drawThumb(t, CX - GAL_CELL, 135, 4, !known);
    }
    if (reg) {
      const char *mark = T(S_RAISED_MARK);
      gfx->setTextColor(UI_BAR_OK);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(mark) * 6, caught ? 354 : 366);
      gfx->print(mark);
    }
    if (caught) {
      const char *mark = T(S_CAUGHT_MARK);
      gfx->setTextColor(UI_BAR_WARN);
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(mark) * 6, reg ? 376 : 366);
      gfx->print(mark);
    }
    // Bouton RETOUR vers la grille Pokédex.
    gfx->fillRoundRect(22, 20, 54, 34, 10, uiPanel());
    gfx->drawRoundRect(22, 20, 54, 34, 10, uiInk());
    gfx->setTextColor(uiInk());
    gfx->setTextSize(2);
    gfx->setCursor(42, 28);
    gfx->print("<");

    // Pokémon capturé : bouton pour le choisir comme compagnon actif.
    if (caught || reg) {
      bool active = (galleryDetail == pet.speciesId);
      const char *care = active ? T(S_ACTIVE) : T(S_CARE_ACTION);
      uint16_t bc = active ? UI_TRACK : UI_BAR_OK;
      gfx->fillRoundRect(128, 386, 210, 42, 13, bc);
      gfx->setTextColor(UI_WHITE);
      gfx->setTextSize(2);
      gfx->setCursor(CX - (int)strlen(care) * 6, 400);
      gfx->print(care);
    } else {
      gfx->setTextColor(uiInk());
      gfx->setTextSize(2);
      gfx->setCursor(CX - strlen(T(S_DETAIL_BACK)) * 6, 408);
      gfx->print(T(S_DETAIL_BACK));
    }
    gfx->flush();
    return;
  }

  if (!galleryDirty) return;  // la rejilla es estatica
  galleryDirty = false;

  gfx->fillScreen(uiBg());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(3);
  gfx->setCursor(CX - 7 * 9, 28);
  gfx->print("POKEDEX");

  char head[28];
  snprintf(head, sizeof(head), "R:%u C:%u", pet.registeredCount(), pet.caughtCount());
  gfx->setTextSize(2);
  gfx->setCursor(CX - strlen(head) * 6, 54);
  gfx->print(head);

  const char *filters[3] = { T(S_FILTER_ALL), T(S_RAISED_MARK), T(S_CAUGHT_MARK) };
  for (int i = 0; i < 3; i++) {
    int fx = 74 + i * 106;
    uint16_t fill = (galleryFilter == i) ? UI_INK : UI_WHITE;
    uint16_t text = (galleryFilter == i) ? UI_BG_DAY : UI_INK;
    gfx->fillRoundRect(fx, 74, 96, 18, 6, fill);
    gfx->drawRoundRect(fx, 74, 96, 18, 6, uiInk());
    gfx->setTextColor(text);
    gfx->setTextSize(1);
    gfx->setCursor(fx + (96 - (int)strlen(filters[i]) * 6) / 2, 80);
    gfx->print(filters[i]);
  }

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      int16_t dex = galleryDexAt(galleryPage * 16 + r * 4 + c);
      if (dex <= 0) continue;
      int x = GAL_X + c * GAL_CELL, y = GAL_Y + r * GAL_CELL;
      const uint8_t *t = thumbs.get(dex);
      if (t) {
        bool reg = pet.isRegistered(dex);
        bool caught = pet.isCaught(dex);
        bool known = reg || caught;
        drawThumb(t, x, y, 2, !known);
        if (pet.isShinyRegistered(dex)) {
          gfx->setTextColor(UI_BAR_WARN);
          gfx->setTextSize(2);
          gfx->setCursor(x + 62, y + 4);
          gfx->print("*");
        } else if (caught && !reg) {
          gfx->setTextColor(UI_BAR_WARN);
          gfx->setTextSize(1);
          gfx->setCursor(x + 60, y + 6);
          gfx->print("C");
        }
      } else {
        char num[6];
        snprintf(num, sizeof(num), "%d", dex);
        gfx->setTextColor(uiSub());
        gfx->setTextSize(2);
        gfx->setCursor(x + 24, y + 32);
        gfx->print(num);
      }
    }
  }
  // Navigation latérale : aucune rangée de petites billes.
  int pages = galleryPageCount();
  if (galleryPage > 0) {
    gfx->fillRoundRect(18, 214, 48, 52, 12, uiPanel());
    gfx->drawRoundRect(18, 214, 48, 52, 12, uiLine());
    gfx->setTextColor(uiInk());
    gfx->setTextSize(3);
    gfx->setCursor(34, 228);
    gfx->print("<");
  }
  if (galleryPage + 1 < pages) {
    gfx->fillRoundRect(400, 214, 48, 52, 12, uiPanel());
    gfx->drawRoundRect(400, 214, 48, 52, 12, uiLine());
    gfx->setTextColor(uiInk());
    gfx->setTextSize(3);
    gfx->setCursor(416, 228);
    gfx->print(">");
  }

  // Retour remonté pour rester dans la zone utile du rond 1,75".
  gfx->fillRoundRect(136, 398, 194, 40, 12, uiPanel());
  gfx->drawRoundRect(136, 398, 194, 40, 12, uiLine());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  const char *dexBack=T(S_LAN_BACK);
  gfx->setCursor(CX-(int)strlen(dexBack)*6,411);
  gfx->print(dexBack);

  gfx->flush();
}

void galleryTap(int16_t x, int16_t y) {
  if (galleryDetail) {
    // RETOUR explicite vers la grille.
    if (x >= 12 && x <= 88 && y >= 10 && y <= 66) {
      galleryDetail = 0;
      galleryPmd.unload();
      galleryDirty = true;
      lockTouchBrief();
      sfxPlay(SFX_TAP);
      return;
    }

    // Le bouton S'OCCUPER est disponible pour les Pokémon capturés OU déjà élevés.
    if ((pet.isCaught(galleryDetail) || pet.isRegistered(galleryDetail)) && y >= 378 && y <= 438 && x >= 112 && x <= 354) {
      if (galleryDetail == pet.speciesId) {
        sfxPlay(SFX_TAP);
        return;
      }
      if (pet.switchToCaught(galleryDetail)) {
        galleryOpen = false;
        galleryDetail = 0;
        galleryPmd.unload();
        sdDirty = true;
        markUiDirty();
        lockTouchBrief();
        sfxPlay(SFX_CATCH_OK);
      } else {
        sfxPlay(SFX_DENY);
      }
      return;
    }
    // Toucher ailleurs revient à la grille.
    galleryDetail = 0;
    galleryPmd.unload();
    galleryDirty = true;
    sfxPlay(SFX_TAP);
    return;
  }
  // RETOUR de la grille Pokédex : grande zone tactile en bas.
  if (x >= 120 && x <= 346 && y >= 390 && y <= 448) {
    galleryOpen=false;
    galleryPmd.unload();
    markUiDirty();
    lockTouchBrief();
    sfxPlay(SFX_TAP);
    return;
  }

  // Flèches latérales centrées dans le rond.
  if (y >= 202 && y <= 278) {
    int pages = galleryPageCount();
    if (x <= 76 && galleryPage > 0) {
      galleryPage--;
      galleryDirty=true;
      sfxPlay(SFX_MENU);
      lockTouchBrief();
      return;
    }
    if (x >= 390 && galleryPage + 1 < pages) {
      galleryPage++;
      galleryDirty=true;
      sfxPlay(SFX_MENU);
      lockTouchBrief();
      return;
    }
  }

  if (y >= 68 && y < GAL_Y) {
    int f = (x - 74) / 106;
    if (f >= 0 && f < 3 && x >= 74 + f * 106 && x <= 170 + f * 106) {
      galleryFilter = (uint8_t)f;
      galleryPage = 0;
      galleryDirty = true;
      sfxPlay(SFX_TAP);
      return;
    }
  }
  if (x < GAL_X || y < GAL_Y) return;
  int c = (x - GAL_X) / GAL_CELL, r = (y - GAL_Y) / GAL_CELL;
  if (c < 0 || c > 3 || r < 0 || r > 3) return;
  int16_t dex = galleryDexAt(galleryPage * 16 + r * 4 + c);
  if (dex <= 0) return;
  galleryDetail = dex;
  galleryPmd.load(dex, pet.isShinyRegistered(dex));
  sfxPlay(SFX_MENU);
  if (pet.isRegistered(dex) || pet.isCaught(dex)) speciesChirpPlay(dex);
}

void drawBattery() {
  int pc = batPercent();
  if (pc < 0) return;  // sin bateria conectada
  int x = CX - 14, y = 12, w = 24, h = 11;
  bool charging = batCharging();
  uint16_t col = charging ? UI_BAR_OK
                 : (pc >= 40) ? inkColor()
                 : (pc >= 15) ? UI_BAR_WARN
                              : UI_BAR_BAD;
  gfx->drawRoundRect(x, y, w, h, 2, col);
  gfx->fillRect(x + w, y + 3, 3, 5, col);  // borne
  if (charging) {
    // rayo de carga (zigzag) en vez de la barra de nivel
    uint16_t bolt = C565(0xff, 0xd9, 0x4a);
    int bx = x + w / 2;
    gfx->fillTriangle(bx + 3, y + 1, bx - 4, y + 6, bx + 1, y + 6, bolt);
    gfx->fillTriangle(bx - 1, y + 5, bx + 4, y + 5, bx - 3, y + 10, bolt);
  } else {
    int fw = (w - 4) * pc / 100;
    if (fw > 0) gfx->fillRect(x + 2, y + 2, fw, h - 4, col);
  }
}

void drawHeader(const char *name, uint16_t nameColor, const char *msg) {
  drawBattery();

  // Nom seul en haut, centré. Réduction automatique pour les noms longs.
  int nlen = (int)strlen(name);
  uint8_t ns = (nlen <= 12) ? 3 : 2;
  gfx->setTextColor(nameColor);
  gfx->setTextSize(ns);
  gfx->setCursor(CX - nlen * (ns == 3 ? 9 : 6), 40);
  gfx->print(name);

  // État simple du Pokémon, sans commentaire ambigu sur le poids.
  if (msg && msg[0]) {
    int mlen=(int)strlen(msg);
    uint8_t ms = mlen <= 18 ? 2 : 1;
    gfx->setTextColor(inkColor());
    gfx->setTextSize(ms);
    gfx->setCursor(CX - mlen * (ms == 2 ? 6 : 3), 94);
    gfx->print(msg);
  }
}

void drawHomeIdentity(const char *name, uint8_t level, uint16_t nameColor, const char *msg) {
  drawBattery();
  int nlen=(int)strlen(name);
  uint8_t ns=(nlen<=12)?3:2;
  gfx->setTextColor(nameColor);
  gfx->setTextSize(ns);
  gfx->setCursor(CX-nlen*(ns==3?9:6),36);
  gfx->print(name);

  char lv[16];
  snprintf(lv,sizeof(lv),T(S_LVL_FMT),(unsigned)level);
  gfx->setTextColor(C565(0xff,0xd7,0x48));
  gfx->setTextSize(2);
  gfx->setCursor(CX-(int)strlen(lv)*6,70);
  gfx->print(lv);

  if (msg && msg[0]) {
    int mlen=(int)strlen(msg);
    gfx->setTextColor(inkColor());
    gfx->setTextSize(1);
    gfx->setCursor(CX-mlen*3,100);
    gfx->print(msg);
  }
}

// animacion de la ceremonia (10s): despedida = reverencia con corazones y se
// aleja caminando; escapada = se asusta y sale corriendo. Sustituye al idle.
void drawCeremony() {
  if (!pmd.loaded) { drawPet(); return; }  // respaldo si no hay sprite PMD
  uint32_t now = millis();
  float t = pet.ceremonyT();               // 0..1 a lo largo de los 10s
  bool panic = (pet.ceremony == CER_RUNAWAY);
  int x = CX, y = PET_GROUND;
  uint8_t act = PMD_IDLE;

  if (panic) {
    // final triste: penumbra azulada + lluvia
    for (int i = 0; i < 46; i++) {
      int rx = (i * 47 + now / 3) % 466;
      int ry = (i * 91 + now / 2) % 470;
      gfx->drawLine(rx, ry, rx - 3, ry + 12, C565(0x6a, 0x84, 0xb0));
    }
    bool fade = false;
    if (t < 0.30f) {                       // cabizbajo, temblando
      act = pmd.has(PMD_HURT) ? PMD_HURT : PMD_IDLE;
      x = CX + (int)(4 * sinf(now * 0.04f));
    } else {                               // se aleja despacio y se desvanece
      act = pmd.has(PMD_WALKL) ? PMD_WALKL : PMD_IDLE;
      x = CX - (int)(((t - 0.30f) / 0.70f) * (CX + 120));
      fade = (t > 0.6f) && ((now / 160) % 2 == 0);  // parpadea hacia la silueta
    }
    drawPmdAct(act, x, y, now, true, fade, 5);  // fade=silueta: se difumina al irse
    // lagrima cayendo del bicho
    if (t < 0.55f) {
      int ty = y - 150 + (int)((now / 6) % 40);
      gfx->fillRect(x + 6, ty, 3, 6, C565(0x9a, 0xc4, 0xe8));
    }
    return;
  }

  // despedida epica: halo dorado pulsante + chispas y corazones que ascienden
  int gcy = PET_GROUND - 96;
  for (int k = 0; k < 4; k++) {
    int r = 60 + k * 34 + (int)(10 * sinf(now * 0.02f));
    gfx->drawCircle(CX, gcy, r, C565(0xff, 0xdf, 0x8a));
  }
  for (int i = 0; i < 16; i++) {
    int px = (i * 71 + 28) % 466;
    int py = 410 - (int)((now / 8 + i * 70) % 360);   // suben y reaparecen abajo
    if (py < 30) continue;
    if (i % 4 == 0) drawMap(SPR_HEART, 32, px - 8, py - 8, 1, false);  // corazoncito
    else gfx->fillRect(px, py, 4, 4, (i % 2) ? C565(0xff, 0xe7, 0x9f) : C565(0xff, 0x9a, 0xc0));
  }

  if (t < 0.45f) {                         // reverencia / pose de despedida
    act = pmd.has(PMD_POSE) ? PMD_POSE : (pmd.has(PMD_NOD) ? PMD_NOD : PMD_IDLE);
  } else {                                 // se aleja por la derecha
    act = pmd.has(PMD_WALKR) ? PMD_WALKR : PMD_IDLE;
    x = CX + (int)(((t - 0.45f) / 0.55f) * (CX + 140));
  }
  drawPmdAct(act, x, y, now, true, false, 5);
  if (pet.showHeart())                     // corazon grande siguiendo al bicho
    drawMap(SPR_HEART, 32, x + 50, y - 190, 2, false);
}

// Dialogue d'evolution uniquement.
void drawChoiceDialog() {
  const char *q = T(S_EVO_Q), *o1 = T(S_EVO_TAP), *o2 = T(S_EVO_KEEP);
  uint16_t c1 = UI_BAR_BAD, c2 = UI_TRACK, t1 = UI_WHITE, t2 = UI_INK;
  gfx->fillRoundRect(73, 156, 320, 188, 16, uiPanel());
  gfx->drawRoundRect(73, 156, 320, 188, 16, uiInk());
  gfx->setTextColor(uiInk());
  gfx->setTextSize(2);
  gfx->setCursor(CX - (int)strlen(q) * 6, 176);
  gfx->print(q);
  gfx->fillRoundRect(93, 206, 280, 52, 12, c1);     // boton accion
  gfx->setTextColor(t1);
  gfx->setCursor(CX - (int)strlen(o1) * 6, 224);
  gfx->print(o1);
  gfx->fillRoundRect(93, 268, 280, 52, 12, c2);     // boton mantener/quedaros
  gfx->setTextColor(t2);
  gfx->setCursor(CX - (int)strlen(o2) * 6, 286);
  gfx->print(o2);
}

// boton-CTA rojo y grande para evolucionar (pulsa para llamar la atencion)
void drawEvolveButton() {
  uint32_t now = millis();
  int p = (int)(5 * sinf(now * 0.006f));  // late: -5..5
  int x = EVO_BTN_X - p, y = EVO_BTN_Y - p, w = EVO_BTN_W + 2 * p, h = EVO_BTN_H + 2 * p;
  gfx->fillRoundRect(x, y, w, h, 18, UI_BAR_BAD);
  gfx->drawRoundRect(x, y, w, h, 18, UI_WHITE);
  gfx->drawRoundRect(x + 2, y + 2, w - 4, h - 4, 16, UI_WHITE);
  gfx->setTextColor(UI_WHITE);
  gfx->setTextSize(3);
  const char *t = T(S_EVO_TAP);
  gfx->setCursor(CX - (int)strlen(t) * 9, y + h / 2 - 11);
  gfx->print(t);
}

// animacion epica de evolucion: halo radial + rayos giratorios + parpadeo del
// sprite acelerando + chispas que salen disparadas + fogonazo final
void drawEvolveFX(uint32_t now) {
  float t = pet.evolveT();          // 0..1
  int cx = CX, cy = PET_GROUND - 96;

  // halo radial que crece y pulsa
  int halo = 36 + (int)(t * 150) + (int)(8 * sinf(now * 0.02f));
  for (int k = 0; k < 4; k++) {
    int r = halo - k * 7;
    if (r > 0) gfx->drawCircle(cx, cy, r, UI_WHITE);
  }
  // rayos giratorios desde el centro del bicho
  float base = now * 0.004f;
  for (int i = 0; i < 12; i++) {
    float a = base + i * (float)(PI / 6);
    int len = 90 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.012f + i)));
    gfx->drawLine(cx, cy, cx + (int)(cosf(a) * len), cy + (int)(sinf(a) * len), UI_WHITE);
  }
  // parpadeo entre la forma ANTERIOR y la NUEVA (siluetas), acelerando; al
  // final (t>0.9) se queda fija en la nueva para el fogonazo de revelado
  int period = 60 + (int)(220 * (1.0f - t));
  bool showOld = t < 0.9f && evoPmd.loaded && ((now / period) % 2) == 0;
  if (showOld) drawPmdActM(evoPmd, PMD_IDLE, cx, PET_GROUND, 0, true, true, 5, pet.prevSpeciesId);
  else drawPmdAct(PMD_IDLE, cx, PET_GROUND, 0, true, true, 5);
  // chispas que salen disparadas
  for (int i = 0; i < 10; i++) {
    float a = i * (float)(PI / 5) + t * 4.0f;
    int d = (int)((now / 14 + i * 33) % 200);
    int sx = cx + (int)(cosf(a) * d), sy = cy + (int)(sinf(a) * d);
    gfx->fillRect(sx - 2, sy - 2, 5, 5, (i & 1) ? C565(0xff, 0xe0, 0x70) : UI_WHITE);
  }
  // fogonazo final antes de revelar la forma nueva
  if (t > 0.9f) gfx->fillCircle(cx, cy, (int)(300 * (t - 0.9f) / 0.1f), UI_WHITE);
}

void drawPet() {
  if (pmd.loaded) {
    drawPetPMD();
    return;
  }
  if (mon.loaded) {
    drawPetSD();
    return;
  }
  int fi = flashIdxForDex(pet.speciesId);
  if (fi < 0) {
    // sin SD y sin sprite de flash: aviso claro de que faltan sprites
    gfx->setTextColor(inkColor());
    gfx->setTextSize(6);
    gfx->setCursor(CX - 18, PET_CY - 80);
    gfx->print("?");
    gfx->setTextSize(2);
    const char *l1 = T(S_NO_SPRITES);
    gfx->setCursor(CX - (int)strlen(l1) * 6, PET_CY - 4);
    gfx->print(l1);
    const char *l2 = T(S_LOAD_SPRITES);
    gfx->setCursor(CX - (int)strlen(l2) * 6, PET_CY + 20);
    gfx->print(l2);
    return;
  }
  const Species &sp = SPECIES[fi];
  int s = sp.scale;
  int x = CX - 16 * s;
  int y = PET_CY - 16 * s;

  // animacion de evolucion: alterna la silueta de la forma anterior y la nueva
  if (pet.evolving()) {
    bool flash = (millis() / 300) % 2;
    int16_t showDex = (flash && pet.prevSpeciesId >= 0) ? pet.prevSpeciesId : pet.speciesId;
    int sfi = flashIdxForDex(showDex);
    if (sfi >= 0) {
      const Species &show = SPECIES[sfi];
      drawMap(show.sprite, SPRITE_H, CX - 16 * show.scale, PET_CY - 16 * show.scale, show.scale, flash);
    }
    return;
  }

  PetMood m = pet.mood();
  if (m == MOOD_HAPPY && (millis() / 500) % 2) y -= 6;  // saltito

  drawMap(sp.sprite, SPRITE_H, x, y, s, false);

  // expresiones superpuestas usando las anclas de la especie
  bool blink = (millis() % 3500 < 300);
  if (m == MOOD_SLEEPING || blink) {
    overlayEye(sp, x, y, s, sp.eyeColL);
    overlayEye(sp, x, y, s, sp.eyeColR);
  }
  if (m == MOOD_EATING) overlayMouth(sp, x, y, s, true);
  else if (m == MOOD_SAD) overlayMouth(sp, x, y, s, false);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + 20 * s, y - 2 * s, 2, false);
}

// ---------- escena de bano ----------

void startBath() {
  if (pet.isEgg() || pet.sleeping || pet.ceremony || bathUntil) return;
  bathUntil = millis() + 3000;
  bathPending = true;
  sfxPlay(SFX_EVENT_SPARKLE);
  int cx = (int)beh.x;
  for (auto &b : bubbles) {
    b.x = cx - 70 + random(140);
    b.y = PET_GROUND - random(150);
    b.r = 8 + random(16);
    b.ph = random(64);
  }
}

void drawBath() {
  uint32_t now = millis();
  if (now > bathUntil) {
    bathUntil = 0;
    if (bathPending) {
      bathPending = false;
      pet.clean();
      // pose de alegria al quedar limpio
      if (pmd.has(PMD_POSE)) {
        beh.mode = 2;
        beh.act = PMD_POSE;
        beh.t0 = now;
        beh.until = now + pmdActTotalMs(pmd.acts[PMD_POSE]) * 2;
      }
    }
    return;
  }
  uint32_t left = bathUntil - now;
  if (left > 800) {
    // espuma: pompas meciendose y subiendo poco a poco
    float t = now / 220.0f;
    for (auto &b : bubbles) {
      int bx = b.x + (int)(sinf(t + b.ph) * 6);
      int by = b.y - (int)((3000 - left) / 90);
      gfx->fillCircle(bx, by, b.r, UI_WHITE);
      gfx->drawCircle(bx, by, b.r, 0x7E3D);
      gfx->fillCircle(bx - b.r / 3, by - b.r / 3, b.r / 4, UI_BG_DAY);
    }
  } else {
    // las pompas revientan: destellos
    for (int i = 0; i < 8; i++) {
      auto &b = bubbles[i];
      int sx = b.x + (i % 3) * 6 - 6, sy = b.y - 18;
      uint16_t col = (i % 2) ? UI_BAR_WARN : UI_WHITE;
      gfx->fillRect(sx - 6, sy - 1, 13, 3, col);
      gfx->fillRect(sx - 1, sy - 6, 3, 13, col);
    }
  }
}

// ---------- mascota PMD: comportamiento ----------

uint32_t pmdActTotalMs(const PmdAct &a) {
  uint32_t t = 0;
  for (uint8_t i = 0; i < a.frames; i++) t += a.ms[i];
  return t ? t : 100;
}

uint8_t pmdFrameAt(const PmdAct &a, uint32_t t, bool loop) {
  uint32_t total = pmdActTotalMs(a);
  if (!loop && t >= total) return a.frames - 1;
  t %= total;
  uint8_t i = 0;
  while (t >= a.ms[i]) {
    t -= a.ms[i];
    i = (i + 1) % a.frames;
  }
  return i;
}

// dibuja una accion anclada por la base (centro-x, suelo) y devuelve su escala
// dibuja una accion de un PmdMon concreto (m); drawPmdAct usa el global pmd
void drawPmdActM(PmdMon &m, uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS, int16_t dex) {
  const PmdAct &a = m.acts[actId];
  if (!a.frames) return;
  const PmdAct &idle=m.acts[PMD_IDLE];
  int minC=idle.w, maxC=-1, minR=idle.h, maxR=-1;
  if (idle.frames) {
    const uint8_t *idleFrame=idle.data;
    for (int r=0;r<idle.h;r++) for (int c=0;c<idle.w;c++)
      if (idleFrame[r*idle.w+c] != 0xFF) {
        minC=min(minC,c); maxC=max(maxC,c);
        minR=min(minR,r); maxR=max(maxR,r);
      }
  }
  int visibleW=maxC>=minC ? maxC-minC+1 : idle.w;
  int visibleH=maxR>=minR ? maxR-minR+1 : idle.h;
  // Uniformite d'accueil : l'ancienne formule ne regardait que la hauteur.
  // Un Pokemon large comme Kaiminus paraissait donc beaucoup plus gros. On
  // borne maintenant la plus grande dimension de la silhouette visible.
  int targetDim=170*spriteSizePercent(dex)/100;
  int visibleMax=max(visibleW,visibleH);
  uint8_t sBase = visibleMax ? targetDim / visibleMax : 5;
  if (sBase < 2) sBase = 2;
  if (sBase > maxS) sBase = maxS;
  uint8_t s = sBase;
  while (s > 2 && a.h * s > 250) s--;  // acciones con frame grande (ataque)
  uint8_t fi = pmdFrameAt(a, t, loop);
  const uint8_t *fr = a.data + (uint32_t)fi * a.w * a.h;
  // anclar por los pies (a.base), no por el alto del lienzo: asi las acciones
  // con padding distinto (Hurt, Eat...) quedan todas a la misma altura de suelo
  int x0 = cx - a.w * s / 2, y0 = groundY - (a.base ? a.base : a.h) * s;
  for (int r = 0; r < a.h; r++) {
    const uint8_t *row = fr + r * a.w;
    for (int c = 0; c < a.w; c++) {
      uint8_t idx = row[c];
      if (idx == 0xFF) continue;
      gfx->fillRect(x0 + c * s, y0 + r * s, s, s, sil ? INK_K : m.pal[idx]);
    }
  }
}
void drawPmdAct(uint8_t actId, int cx, int groundY, uint32_t t, bool loop, bool sil, uint8_t maxS) {
  drawPmdActM(pmd, actId, cx, groundY, t, loop, sil, maxS, pet.speciesId);
}

// elige el siguiente capricho del bicho cuando esta contento
void behNext() {
  uint32_t now = millis();
  beh.t0 = now;
  int r = random(100);
  if (r < 35 && (pmd.has(PMD_WALKL) || pmd.has(PMD_WALKR))) {
    beh.mode = 1;  // paseo
    beh.targetX = 150 + random(176);
    beh.until = now + 15000;
  } else if (r < 60) {
    // gesto aleatorio entre los disponibles
    // (Hop fuera: salta demasiado alto; Sit fuera: mira hacia atras)
    static const uint8_t flair[] = { PMD_POSE, PMD_NOD, PMD_BREATH };
    uint8_t pick[3], n = 0;
    for (uint8_t f : flair)
      if (pmd.has(f)) pick[n++] = f;
    if (n) {
      beh.mode = 2;
      beh.act = pick[random(n)];
      beh.until = now + pmdActTotalMs(pmd.acts[beh.act]);
      return;
    }
    beh.mode = 0;
    beh.until = now + 2000 + random(3000);
  } else {
    beh.mode = 0;  // mirar al frente
    beh.until = now + 2000 + random(3000);
  }
}

void drawPetPMD() {
  uint32_t now = millis();

  if (pet.evolving()) {
    drawEvolveFX(now);
    return;
  }
  if (evoPmd.loaded) evoPmd.unload();  // termino la evolucion: libera la forma anterior

  PetMood m = pet.mood();
  uint8_t act;
  bool loop = true;
  if (m == MOOD_SLEEPING && pmd.has(PMD_SLEEP)) {
    act = PMD_SLEEP;
    beh.mode = 0;
  } else if (m == MOOD_EATING && pmd.has(PMD_EAT)) {
    act = PMD_EAT;
    beh.t0 = 0;
  } else if (m == MOOD_SAD && pmd.has(PMD_HURT)) {
    act = PMD_HURT;
  } else {
    // contento: el planificador decide (idle / paseo / gesto)
    if (now > beh.until) behNext();
    if (beh.mode == 1) {
      float d = beh.targetX - beh.x;
      if (fabsf(d) < 4) {
        behNext();
        act = PMD_IDLE;
      } else {
        beh.x += (d > 0 ? 3.0f : -3.0f);
        act = (d > 0) ? PMD_WALKR : PMD_WALKL;
      }
    } else {
      act = (beh.mode == 2) ? beh.act : PMD_IDLE;
      loop = false;
    }
    if (!pmd.has(act)) act = PMD_IDLE;
  }

  // Meme plafond d'echelle que le portrait de la fiche profil.
  drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, 4);

  if (pet.showHeart()) drawMap(SPR_HEART, 32, (int)beh.x + 50, PET_GROUND - 190, 2, false);
}

// sprite animado desde la SD: zoom entero por pixel, frames a su ritmo
void drawPetSD() {
  int s = mon.scale;
  int w = mon.w * s, h = mon.h * s;
  int x = CX - w / 2;
  int y = PET_CY - h / 2;

  bool sil = false;
  if (pet.evolving()) {
    sil = (millis() / 300) % 2;
  } else if (pet.mood() == MOOD_HAPPY && (millis() / 500) % 2) {
    y -= 6;  // saltito
  }

  uint16_t fm = mon.frameMs ? mon.frameMs : 100;
  uint16_t fi = pet.sleeping ? 0 : (millis() / fm) % mon.frames;
  const uint8_t *fr = mon.data + (uint32_t)fi * mon.w * mon.h;
  for (int r = 0; r < mon.h; r++) {
    const uint8_t *row = fr + r * mon.w;
    for (int c = 0; c < mon.w; c++) {
      uint8_t idx = row[c];
      if (idx == 0xFF) continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, sil ? INK_K : mon.pal[idx]);
    }
  }

  // emotes en vez de expresiones (los sprites importados no tienen anclas)
  if (pet.showHeart()) drawMap(SPR_HEART, 32, x + w - 30, y - 50, 2, false);
}

// ojo cerrado: borra el ojo 3x4 y dibuja el parpado
void overlayEye(const Species &sp, int x, int y, int s, int col) {
  gfx->fillRect(x + col * s, y + sp.eyeRow * s, 3 * s, 4 * s, sp.bodyColor);
  gfx->fillRect(x + col * s, y + (sp.eyeRow + 2) * s, 3 * s, s, INK_K);
}

// borra la sonrisa base y pinta boca abierta (comer) o ceno (triste)
void overlayMouth(const Species &sp, int x, int y, int s, bool open) {
  int mc = sp.mouthCol, mr = sp.mouthRow;
  gfx->fillRect(x + (mc - 3) * s, y + mr * s, 7 * s, 2 * s, sp.bodyColor);
  if (open) {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, 2 * s, INK_K);
  } else {
    gfx->fillRect(x + (mc - 2) * s, y + mr * s, 5 * s, s, INK_K);
    gfx->fillRect(x + (mc - 3) * s, y + (mr + 1) * s, s, s, INK_K);
    gfx->fillRect(x + (mc + 3) * s, y + (mr + 1) * s, s, s, INK_K);
  }
}

void drawPoops() {
  for (int i = 0; i < pet.poops; i++) {
    drawMap(SPR_POOP, 32, 36 + i * 46, 244, 2, false);
  }
}

void drawBars() {
  drawBar(78, 318, T(S_BAR_FOOD), pet.fullness);
  drawBar(244, 318, T(S_BAR_JOY), pet.joy);
  drawBar(78, 346, T(S_BAR_ENE), pet.energy);
  drawBar(244, 346, T(S_BAR_HYG), pet.hygiene);
}

void drawBar(int x, int y, const char *label, uint8_t val) {
  gfx->setTextColor(inkColor());
  gfx->setTextSize(2);
  gfx->setCursor(x, y);
  gfx->print(label);
  int bx = x + 48, bw = 100, bh = 15;  // +48: deja sitio a etiquetas de 4 letras (EN)
  uint16_t fill = (val >= 50) ? UI_BAR_OK : (val >= 25) ? UI_BAR_WARN : UI_BAR_BAD;
  gfx->fillRoundRect(bx, y, bw, bh, 4, UI_TRACK);
  int fw = (bw - 4) * val / 100;
  if (fw > 0) gfx->fillRoundRect(bx + 2, y + 2, fw, bh - 4, 3, fill);
}

void drawButtons() {
  for (int i = 0; i < 4; i++) {
    bool off = pet.sleeping && i != 2;  // durmiendo solo funciona LUZ
    int frame = buttons[i].frameSize;
    int bx = buttons[i].cx - frame / 2, by = buttons[i].cy - frame / 2;
    if (!pet.sleeping) gfx->fillRoundRect(bx, by, frame, frame, 14, uiPanel());
    gfx->drawRoundRect(bx, by, frame, frame, 14, inkColor());
    if (!off) {
      // Bouton JOUER de l'accueil : même vraie Poké Ball pixel-art que la page RECORDS.
      // Les autres boutons conservent leurs sprites d'origine.
      drawMapSized(buttons[i].icon, 16,
                   buttons[i].cx + buttons[i].iconDx,
                   buttons[i].cy + buttons[i].iconDy,
                   buttons[i].iconSize, false);
    }
  }
}

const char *eggMsg() {
  switch (pet.eggCracks()) {
    case 0: return T(S_EGG_TOUCH);
    case 1: return T(S_EGG_MOVES);
    default: return T(S_EGG_ALMOST);
  }
}

const char *statusMsg() {
  if (pet.evolving()) return T(S_EVOLVING);
  if (bathUntil) return "Splish splash!";  // onomatopeya universal
  if (pet.sleeping) return "Zzz...";
  if (pet.eating()) return T(S_EATING);
  if (pet.showHeart()) return T(S_LIKES);
  if (pet.fullness < 25) return T(S_HUNGRY);
  if (pet.hygiene < 25) return T(S_NEEDS_BATH);
  if (pet.energy < 25) return T(S_EXHAUSTED);
  if (pet.joy < 25) return T(S_SAD);
  if (pet.shiny && pet.ageMinutes < 15) return T(S_IS_SHINY);
  return T(S_HAPPY);
}

// dibuja un mapa de n x n pixeles escalado; silhouette=true lo pinta en tinta
void drawMap(const char *const *map, int n, int x, int y, int s, bool silhouette) {
  for (int r = 0; r < n; r++) {
    for (int c = 0; c < n; c++) {
      char ch = map[r][c];
      if (ch == '.') continue;
      gfx->fillRect(x + c * s, y + r * s, s, s, silhouette ? INK_K : spriteColor(ch));
    }
  }
}

// Mise a l'echelle independante au pixel pres pour les boutons d'accueil.
void drawMapSized(const char *const *map, int n, int cx, int cy, int target, bool silhouette) {
  int left = cx - target / 2;
  int top = cy - target / 2;
  for (int dy = 0; dy < target; dy++) {
    int sy = dy * n / target;
    for (int dx = 0; dx < target; dx++) {
      int sx = dx * n / target;
      char ch = map[sy][sx];
      if (ch == '.') continue;
      gfx->drawPixel(left + dx, top + dy, silhouette ? INK_K : spriteColor(ch));
    }
  }
}
