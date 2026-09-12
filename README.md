# PokeTama Moretro3D

Firmware pour le PokeTama Moretro3D sur Waveshare ESP32-S3 Touch AMOLED 1.75.

## Installation

Ouvre la page GitHub Pages du depot **Moretro3D/Test-1** avec Chrome ou Edge sur ordinateur.

1. Branche le PokeTama en USB-C.
2. Clique sur **Installer le firmware Moretro3D**.
3. Pour une mise a jour, conserve les donnees afin de garder ton Pokemon et ta sauvegarde.
4. Utilise **Tout installer sur la microSD** seulement pour installer ou reparer les sprites, decors et musiques.

## Premier demarrage

Choisis librement une generation :

- 1G : Bulbizarre, Salameche ou Carapuce ;
- 2G : Germignon, Hericendre ou Kaiminus ;
- 3G : Arcko, Poussifeu ou Gobou.

Les noms suivent la langue selectionnee. Plante est affiche en vert, Feu en rouge et Eau en bleu.

## Version V9.43

- Nouvelle page `shopify.html` dédiée à l'intégration dans Moretro3D.fr.
- Contenu central uniquement : aucun header ni footer ajouté par PokeTama.
- Custom Liquid Shopify fourni avec hauteur responsive automatique.
- Page GitHub normale conservée telle quelle.

- Emojis des langues remplacés par six véritables drapeaux SVG locaux.
- Affichage des langues indépendant des polices emoji de Chrome et Google.
- Drapeaux nets sur ordinateur et mobile avec état actif visible.

- Visuel du site limité à la partie affichage circulaire, sans appareil ni main.
- Ectoplasma affiché au niveau 75 avec les quatre jauges entièrement vertes.
- Contour extérieur rendu transparent sans toucher aux éléments blancs de l'écran.

- Page Web traduite intégralement en français, anglais, espagnol, allemand,
  italien et portugais avec six drapeaux de sélection.
- Langue mémorisée dans le navigateur et messages USB traduits en direct.
- Véritable photo du PokeTama intégrée avec Ectoplasma affiché à l'écran.

- Nouvelle page d'installation sombre aux couleurs MoRetro3D.
- Présentation claire en deux cartes : firmware puis préparation de la microSD.
- Logo officiel, aperçu rond du PokeTama et mise en page responsive téléphone.
- Boutons USB et journal de transfert conservés avec leurs identifiants d'origine.

- Taille des sprites animes de l'accueil uniformisee sur leur silhouette visible.
- La largeur et la hauteur sont maintenant prises en compte : Kaiminus et les
  autres Pokemon larges ne paraissent plus surdimensionnes.
- Meme regle automatique pour les 386 Pokemon, sans correction fragile par espece.

- Suppression du symbole/cadre en haut a gauche de toutes les sous-pages Reglages.
- Suppression des zones tactiles invisibles correspondantes.
- Navigation uniquement avec OK, Valider ou Annuler en bas de page.

- Page Heure descendue et alignee sur une grille symetrique.
- Tous les textes de l'heure sont noirs en mode clair et blancs en mode sombre.
- Langue et economie d'energie utilisent un gris fonce lisible en mode clair.
- Zones tactiles recalees sur les nouvelles positions.

- Reglages reorganises en menu compact avec chevrons, inspire d'iOS.
- Sous-pages dediees : Heure, Affichage, Son et Reinitialisation.
- Affichage regroupe mode sombre, cadres, langue et economie d'energie.
- Remise a zero securisee sur une page rouge dediee, sans toucher a la microSD.
- Trois nouveaux textes traduits dans les six langues.

- Ecran de demarrage recentre optiquement sur le disque 466x466.
- Logo place a Y=96, PokeTama a Y=284 et Moretro3D a Y=326.
- Affichage prolonge de 1,5 seconde apres l'initialisation, sans frame noire.

- Chaque bouton d'accueil possede maintenant sa position, son cadre, sa taille de sprite, son decalage et sa zone tactile.
- La Poke Ball est reglee independamment a 28 px ; les autres icones restent a 32 px.
- Les tailles ne sont plus limitees aux multiples 16/32/48 px.

- La Poke Ball de l'accueil est reduite de 48 px a 32 px et recentree dans son cadre de 52 px.
- Le sprite reste strictement identique a celui du choix des starters.

- L'accueil utilise desormais exactement la meme Poke Ball que le choix du starter, en 48 px.
- L'ancien sprite REC_BALL n'est plus utilise pour le bouton Jouer.
- Le coeur est redessine comme une icone d'affection Pokemon compacte, avec reflet et ombre.

- La vraie Poke Ball pixel-art est affichee sur l'accueil a cote des baies et dans le choix du starter.
- Le coeur de reaction a ete entierement redessine en pixel art 32x32 : contour net, creux central, reflet et ombre, sans effet Mickey.

- nouvelle Poke Ball 16x16 entierement redessinee en pixel-art ;
- coque rouge ombree, reflet, separation centrale et bouton blanc ;
- rendu inspire des interfaces GBA et integre directement au firmware.

- nouvelles baies rouge, bleue et verte en pixel-art inspire de l'esthetique GBA ;
- silhouettes et couleurs distinctes pour une lecture immediate ;
- nouveau Super Bonbon avec emballage bleu et coeur dore ;
- icones integrees au firmware, sans rechargement de la microSD.

- navigation de la page Aide remontee dans la zone circulaire sure ;
- Boite transformee en grille 4x2 de mini-sprites captures ;
- noms retires de la liste pour un rendu plus visuel ;
- pagination interne de la Boite remontee et Retour aligne avec les autres fiches.

- interface des fiches recomposee pour l'ecran rond 1,75 pouce ;
- Pokemon anime de l'accueil reduit a la taille du portrait de profil ;
- page Records separee de la page Caractere ;
- Quotidien en texte blanc en mode sombre ;
- PDS retire de la page Combat et contenu remonte ;
- Retour et fleches remontes sur Profil, Caractere, Quotidien, Combat, Medailles, Progres, Expedition et Records.

- page Quotidien lisible en noir et recompense unique apres les trois objectifs ;
- textes des medailles centres ;
- page Progres sans negligences et texte d'evolution blanc en mode sombre ;
- cadre du profil recentre sur la silhouette du Pokemon ;
- aucun depart ni abandon cause par un manque de soins ;
- navigation Retour et fleches remontee sur Quotidien, Medailles et Progres.

- une seule configuration de taille pour les 386 Pokemon ;
- sprites uniformement reduits et centres sur leur silhouette pour un rendu plus propre.

- petites billes de pagination supprimees sur les fiches coulissantes ;
- fleches gauche et droite propres, visibles et tactiles de chaque cote du bouton Retour.

- cadence d'affichage securisee pendant le lavage, le repas et les interactions sur l'accueil ;
- suppression du chevauchement entre deux transferts complets vers l'AMOLED afin d'eviter les petits flashs noirs.

- 386 Pokemon et leurs evolutions ;
- six langues : francais, anglais, espagnol, allemand, italien et portugais ;
- musique depuis la microSD ;
- demarrage et reveil PWR optimises ;
- rendu AMOLED sans flash noir ;
- combats, boite, Pokedex, expeditions et mini-jeux.

Projet de fan gratuit et non commercial. Pokemon appartient a Nintendo, Game Freak et The Pokemon Company. Les sprites PMD proviennent de PMD SpriteCollab selon leurs conditions de licence.
