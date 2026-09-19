@echo off
setlocal EnableExtensions
title PokeTama V10.08 - Envoi vers Test-2
cd /d "%~dp0"

echo PokeTama V10.08 - envoi vers Moretro3D/Test-2
echo.
where git >nul 2>nul
if errorlevel 1 (
  echo ERREUR : Git pour Windows est introuvable.
  pause
  exit /b 1
)
if not exist ".git\HEAD" (
  echo ERREUR : extrais d'abord le ZIP entier, avec tous ses fichiers.
  pause
  exit /b 1
)

git -c safe.directory="%CD%" remote set-url origin https://github.com/Moretro3D/Test-2.git
if errorlevel 1 goto :connexion

echo Preparation des fichiers V10.08...
git -c safe.directory="%CD%" add -A
git -c safe.directory="%CD%" diff --cached --quiet
if errorlevel 1 (
  git -c safe.directory="%CD%" commit -m "PokeTama V10.08 - badges Kanto HD"
  if errorlevel 1 goto :connexion
)

echo Synchronisation securisee avec Test-2...
git -c safe.directory="%CD%" -c http.sslBackend=openssl fetch origin main
if errorlevel 1 goto :connexion
git -c safe.directory="%CD%" merge-base --is-ancestor origin/main HEAD
if errorlevel 1 (
  echo Rattachement de la V10 a la version deja presente sur GitHub...
  git -c safe.directory="%CD%" merge -s ours --allow-unrelated-histories --no-edit origin/main -m "Rattache PokeTama V10 a Test-2"
  if errorlevel 1 goto :connexion
)

echo Envoi de la V10.08 sans effacement ni force push...
git -c safe.directory="%CD%" -c http.sslBackend=openssl push origin HEAD:main
if errorlevel 1 goto :connexion
echo.
echo TERMINE : ouvre https://github.com/Moretro3D/Test-2/actions
echo Quand l'action est verte, lance OUVRIR_WEB_FLASHER_TEST_2.bat.
pause
exit /b 0

:connexion
echo.
echo ECHEC : consulte le message Git affiche juste au-dessus.
echo Aucun force push ni effacement n'a ete effectue. Relance ensuite ce fichier.
pause
exit /b 1
