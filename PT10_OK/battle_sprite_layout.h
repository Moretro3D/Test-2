#pragma once
#include <stdint.h>

// Configuration finale et unique des 386 sprites de combat.
// Index 0 = Bulbizarre (#001), index 385 = Deoxys (#386).
// Une valeur scale à 0 signifie 100 %. Cette valeur unique est utilisée sur
// l'accueil, la fiche générale et les deux côtés du combat. Aucun autre
// coefficient caché n'est appliqué. Les décalages restent propres aux pages.
struct BattleSpriteLayout {
  uint8_t scale;
  int8_t homeX;
  int8_t homeY;
  int8_t cardX;
  int8_t cardY;
  int8_t battlePlayerX;
  int8_t battlePlayerY;
  int8_t battleEnemyX;
  int8_t battleEnemyY;
};

// Base propre : les 386 entrées commencent toutes à 100 %, centrées.
// Les valeurs seront renseignées espèce par espèce pendant l'audit visuel.
static const BattleSpriteLayout BATTLE_SPRITE_LAYOUT[386] = {};

static inline const BattleSpriteLayout &battleSpriteLayout(int16_t dex) {
  static const BattleSpriteLayout fallback = {};
  static const BattleSpriteLayout enlarged130 = {130, 0, 0, 0, 0, 0, 0, 0, 0};
  if (dex == 144 || dex == 145 || dex == 146 || dex == 248 || dex == 382)
    return enlarged130; // Artikodin, Electhor, Sulfura, Tyranocif, Kyogre
  return (dex >= 1 && dex <= 386) ? BATTLE_SPRITE_LAYOUT[dex - 1] : fallback;
}

static inline uint8_t pokemonSpriteScale(int16_t dex) {
  const BattleSpriteLayout &layout = battleSpriteLayout(dex);
  return layout.scale ? layout.scale : 100;
}

static inline int8_t battleSpriteX(int16_t dex, bool playerSide) {
  const BattleSpriteLayout &layout = battleSpriteLayout(dex);
  return playerSide ? layout.battlePlayerX : layout.battleEnemyX;
}

static inline int8_t battleSpriteY(int16_t dex, bool playerSide) {
  const BattleSpriteLayout &layout = battleSpriteLayout(dex);
  return playerSide ? layout.battlePlayerY : layout.battleEnemyY;
}
