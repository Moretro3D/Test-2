#!/usr/bin/env python3
from pathlib import Path
import re, subprocess, sys, csv, wave, shutil, ast

ROOT=Path(__file__).resolve().parents[1]
ino=(ROOT/"TamaPoke.ino").read_text(encoding="utf-8")
dex=(ROOT/"dex.h").read_text(encoding="utf-8")
battle=(ROOT/"battle.cpp").read_text(encoding="utf-8")
battle_bases=(ROOT/"battle_bases.h").read_text(encoding="utf-8")
pet=(ROOT/"pet.cpp").read_text(encoding="utf-8")
pet_h=(ROOT/"pet.h").read_text(encoding="utf-8")
audio=(ROOT/"audio.cpp").read_text(encoding="utf-8")
chirp=(ROOT/"species_chirp.cpp").read_text(encoding="utf-8")
sdmon=(ROOT/"sdmon.cpp").read_text(encoding="utf-8")

def ok(cond,msg):
    if not cond:
        raise SystemExit("FAIL V9: "+msg)
    print("OK  ",msg)

ok("1.46.99-moretro3d-v10.20-home-habitats-fit" in ino and
   "1.46.99-moretro3d-v10.20-home-habitats-fit" in (ROOT/"web/manifest.json").read_text(encoding="utf-8") and
   "1.46.99-moretro3d-v10.20-home-habitats-fit" in (ROOT/"web/index.html").read_text(encoding="utf-8") and
   "1.46.99-moretro3d-v10.20-home-habitats-fit" in (ROOT/"web/shopify.html").read_text(encoding="utf-8") and
   "1.46.99-moretro3d-v10.20-home-habitats-fit" in (ROOT/"tools/build_web.sh").read_text(encoding="utf-8"),
   "version 1.46.99 coherente et caches installateur invalides")
ok('gfx->setCursor(CX - 18, 366); gfx->print("V10")' in ino and
   '<div class="version">Firmware V10</div>' in (ROOT/"web/index.html").read_text(encoding="utf-8") and
   '<div class="version">Firmware V10</div>' in (ROOT/"web/shopify.html").read_text(encoding="utf-8"),
   "V10 affichee au demarrage, sur la page officielle et sur Shopify")
ok('location.replace("https://moretro3d.fr/pages/poketama")' in (ROOT/"web/index.html").read_text(encoding="utf-8") and
   'https://moretro3d.github.io/Test-1/manifest.json' in (ROOT/"web/shopify.html").read_text(encoding="utf-8"),
   "GitHub Pages redirige vers Shopify et conserve le Web Flasher officiel")
workflow=(ROOT/".github/workflows/pages.yml").read_text(encoding="utf-8")
ok("github.repository == 'Moretro3D/Test-2'" in workflow and
   "sed -i '/http-equiv=\"refresh\"/d' web/index.html" in workflow and
   "! grep -q 'location.replace' web/index.html" in workflow,
   "Test-2 affiche son Web Flasher sans redirection Shopify")
test2_launcher=(ROOT/"OUVRIR_WEB_FLASHER_TEST_2.bat").read_text(encoding="utf-8")
ok("https://moretro3d.github.io/Test-2/?v=1.46.99-moretro3d-v10.20-home-habitats-fit" in test2_launcher,
   "lanceur direct Test-2 avec contournement du cache")

ok('snprintf(caught, sizeof(caught), T(S_CAUGHT_COUNT_FMT), (unsigned)pet.caughtCount());' in ino and
   '#define POKETAMA_UNLOCK_ALL_386 0' in pet_h and
   '#define SPRITE_AUDIT_BUILD 0' in ino,
   "compteur Boite reel et debloquages de test desactives")

# 466x466 / UI 1.75"
ok("#define CX 233" in ino and "#define CY 233" in ino, "centre écran 466x466")
ok("#define GAL_CELL 72" in ino and "#define GAL_X 89" in ino, "Pokédex compact dans le rond")

# Pokedex
ok("Pagination compacte" not in ino, "billes pagination Pokédex supprimées")
ok('gfx->fillRoundRect(18, 214, 48, 52' in ino, "flèche gauche Pokédex centrée")
ok('gfx->fillRoundRect(400, 214, 48, 52' in ino, "flèche droite Pokédex centrée")
ok('gfx->fillRoundRect(136, 398, 194, 40' in ino, "bouton RETOUR Pokédex remonté")
ok("galleryPage--" in ino and "galleryPage++" in ino, "navigation tactile flèches Pokédex")
ok("galleryFilter" not in ino and "S_FILTER_ALL" not in ino and
   "R:%u C:%u" not in ino and "S_RAISED_MARK" not in ino and
   "S_CAUGHT_MARK" not in ino,
   "Pokedex sans filtres ni mentions eleve/capture")
ok("drawBattleCaughtBall(350,354,24,26)" in ino and
   "if (known) drawBattleCaughtBall" in ino,
   "Pokeball discrete sur les fiches de Pokemon deja obtenus")
ok("#define GAL_ROWS 3" in ino and "#define GAL_PAGE_SIZE (GAL_COLS * GAL_ROWS)" in ino and
   "galleryPage * GAL_PAGE_SIZE" in ino and
   "snprintf(caught,sizeof(caught),T(S_CAUGHT_COUNT_FMT),(unsigned)pet.caughtCount())" in ino,
   "Pokedex en grille 4x3 avec total reel des captures")
ok("switchToCaught(galleryDetail)" not in ino and
   "drawPokedexInfoPopup(galleryDetail)" in ino and
   "galleryInfoOpen=true" in ino and
   'snprintf(line,sizeof(line),"PV %u  ATK %u"' in ino and
   'snprintf(line,sizeof(line),"EVOL: %s N.%u"' in ino,
   "popup Informations du Pokedex avec types habitat stats et evolution")
ok("boxSelectionDex=dex" in ino and
   "pet.switchToCaught(dex)" in ino and
   "const char *care=T(S_CARE_ACTION)" in ino and
   "galleryDetail = dex;" not in ino[ino.index("else if (cardPage == 3)"):ino.index("else if (cardPage == 4")],
   "popup S'occuper selectionne le compagnon sans quitter la Boite")
ok("confirmUntil" not in ino and "pet.release();" not in ino,
   "relachement par appui long totalement retire de l'accueil")
ok("if (battleOpen) return !battleDirty" in ino and
   "void performBattleAction(BattleAction action)" in ino and
   "battleDirty = true;" in ino[ino.index("void performBattleAction"):ino.index("void battleTap")],
   "combat redessine uniquement lors des changements pour eviter les flashs noirs")
ok("battleCatchAnimPhase" in ino and "drawBattleCatchAnimation()" in ino and
   "elapsed >= 4000UL" in ino and
   "uint32_t battleAnimNow = millis()" in ino and
   "uint32_t elapsed = battleAnimNow - battleCatchAnimStart" in ino and
   "battleCatchAnimPhase = battleCatchSuccess ? 3 : 4" in ino and
   "bool hideEnemy = battleCatchAnimPhase==2 || battleCatchAnimPhase==3" in ino and
   "int x=354, y=166" in ino and
   "y=266-(int)(100UL*t/650UL)" in ino,
   "capture animee prolongee avec Ball remontee, fermee en succes et reapparition en echec")

# Aide : meme structure dans les six langues et lignes lisibles sur le rond.
help_match=re.search(r'HELP_LINES\[LANG_COUNT\]\[HELP_PAGE_COUNT\]\[HELP_LINE_COUNT\]\s*=\s*\{(.*?)\n\};', ino, re.S)
ok(help_match is not None and "#define HELP_PAGE_COUNT 6" in ino, "aide reduite a six pages")
help_lines=ast.literal_eval("["+help_match.group(1).replace("{","[").replace("}","]")+"]")
ok(len(help_lines)==6 and all(len(lang)==6 and all(len(page)==6 for page in lang) for lang in help_lines),
   "six pages et six lignes pour chaque langue")
ok(all(len(line)<=31 and line.isascii() for lang in help_lines for page in lang for line in page),
   "aide lisible sur ecran rond et compatible police bitmap")
ok("light sleep" not in ino and "Sans erase garde save" not in ino and
   "Faim basse = erreur" not in ino and "SON TOUT: touche pet" not in ino,
   "ancienne aide technique et formulations incorrectes supprimees")

# Fiche
ok('const char *cardBack=T(S_LAN_BACK)' in ino, "bouton RETOUR fiche traduit")
ok("#define CARD_COUNT 10" in ino and "renderCardKantoGyms" in ino and
   "KANTO_LEADER_DEX[8] = { 95, 121, 26, 45, 110, 65, 59, 112 }" in ino,
   "page des huit arenes de Kanto et Pokemon emblematiques")
ok("const char *title=kantoArenaTitle()" in ino and
   ino.index("const char *title=kantoArenaTitle()") > ino.index("if (kantoArenaDetail >= 0)") and
   "gfx->setCursor(CX-(int)strlen(count)*6,64)" in ino,
   "titre Arenes Kanto conserve sur la liste et retire de la fiche champion")
ok("kantoArenaDetail" in ino and "earned || (nextArena && requirementsMet)" in ino and
   "startKantoArenaBattle" in ino,
   "champions deverrouilles progressivement avec confirmation de combat")
ok("KANTO_REQUIRED_LEVEL[8] = { 10, 18, 25, 32, 40, 50, 60, 75 }" in ino and
   "KANTO_REQUIRED_CAUGHT[8] = { 3, 8, 15, 25, 40, 60, 85, 110 }" in ino and
   "kantoCaughtCount() < KANTO_REQUIRED_CAUGHT[index]" in ino and
   "for(int16_t dex=1;dex<=151;dex++) if(pet.isCaught(dex))" in ino and
   'snprintf(requirement,sizeof(requirement),"N%u CAP%u"' in ino,
   "chaque arene exige un niveau et des captures Kanto visibles")
ok("gfx->fillRect(x+px*2,y+py*2,2,2,color)" in ino and
   "galleryPmd.load(KANTO_LEADER_DEX[arena], false)" in ino and
   "drawBattlePmd(galleryPmd,KANTO_LEADER_DEX[i],334,264,108" in ino and
   "drawKantoLeaderSprite(56,110,i)" in ino and
   "gfx->setCursor(124-(int)strlen(leader)*9,80)" in ino,
   "champions agrandis et Pokemon emblematiques affiches en PMD complet")
ok("void drawKantoArenaBackground(uint8_t index)" in ino and
   "static const uint16_t sky[8]" in ino and
   "static const uint16_t wall[8]" in ino and
   "static const uint16_t floorCol[8]" in ino and
   "drawKantoArenaBackground(i)" in ino and
   "drawKantoBadge(120,246,i" in ino,
   "huit fonds d'arene thematiques et champion decale dans le cadre")
ok("gfx->fillRoundRect(136,302,194,46,12,UI_BAR_BAD)" in ino and
   "cardPage == 9 && kantoArenaDetail >= 0" in ino and
   "int x=(i&1)?242:54, y=94+(i/2)*52" in ino,
   "retour doublon retire, retour inferieur actif et badges agrandis descendus")
ok('prefs.putUChar("kbadge", kantoBadges)' in pet and
   'prefs.getUChar("kbadge", 0)' in pet and "awardKantoBadge" in pet and
   "battleCatchOffered = !battleArena" in ino,
   "badges persistants compatibles anciennes sauvegardes et sans capture d'arene")
ok('#include "kanto_badge_sprites.h"' in ino and
   "KANTO_BADGE_PIXELS" in ino and
   "#define KANTO_BADGE_W 32" in (ROOT/"kanto_badge_sprites.h").read_text(encoding="utf-8") and
   "gfx->drawPixel(cx-16+px,cy-16+py,color)" in ino and
   (ROOT/"kanto_badge_sprites.h").exists() and
   (ROOT/"assets/design/badges-kanto-originaux.png").exists(),
   "huit nouveaux badges Kanto HD integres en pixel-perfect")
ok("if (col >= 0 && row >= 0 && row < 4 && (earned || (nextArena && requirementsMet)))" in ino,
   "fiches verrouillees conservees pour garder le mystere")
ok('#include "kanto_leader_sprites.h"' in ino and
   "KANTO_LEADER_PIXELS" in ino and
   (ROOT/"kanto_leader_sprites.h").exists() and
   (ROOT/"assets/design/champions-kanto-recolores.png").exists() and
   (ROOT/"assets/design/champions-kanto-recolores-hd.png").exists() and
   (ROOT/"tools/prepare_kanto_leaders_hd.py").exists(),
   "huit sprites recolores des champions integres sans fond")
ok('gfx->setCursor(96, navY + 11); gfx->print("<")' in ino and
   'gfx->setCursor(354, navY + 11); gfx->print(">")' in ino and
   'dotsX' not in ino,
   "pagination fiche remplacee par deux fleches")
ok('y >= cardNavY && y <= cardNavY + 58' in ino, "zone tactile RETOUR fiche")
ok("cardPage == 0 && y >= 366 && y <= 398" not in ino, "ancienne zone cachée de cadre supprimée")

# Accueil / heure / habitat
ok("drawHomeIdentity" in ino, "nom + niveau séparés sur accueil")
ok("if (pet.weight > 60) return T(S_CHUBBY)" not in ino, "message ambigu 'un peu rond' supprimé")
ok("gNight = pet.sleeping || h < 6 || h >= 20;" in ino, "fond piloté par heure réelle, pas par thème sombre")
ok("06-07 lever" in ino and "08-17 jour" in ino and "18-19 coucher" in ino, "cycle 24h en 4 phases")
sun_code=ino[ino.index("void drawScene("):ino.index("void drawStarterPokeball(")]
sun_points=[]
for minute in (6*60, 8*60, 13*60, 18*60, 20*60):
    daylight=minute-6*60
    x=94+(278*daylight)//(14*60)
    y=232-18-(600*daylight*(14*60-daylight))//((14*60)*(14*60))
    sun_points.append((x,y))
ok("int sunX=94+(278*daylight)/(14*60);" in sun_code and
   "int sunY=HORIZON-18-" in sun_code and
   all(f"gfx->fillCircle(sunX,sunY,{radius}" in sun_code for radius in (32,25,34)) and
   sun_points[0][0]<sun_points[1][0]<sun_points[2][0]<sun_points[3][0]<sun_points[4][0] and
   sun_points[2][1]<sun_points[0][1] and sun_points[2][1]<sun_points[4][1] and
   all((x-233)**2+(y-233)**2 < (233-34)**2 for x,y in sun_points),
   "soleil de gauche a droite et toujours dans l'ecran rond")
ok("DEX_TBL[pet.speciesId].biome" in ino, "habitat lié au Pokémon actif")
ok("drawCollectionFrame(CX, PET_GROUND - 96" not in ino, "aucun anneau/flèche latérale sur accueil")

# 386
ok("#define DEX_COUNT 386" in dex, "Pokédex 386")
entries=re.findall(r'\{\s*"[^"]+",\s*\d+,\s*\d+,\s*[A-Z_]+,\s*0x[0-9A-Fa-f]+,\s*\d+,\s*\d+,\s*\d+,\s*\d+,\s*TYPE_[A-Z_]+,\s*TYPE_[A-Z_]+,\s*\d+\s*\},\s*//\s*(\d+)',dex)
ok(len(entries)>=386, "386 entrées combat/habitat présentes")

m=re.search(r'// FR\n  \{ ([^\n]+) \},',dex)
ok(m is not None, "table noms FR présente")
fr=re.findall(r'"([^"]*)"',m.group(1))
ok(len(fr)==387 and all(fr[i] for i in range(1,387)), "386 noms français non vides")

# Battle UI
ok("PAL[0]" not in battle_bases and "RLE[0]" not in battle_bases,
   "aucune palette de sol vide incompatible avec le compilateur")
ok("void drawBattlePmd" in ino and "visibleW" in ino and
   "uint8_t fi=0" in ino and
   "visibleW*target/maxDim" in ino and "visibleH*target/maxDim" in ino and
   "Rééchantillonnage nearest-neighbour" in ino and
   "void drawBattleThumb" in ino and
   "drawBattleThumb(th,battleDex,354,190,84,false,false)" in ino and
   "drawBattleThumb(th,playerDex,110,302,104,true,false)" in ino and
   (ROOT/"tools/audit_battle_sprite_sizes.py").exists() and
   "drawBattlePmd(wildPmd, battleDex, 354, 190, 84" in ino and
   "drawBattlePmd(pmd, playerDex, 110, 302, 104" in ino,
   "sprites PMD et miniatures de combat normalises")
ok("drawBattleStatusBar(enemyName,battleLevel,enemySex,82,66,190" in ino and
   "battlePlayer.level,playerSex,220,234,220" in ino,
   "informations combat opposees aux sprites")
ok("if(type1==TYPE_FIRE||type2==TYPE_FIRE) return 0" in ino,
   "Pokemon Feu places sur le sol herbe")
steel_species=[int(n) for n in re.findall(r'^\s*\{[^\n]*TYPE_STEEL[^\n]*\},\s*//\s*(\d+)',dex,re.M)]
ground_rules=re.search(r'uint8_t battleGroundStyleFor\([^)]*\) \{(.*?)\n\}',ino,re.S)
ok(len(steel_species)==15 and {81,82,205,208,212,227,303,304,305,306,374,375,376,379,385}==set(steel_species) and
   ground_rules is not None and
   ground_rules.group(1).index('if(type1==TYPE_STEEL||type2==TYPE_STEEL) return 0;') <
   ground_rules.group(1).index('if(type1==TYPE_GROUND||type2==TYPE_GROUND) return 2;') <
   ground_rules.group(1).index('if(type1==TYPE_ROCK||type2==TYPE_ROCK) return 4;'),
   "15 Pokemon Acier sur sol herbeux, Roche seul conserve son sol")
ok("void drawBattleCaughtBall(int cx, int cy, int outW=27, int outH=29)" in ino and
   "drawBattleCaughtBall(102,137)" in ino and
   "buttons[i].cy + buttons[i].iconDy, 29, 31" in ino,
   "Pokeball pixel art transparente en combat et sur le bouton d'accueil")
boxbg=(ROOT/"box_backgrounds.h").read_text(encoding="utf-8")
ok("BOX_BG_COUNT=16" in boxbg and
   len(list((ROOT/"assets/box_backgrounds").glob("box-*.png")))==16,
   "planche decoupee en 16 fonds de Boite")
ok("renderBoxBackgroundSettings" in ino and "boxBgPreview" in ino and
   "setBoxBackground(boxBgPreview)" in ino and
   "drawBoxBackgroundAsset(pet.boxBackground,-67,-20,4)" in ino,
   "selection, apercu, validation et affichage du fond de Boite")
ok("pet.boxBackground=boxBgPreview" in ino and "cardDirty=true" in ino,
   "fond de Boite applique immediatement sans redemarrage")
ok("Mode cover : 600x432" in ino and "-67,-20,4" in ino,
   "fond de Boite debordant sans marges haut gauche droite")
ok('snprintf(known' not in ino and 'snprintf(goal' not in ino and
   'gfx->fillRoundRect(302, 62, 106, 28' not in ino,
   "Connus, But Dex et bouton de tri DEX supprimes de la Boite")
ok("x = 84 + col * 78, y = 112 + row * 74" in ino and
   "gfx->fillRoundRect(x, y, 64, 64" in ino and
   "gfx->fillRoundRect(76, 306, 94, 38" in ino and
   "gfx->fillRect(0,350,466,116" not in ino,
   "grille Boite reduite et pagination sur le decor")
ok("gfx->fillRect(0,350,466,116" not in ino and "gfx->fillRect(0,355,466,111,uiBg())" in ino and "gfx->fillRect(0,350,466,5,UI_INK)" in ino and
   "int navY = 360" in ino,
   "ligne fine et zone navigation sur fond clair/sombre du theme")
ok("gfx->fillRoundRect(x, y, 64, 64, 10" in ino and
   "gfx->fillRoundRect(108,24,250,72,15" in ino and
   "y >= 112 + row * 74 && y <= 176 + row * 74" in ino,
   "cadre titre agrandi, grille descendue et tactile synchronise")
ok('prefs.putUChar("boxbg", boxBackground)' in pet and
   'prefs.getUChar("boxbg", 0)' in pet,
   "fond de Boite memorise apres redemarrage")
ok("int16_t boxFavorites[2][8]" in pet_h and
   'prefs.putBytes("boxfav1"' in pet and 'prefs.putBytes("boxfav2"' in pet and
   'prefs.getBytes("boxfav1"' in pet and 'prefs.getBytes("boxfav2"' in pet and
   "toggleBoxFavorite" in pet and "boxFavoriteView" in ino and
   'gfx->print("FAVORI 1")' in ino and 'gfx->print("FAVORI 2")' in ino and
   "boxDisplayedDexAt" in ino,
   "deux equipes favorites de huit Pokemon selectionnables et persistantes")
ok("void drawHomeGymCabin(uint8_t phase, uint8_t biome)" in ino and
   "drawHomeGymCabin(phase,b)" in ino and
   '#include "home_gym_sprite.h"' in ino and
   "HOME_GYM_PIXELS" in ino and "HOME_GYM_PALETTE" in ino and
   (ROOT/"home_gym_sprite.h").exists() and
   "x >= 318 && x <= 446 && y >= 128 && y <= 244" in ino and
   "const int x=318, y=132" in ino and
   "Toit rouge étagé, large et immédiatement lisible" in ino and
   "Grand emblème Poké Ball centré dans le fronton" in ino and
   "Deux colonnes épaisses encadrent les doubles portes" in ino and
   "cardPage = 9" in ino and "kantoArenaDetail = -1" in ino,
   "arene Pokemon pixel art tactile ouvrant directement les arenes Kanto")
ok('#include "home_habitat_sprites.h"' in ino and
   "void drawHomeHabitatGround(uint8_t biome)" in ino and
   "drawHomeHabitatGround(b)" in ino and
   "const int16_t x0=49, y0=236" in ino and
   "if (color)" in ino and
   "HOME_HABITAT_PIXELS" in ino and "HOME_HABITAT_PALETTES" in ino and
   (ROOT/"home_habitat_sprites.h").exists() and
   all((ROOT/"assets/home_habitats_v10_19"/f"{name}.png").exists()
       for name in ("meadow","water","forest","volcano","mountain","snow")),
   "six sols pixel art detailles integres sur l'accueil selon le biome")
ok("STARTER_DEX[3][3]" in ino and "{ 252, 255, 258 }" in ino,
   "starters 1G, 2G et 3G presents")
ok("#define SPRITE_AUDIT_BUILD 0" in ino and
   "#define POKETAMA_UNLOCK_ALL_386 1" not in pet_h and
   "#define POKETAMA_UNLOCK_ALL_386 0" in pet_h and
   'gfx->print("JOUEUR")' not in ino and 'gfx->print("ADVERSAIRE")' not in ino,
   "mode test 386 retire sans effacer les Pokemon obtenus")
layout=(ROOT/"battle_sprite_layout.h").read_text(encoding="utf-8")
ok("BattleSpriteLayout BATTLE_SPRITE_LAYOUT[386]" in layout and
   "uint8_t scale" in layout and "playerScale" not in layout and "enemyScale" not in layout and
   ino.count("pokemonSpriteScale(dex)") == 4,
   "taille individuelle unique pour 386 Pokemon sur choix accueil fiche et combat")
ok("if (dex == 144 || dex == 145 || dex == 146 || dex == 248 || dex == 382)" in layout and
   "BattleSpriteLayout enlarged130 = {130," in layout and
   "if ((dex == 144 || dex == 145 || dex == 146 || dex == 248 || dex == 382) && visibleMax > 0)" in ino and
   "drawW = max(1, a.w * targetDim / visibleMax)" in ino,
   "cinq Pokemon dont les oiseaux legendaires a 130 pour cent sans blocage PMD")
ok("spriteSizePercent" not in ino and "battleAuditScalePercent" not in ino and
   "pokemonVisualScalePercent" not in ino and "pokemon_visual_scale.h" not in ino,
   "anciens coefficients et exceptions de taille entierement retires")
workflow=(ROOT/".github/workflows/pages.yml").read_text(encoding="utf-8")
build_web=(ROOT/"tools/build_web.sh").read_text(encoding="utf-8")
ok("python3 \"$ROOT/tools/pack_pmd.py\" $(seq 1 386)" in build_web and
   "poketama-pmd-original-386-v3" in workflow and "restore-keys" not in workflow,
   "386 formes normales et Shiny rechargees depuis les sources originales")
ok("int8_t forcedShiny = -1" not in ino and
   "startBattleWith(wildPromptDex, wildPromptLevel, -1)" in ino and
   "startBattleWith(0, 0, -1)" in ino,
   "prototype Arduino sans argument par defaut duplique")
ok('"..........kkk..."' in (ROOT/"species.h").read_text(encoding="utf-8") and
   '".....kkkkk.k...."' in (ROOT/"species.h").read_text(encoding="utf-8") and
   '"...........lLk.."' in (ROOT/"species.h").read_text(encoding="utf-8"),
   "nouvelle planche de baies et super bonbon integree en 16x16")
ok("drawStarterPokeball" in ino and "starterPreviewDex" in ino,
   "Pokeballs et popup de confirmation starter presentes")
ok("repairCaughtProfiles" in pet and "hasStoredProfile(candidate)" in pet,
   "anciens profils evolues restaures dans la Boite")
ok(all(name in audio for name in ("OST_MORNING", "OST_LOFI", "OST_NIGHT")) and
   "AUDIO_EVENT_AMBIENT" in audio and "audioAmbientPlay" in ino,
   "trois OST originales et lecture ambiante non bloquante")
ok("hour>=7 && hour<13" in ino and "hour>=13 && hour<20" in ino,
   "selection OST automatique matin, lo-fi et nuit")
music=ROOT/"tools/sdcard/music"
for filename in ("morning.wav", "lofi.wav", "night.wav"):
    with wave.open(str(music/filename), "rb") as wav:
        ok(wav.getnchannels()==1 and wav.getsampwidth()==2 and
           wav.getframerate()==16000 and wav.getnframes()==384000,
           f"{filename} PCM mono 16 bits, 16 kHz, 24 secondes")
ok("SD_MMC.open(MUSIC_PATHS[theme]" in audio and
   "SAMPLE_RATE * 240 / 1000" in audio,
   "lecture WAV SD en blocs courts avec repli synthetise")
ok("gAmbientKeepAliveUntil" in audio and "uxQueueMessagesWaiting(gQ) > 0" in audio and
   "musicGainPct() / 100" in audio and "case SOUND_FULL: return 135" in audio and "ps_malloc(bytes)" in audio,
   "OST WAV continue, sans coupure d'ampli entre blocs et sans saturation")
ok("SD_MMC.exists(path) && !SD_MMC.remove(path)" in sdmon,
   "chargement USB remplace les fichiers SD sans les agrandir")
ok('line == "PREPARE"' in sdmon and 'Serial.println("READY")' not in sdmon and
   '"READY" : "ERRCLEAN"' in sdmon,
   "preparation USB nettoie les anciens assets avant transfert")
ok("maintainTamaPokeSd()" in sdmon and "logicalTpk2Size" in sdmon and
   "logical < physical) SD_MMC.remove(path)" in sdmon,
   "maintenance SD supprime les intrus et anciens TPK2 dupliques")
ok('Serial.println("ERRFULL")' in sdmon and "freeBytes" in sdmon,
   "reserve d'espace controlee avant chaque ecriture SD")
ok("ARRÊT SÉCURISÉ" in ino or "ARRÊT SÉCURISÉ" in (ROOT/"web/index.html").read_text(encoding="utf-8"),
   "installateur web stoppe des la premiere erreur")
ok("void drawBattleName" in ino, "noms combat auto-ajustés")
ok("drawBattleHpInfo" in ino, "PV courant/max affichés")
pet_h=(ROOT/"pet.h").read_text(encoding="utf-8")
ok("#define MAX_LEVEL 100" in pet_h and
   "calculated > MAX_LEVEL ? MAX_LEVEL" in pet_h and
   "if (maxLevel) snprintf(nx, sizeof(nx), \"MAX\")" in ino,
   "niveau reel plafonne a 100 et page Progres adaptee")
ok("gfx->fillRect(0, 320, 466, 146" in ino and
   "gfx->fillRect(0,318,466,9,UI_INK)" in ino and
   "drawBattleButtonLabel(240,399,158,T(S_RUN_BATTLE))" in ino,
   "bandeau combat sans filet clair, barre noire et menu 2x2")
ok("gfx->setCursor(msgX,306); gfx->print(battleMsg);" in ino and
   "int msgX=396-msgW;" in ino and "if (msgX < 238) msgX=238;" in ino and
   "x >= 40 && x <= 232 && y >= 326 && y <= 381" in ino and
   "x >= 233 && x <= 426 && y >= 382 && y <= 452" in ino,
   "effets de combat remontes a droite et quatre grandes zones tactiles")
ok('#include "battle_bases.h"' in ino and
   "drawBattleBaseFor(battleGroundStyleFor(foe.type1,foe.type2),true)" in ino and
   "drawBattleBaseFor(battleGroundStyleFor(mine.type1,mine.type2),false)" in ino and
   "gfx->fillRect(-23+sx*2,102+sy*2,take*2,2,color)" in ino,
   "sol de chaque Pokemon choisi independamment selon ses types")
ok("paste_on_fixed_baseline(scene, enemy, 128, 48)" in (ROOT/"tools/make_battle_bases.py").read_text(encoding="utf-8") and
   "paste_on_fixed_baseline(scene, player, 0, 108)" in (ROOT/"tools/make_battle_bases.py").read_text(encoding="utf-8"),
   "sols joueur et adversaire verrouilles sur une ligne de base fixe")
ground_generator=(ROOT/"tools/make_battle_bases.py").read_text(encoding="utf-8")
ok('("HERBE",   0, 0, 0)' in ground_generator and
   '("EAU",     1, 1, 0)' in ground_generator and
   '("SABLE",   2, 2, 0)' in ground_generator and
   '("VOLCAN", 11, 3, 2)' in ground_generator and
   '("ROCHE",   4, 0, 1)' in ground_generator and
   '("NEIGE",   5, 1, 1)' in ground_generator and
   "enemy_y = 16 + enemy_row * 72" in ground_generator and
   (ROOT/"tools/audit_battle_grounds.py").exists(),
   "six sols semantiques et ovales adverses audites")
ok('#include "battle_backgrounds.h"' in ino and
   "drawBattleBackgroundBiome(biome)" in ino and
   "sourcePhase=(phase==3)?2:(phase==1?0:1)" in ino and
   "(uint32_t)(sy+1)*320/BATTLE_BG_H" in ino and
   "gfx->fillRect(-23+sx*2,dy,take*2,dy2-dy,color)" in ino,
   "18 fonds DP plein cadre par habitat et moment de la journee")
ok("void drawBattleStatusBar" in ino and
   "void drawBattleSex" in ino and
   "battleSpeciesGenderless" in ino and
   "drawBattleStatusBar(enemyName,battleLevel,enemySex,82,66,190" in ino and
   "drawBattleStatusBar(pet.nick[0]?pet.nick:dexName(pet.speciesId),battlePlayer.level,playerSex,220,234,220" in ino and
   "(phase==3||biome==2||biome==3)?UI_WHITE:UI_INK" in ino,
   "barres HP DS sans EXP, nom niveau sexe dans zone sure ronde")
ok('#include "battle_ball_icon.h"' in ino and "BATTLE_BALL_MASK" in ino and
   (ROOT/"battle_ball_icon.h").exists(), "Pokeball pixel art transparente embarquee")
ok("T(S_QUICK_ATTACK)" in ino and "T(S_NORMAL_ATTACK)" in ino and "T(S_HEAVY_ATTACK)" in ino, "menu attaques traduit")
ok("int16_t pickWildSpecies(uint32_t roll)" in battle and
   "rarityRoll < 55" in battle and "rarityRoll < 80" in battle and "rarityRoll < 98" in battle and
   "generation = (uint8_t)(x % 3)" in battle,
   "rencontres variees sur les 386 Pokemon et les trois generations")
ok("(random(128)==0)" in ino and "battleEnemyShiny=(forcedShiny >= 0)" in ino and
   "wildPmd.load(battleDex, battleEnemyShiny)" in ino and
   "drawBattleShinyEntrance(354,146)" in ino and
   "registerCaught(wildDex, wildShiny)" in pet,
   "Shiny sauvages 1/128, animation et capture memorisee")
ok("BATTLE_ATTACK_QUICK" in ino and "BATTLE_ATTACK_HEAVY" in ino, "menu relié au moteur réel")

# HP / damage exact
ok("if (turn.playerDamage > battle.enemyHp) turn.playerDamage = battle.enemyHp;" in battle,
   "dégâts joueur = PV réellement retirés")
ok("turn.enemyDamage = enemyHit > battle.playerHp ? battle.playerHp : enemyHit;" in battle,
   "dégâts ennemi = PV réellement retirés")
ok("hpLeft -= dealt;" in battle, "aucun PV négatif")
ok("battle.playerHp += turn.playerHeal;" in battle, "soin borné par PV max")

# Chirps 386
ok("clampPitch(160 + dex * 6)" in chirp, "cris synthétiques 001-386 dans plage sûre")

# Background SD assets
bg=ROOT/"tools/sdcard/backgrounds"
pngs=list(bg.glob("*_466.png"))
ok(len(pngs)==24, "24 fonds SD (6 habitats x 4 phases)")
rows=list(csv.reader((bg/"habitats_001_386.csv").open(encoding="utf-8-sig"),delimiter=";"))
ok(len(rows)==387, "mapping habitat SD pour 386 Pokémon")

# Existing audits syntax / shell
for script in ["audit_evolutions.py","audit_386_assets.py","verify_web.py"]:
    p=ROOT/"tools"/script
    ok(p.exists(), f"{script} présent")
    subprocess.run([sys.executable,"-m","py_compile",str(p)],check=True)
if shutil.which("bash"):
    ok(subprocess.run(["bash","-n",str(ROOT/"tools/build_web.sh")]).returncode==0, "build_web.sh syntaxe")
else:
    print("SKIP build_web.sh syntaxe (bash absent sur ce poste)")

print("AUDIT FINAL V9 OK")
