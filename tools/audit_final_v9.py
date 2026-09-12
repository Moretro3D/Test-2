#!/usr/bin/env python3
from pathlib import Path
import re, subprocess, sys, csv, wave, shutil

ROOT=Path(__file__).resolve().parents[1]
ino=(ROOT/"TamaPoke.ino").read_text(encoding="utf-8")
dex=(ROOT/"dex.h").read_text(encoding="utf-8")
battle=(ROOT/"battle.cpp").read_text(encoding="utf-8")
battle_bases=(ROOT/"battle_bases.h").read_text(encoding="utf-8")
pet=(ROOT/"pet.cpp").read_text(encoding="utf-8")
audio=(ROOT/"audio.cpp").read_text(encoding="utf-8")
chirp=(ROOT/"species_chirp.cpp").read_text(encoding="utf-8")
sdmon=(ROOT/"sdmon.cpp").read_text(encoding="utf-8")

def ok(cond,msg):
    if not cond:
        raise SystemExit("FAIL V9: "+msg)
    print("OK  ",msg)

# 466x466 / UI 1.75"
ok("#define CX 233" in ino and "#define CY 233" in ino, "centre écran 466x466")
ok("#define GAL_CELL 72" in ino and "#define GAL_X 89" in ino, "Pokédex compact dans le rond")

# Pokedex
ok("Pagination compacte" not in ino, "billes pagination Pokédex supprimées")
ok('gfx->fillRoundRect(18, 214, 48, 52' in ino, "flèche gauche Pokédex centrée")
ok('gfx->fillRoundRect(400, 214, 48, 52' in ino, "flèche droite Pokédex centrée")
ok('gfx->fillRoundRect(136, 398, 194, 40' in ino, "bouton RETOUR Pokédex remonté")
ok("galleryPage--" in ino and "galleryPage++" in ino, "navigation tactile flèches Pokédex")

# Fiche
ok('const char *cardBack=T(S_LAN_BACK)' in ino, "bouton RETOUR fiche traduit")
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
   "drawBattleThumb(th,battleDex,354,190,84,false)" in ino and
   "drawBattleThumb(th,pet.speciesId,110,302,104,false)" in ino and
   (ROOT/"tools/audit_battle_sprite_sizes.py").exists() and
   "drawBattlePmd(wildPmd, battleDex, 354, 190, 84" in ino and
   "drawBattlePmd(pmd, pet.speciesId, 110, 302, 104" in ino,
   "sprites PMD et miniatures de combat normalises")
ok("drawBattleStatusBar(enemyName,battleLevel,enemySex,82,66,190" in ino and
   "battlePlayer.level,playerSex,220,234,220" in ino,
   "informations combat opposees aux sprites")
ok("if(type1==TYPE_FIRE||type2==TYPE_FIRE) return 0" in ino,
   "Pokemon Feu places sur le sol herbe")
ok("constexpr int outW=27, outH=29" in ino and
   "drawBattleCaughtBall(102,137)" in ino,
   "Pokeball de Pokemon deja capture reduite uniquement en combat")
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
ok("STARTER_DEX[3][3]" in ino and "{ 252, 255, 258 }" in ino,
   "starters 1G, 2G et 3G presents")
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
   "gfx->drawFastHLine(0,320,466" in ino and
   "drawBattleButtonLabel(240,396,154,T(S_RUN_BATTLE))" in ino,
   "bandeau combat classique pleine largeur et menu 2x2")
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
ok("battleEnemyShiny=(random(128)==0)" in ino and
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
