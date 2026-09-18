#!/usr/bin/env python3
from pathlib import Path
import json
import re

root = Path(__file__).resolve().parents[1]
ino = (root / "TamaPoke.ino").read_text(encoding="utf-8")
dex = (root / "dex.h").read_text(encoding="utf-8")
pet = (root / "pet.cpp").read_text(encoding="utf-8")
html = (root / "web" / "index.html").read_text(encoding="utf-8")
readme = (root / "README.md").read_text(encoding="utf-8")
manifest = json.loads((root / "web" / "manifest.json").read_text(encoding="utf-8"))
build = (root / "tools" / "build_web.sh").read_text(encoding="utf-8")
sprites_src = (root / "tools" / "sprites.py").read_text(encoding="utf-8")

def check(value, label):
    if not value:
        raise SystemExit("ECHEC V9.43: " + label)
    print("OK  ", label)

all_public = "\n".join((ino, dex, pet, html, readme, build, json.dumps(manifest)))
check("MIMIKYU" not in all_public.upper() and "MIMIQUI" not in all_public.upper(), "aucune edition Mimiqui")
check("ShadowEnemyx" not in all_public, "aucun ancien lien ShadowEnemyx")
check('uint16_t displayedDexNumber(int16_t dex) { return dex; }' in ino, "numeros nationaux 001-386")
check('"JIRACHI"' in dex, "Jirachi restaure au numero 385")
check("{ 1, 4, 7 }" in ino and "{ 152, 155, 158 }" in ino and "{ 252, 255, 258 }" in ino,
      "neuf starters 1G/2G/3G")
check("starterLanguageChosen" in ino and "LANGUAGE / LANGUE" not in ino,
      "choix de langue epure avant generation")
check("starterName=dexName" in ino, "noms des trois starters affiches")
check("STARTER_COLORS[3]" in ino and "// plante" in ino and "// feu" in ino and "// eau" in ino,
      "couleurs Plante vert, Feu rouge, Eau bleu")
check("drawStarterCentered(T(S_TOUCH_POKEBALL), 330, 1, 0x0000)" in ino,
      "instruction Pokeball noire")
check('gfx->setCursor(CX - 70, 284); gfx->print("PokeTama")' in ino and
      'gfx->setCursor(CX - 79, 326); gfx->print("Moretro3D")' in ino and
      'drawStarterCentered("PokeTama", 48, 3' in ino and
      'drawStarterCentered("Moretro3D", 86, 2' in ino,
      "marque centree au demarrage et sur la langue")
check('#include "brand_logo.h"' in ino and
      'drawBrandLogo(CX - BRAND_LOGO_W / 2 + 1, 96)' in ino and
      (root / "brand_logo.h").is_file() and
      (root / "assets" / "moretro3d-logo-transparent.png").is_file(),
      "logo Moretro3D transparent embarque au demarrage")
check(ino.count("gfx->fillRoundRect(96,368,274,46,14,UI_TRACK)") == 2 and
      ino.count("x >= 96 && x <= 370 && y >= 368 && y <= 414") == 2,
      "boutons retour centres et remontes dans le rond")
check("drawStarterCentered(T(S_CHOOSE_GENERATION), 92, 2, 0x0000)" in ino and
      "drawStarterCentered(generation, gy + 22, 2, 0x0000)" in ino and
      "drawStarterCentered(generation, 112, 2, 0x0000)" in ino,
      "titres de generation noirs")
check("gfx->fillScreen(UI_WHITE);" in ino and
      "drawStarterThumbCentered(th, starterPreviewDex, CX, 190, 8)" in ino and
      "gfx->fillRoundRect(84,358,140,50,14,UI_TRACK)" in ino and
      "gfx->fillRoundRect(242,358,140,50,14,UI_BAR_OK)" in ino,
      "fiche starter blanche adaptee a l'ecran rond")
check("spriteSizePercent" in ino and "visibleW=maxX-minX+1" in ino and
      "target=target*spriteSizePercent(dex)/100" in ino and
      "target=145*spriteSizePercent(dex)/100" in ino,
      "sprites centres sur silhouette et tailles par evolution")
check("panel->setBrightness(0)" in ino and "gfx->flush();\n  panel->setBrightness(120);" in ino,
      "splash complet avant allumage AMOLED")
check("if (!screenOff) return false;" in ino, "aucun light-sleep ecran visible")
check("if (now - lastPwr > 60)" in ino and "markUiDirty();\n          render();" in ino,
      "reveil PWR rapide avec frame complet")
check("starterPick ? 1 : pickEggSpecies()" in pet and "if (starterPick) return" in pet,
      "aucune eclosion aleatoire avant choix")
check("speciesId = dex" in (root / "pet.h").read_text(encoding="utf-8") and
      "dexCaught[(dex - 1) >> 3]" in (root / "pet.h").read_text(encoding="utf-8"),
      "starter installe directement sans oeuf")
check("speciesId == 133 && caughtTotal <= 1" in pet,
      "ancienne sauvegarde Evoli reparee")
check("case 252: case 255: case 258:" in (root / "pet.h").read_text(encoding="utf-8") and
      "case 133" not in (root / "pet.h").read_text(encoding="utf-8"), "Evoli refuse comme starter")
check(manifest["name"] == "PokeTama Moretro3D - V9.87 Test-2", "manifest Moretro3D")
check(manifest["version"] == "1.46.49-moretro3d-v9.87-boite-separee", "version Web coherente")
check('VERSION="1.46.49-moretro3d-v9.87-boite-separee"' in build, "version de compilation coherente")
check('if (!powerSave) return 110;' in ino, "cadence AMOLED anti-chevauchement")
check('dotsX' not in ino and 'cardPage--;' in ino and 'cardPage++;' in ino,
      "fleches tactiles remplacent les billes de pages")
check('(void)dex;' in ino and 'return 92;' in ino and
      'if (!pre && next) return 92;' not in ino,
      "une seule echelle reduite pour tous les sprites")
check('drawCollectionFrame(CX, 146, 98' in ino, "cadre centre sur la silhouette")
check('snprintf(bonus' not in ino and 'rewardWasComplete' in pet,
      "recompense quotidienne unique apres trois objectifs")
check('x + (186 - (int)strlen(desc) * 12) / 2' in ino,
      "textes des medailles centres")
check('int x = 42 + (i % 2) * 196' in ino and
      'fillRoundRect(x, y, 186, 44' in ino,
      "cases Medailles dans la zone circulaire sure")
check('uint16_t evoCol = darkMode ? UI_WHITE : UI_INK;' in ino and
      'S_MISTAKES_FMT' not in ino,
      "progres sombre lisible et negligences masquees")
pet_h = (root / "pet.h").read_text(encoding="utf-8")
forbidden_departure = (
    "canRunawayNow", "canFarewellNow", "wantFarewellButton",
    "startRunaway", "startFarewell", "drawRunawayButton", "drawFarewellButton",
)
check(all(token not in ino and token not in pet and token not in pet_h
          for token in forbidden_departure) and 'line == "BYE"' not in ino,
      "abandon et adieu entierement inaccessibles")
check('drawBattleButtonLabel(240,396,154,T(S_RUN_BATTLE))' in ino and
      'x >= 240 && x <= 394 && y >= 383 && y <= 426' in ino and
      'finishBattle() n\'est pas appele' in ino,
      "fuite tactile en combat sauvage sans gain ni penalite")
check('gfx->fillRect(0, 320, 466, 146' in ino and
      'gfx->fillRoundRect(72,334,154,43' in ino and
      'gfx->fillRoundRect(240,383,154,43' in ino,
      "bandeau combat classique pleine largeur et boutons 2x2")
check('#define CARD_COUNT 9' in ino and 'renderCardRecords();' in ino and
      'drawPersonalityRecord(68,94' in ino,
      "records separes sur une neuvieme page")
check('drawCardStat(226,T(S_BAR_JOY)' in ino and
      'drawPersonalityRecord(52,276' not in ino,
      "page Caractere allegee sans records")
check('return darkMode ? UI_WHITE : UI_INK;' in ino and
      'cardPage == 2 ? dailyTextColor()' in ino,
      "Quotidien blanc complet en mode sombre")
check('T(S_STAT_WGT)' not in ino and 'drawCardStat(102, T(S_STAT_ATK)' in ino and
      'gfx->fillRoundRect(96, 264, 274, 40' in ino,
      "Combat sans PDS et contenu remonte")
check('int navY = 360;' in ino and 'int cardNavY = 354;' in ino,
      "navigation remontee dans la zone sure du cercle")
check('drawPmdAct(act, (int)beh.x, PET_GROUND, now - beh.t0, loop || act == PMD_IDLE, false, 4);' in ino and
      'drawPmdAct(PMD_IDLE, CX, 206, millis(), true, false, 4);' in ino,
      "accueil et portrait utilisent le meme plafond de sprite")
check('fillRoundRect(84, 370, 62, 42' in ino and
      'fillRoundRect(154, 370, 158, 42' in ino and
      'if (y >= 364 && y <= 420)' in ino,
      "Aide OK et fleches remontes dans le cercle")
check('#define BOX_ROWS 8' in ino and
      'Grille 4x2 de mini-sprites captures' in ino and
      'drawStarterThumbCentered(thumb, dex, x + 39, y + 37, 2)' in ino,
      "Boite visuelle avec huit mini-sprites captures")
check('fillRoundRect(76, 292, 94, 38' in ino and
      'x >= 76 && x <= 170 && y >= 286 && y <= 334' in ino,
      "pagination Boite remontee et tactile")
check('snprintf(name, sizeof(name), "#%03u %s"' not in ino,
      "ancienne liste de noms de la Boite supprimee")
check('style inventaire GBA' in sprites_src and
      'Baie bleue ronde avec calice etoile' in sprites_src and
      'Baie verte nervuree' in sprites_src,
      "trois nourritures GBA originales et distinctes")
check('Super Bonbon : emballage bleu' in sprites_src and
      '"...kbyyfyBbk...."' in sprites_src,
      "Super Bonbon pixel-art integre")
check('Poke Ball 16x16 originale' in sprites_src and
      '"kkkkkkkwwkkkkkkk"' in sprites_src and
      '"kwwwwwkwwkwwwwwk"' in sprites_src,
      "Poke Ball 16x16 detaillee et bouton central")
check("Coeur d'affection Pokemon" in sprites_src and
      'g.rect(lx * 2, ly * 2' in sprites_src,
      "coeur d'affection pixel-art compact")
check('uint8_t frameSize' in ino and 'uint8_t iconSize' in ino and
      'uint8_t hitRadius' in ino and 'int8_t iconDx' in ino and
      '{ 202, 404, SPR_ICON_PLAY,  52, 28, 36, 0, 0 }' in ino and
      'drawMapSized(buttons[i].icon' in ino,
      "reglage independant des quatre boutons au pixel pres")
check('drawBrandLogo(CX - BRAND_LOGO_W / 2 + 1, 96)' in ino and
      'setCursor(CX - 70, 284)' in ino and
      'setCursor(CX - 79, 326)' in ino,
      "logo et textes de demarrage centres optiquement")
check('delay(1500);' in ino and "aucun flash noir" in ino,
      "demarrage prolonge de 1,5 seconde sans flash")
check('renderTimeSettings()' in ino and 'renderSoundSettings()' in ino and
      'renderResetSettings()' in ino and
      'drawSettingsRow(58,88,350,50,T(S_SET_TIME)' in ino and
      'drawSettingsRow(58,268,350,50,T(S_RESET)' in ino,
      "reglages repartis en quatre sous-pages")
check('S_RESET, S_RESET_WARNING, S_RESET_CONFIRM' in (root / 'i18n.h').read_text(encoding='utf-8') and
      'EFFACER ET REDEMARRER' in (root / 'i18n.cpp').read_text(encoding='utf-8') and
      'LOESCHEN UND NEUSTART' in (root / 'i18n.cpp').read_text(encoding='utf-8'),
      "remise a zero traduite dans les six langues")
display_start=ino.index('void renderDisplaySettings()')
display_fn=ino[display_start:ino.index('void drawSettingsTitle(', display_start)]
reset_fn=ino[ino.index('void renderResetSettings()'):ino.index('void renderClock()')]
check('warning' not in display_fn and
      reset_fn.index('const char *warning') < reset_fn.index('strlen(warning)') and
      reset_fn.index('uint8_t warnScale') < reset_fn.index('*warnScale'),
      "variables de l'avertissement declarees dans la bonne portee")
check('darkMode ? UI_WHITE' in ino and
      'gfx->setTextColor(selected ? UI_WHITE : uiInk())' in ino and
      'gfx->setTextColor(uiInk())' in ino and
      'setTextColor(UI_TRACK)' not in ino and
      'setTextColor(UI_BG_DAY)' not in ino,
      "tous les textes utilisent un contraste adapte au mode sombre")
check('uint16_t timeInk=darkMode ? UI_WHITE : UI_INK' in ino and
      'setCursor(CX-(int)strlen(T(S_SET_TIME))*9,62)' in ino and
      'drawClockBtn(90,216,"-")' in ino and
      'drawClockBtn(316,216,"+")' in ino,
      "page Heure descendue, alignee et noire en mode clair")
check('C565(0x32,0x38,0x44)' in ino and
      'drawSettingsRow(64,352,164,38,T(S_LANG_LABEL)' in ino and
      'drawSettingsRow(238,352,164,38,T(S_POWER_SAVE_LABEL)' in ino,
      "langue et economie en gris fonce dans Affichage")
check('drawSettingsBack' not in ino and
      'fillRoundRect(30,28,54,36' not in ino and
      'setCursor(49,38)' not in ino and
      'x>=20 && x<=94 && y>=18 && y<=72' not in ino,
      "aucun symbole ni zone tactile en haut a gauche des Reglages")
check('int visibleW=maxC>=minC ? maxC-minC+1' in ino and
      'int visibleH=maxR>=minR ? maxR-minR+1' in ino and
      'int visibleMax=max(visibleW,visibleH)' in ino and
      'uint8_t sBase = visibleMax ? targetDim / visibleMax : 5' in ino,
      "sprites accueil uniformises sur largeur et hauteur visibles")
web_css=(root / 'web' / 'moretro.css').read_text(encoding='utf-8')
check('moretro3d-logo.png' in html and 'moretro.css?v=9.43' in html and
      'class="hero"' in html and 'class="steps"' in html and
      all(f'id="{button_id}"' in html for button_id in ('connect','auto','music','bar','log')) and
      '@media(max-width:760px)' in web_css and
      (root / 'web' / 'moretro3d-logo.png').is_file(),
      "integration Shopify sans header footer responsive et fonctions USB conservees")
check(all(f'data-lang="{lang}"' in html for lang in ('fr','en','es','de','it','pt')) and
      all(f'{lang}:' in html for lang in ('fr','en','es','de','it','pt')) and
      "localStorage.getItem('poketama-web-lang')" in html and
      'function applyLanguage(lang)' in html and 'function status(index' in html,
      "page Web et journal USB traduits dans les six langues avec drapeaux")
flag_files=('fr.svg','gb.svg','es.svg','de.svg','it.svg','pt.svg')
check(all(f'flags/{flag}' in html for flag in flag_files) and
      all((root / 'web' / 'flags' / flag).is_file() for flag in flag_files) and
      not any(flag in html for flag in ('🇫🇷','🇬🇧','🇪🇸','🇩🇪','🇮🇹','🇵🇹')),
      "six drapeaux SVG locaux sans emoji pour compatibilite Google")
shopify_html=(root / 'web' / 'shopify.html').read_text(encoding='utf-8')
shopify_liquid=(root / 'shopify' / 'poketama-custom-liquid.liquid').read_text(encoding='utf-8')
check('<header class="topbar">' not in shopify_html and '<footer>' not in shopify_html and
      'class="embed-toolbar"' in shopify_html and
      "type:'poketama-shopify-height'" in shopify_html,
      "page Shopify sans header/footer et hauteur automatique")
check('https://moretro3d.github.io/Test-1/shopify.html' in shopify_liquid and
      'allow="serial; usb"' in shopify_liquid and
      "event.origin !== 'https://moretro3d.github.io'" in shopify_liquid,
      "Custom Liquid Shopify securise et compatible installation")
check('poketama-screen-ectoplasma-niv75-transparent.png' in html and
      (root / 'web' / 'poketama-screen-ectoplasma-niv75-transparent.png').is_file() and
      'ECTOPLASMA' not in ino,
      "ecran transparent Ectoplasma niveau 75 reserve a la page Web")
check("PokeTama Moretro3D" in html and "Installer le firmware Moretro3D" in html,
      "page firmware Moretro3D")
check("Moretro3D/Test-1" in readme, "page GitHub Moretro3D/Test-1")
check('fr:{eyebrow:"Compagnon Pokémon de poche"' in html and
      'document.documentElement.lang=lang' in html,
      "francais complet disponible et applique au document")
print("AUDIT MORETRO3D V9.43 OK")
