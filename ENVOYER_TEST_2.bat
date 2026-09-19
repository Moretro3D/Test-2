@echo off
setlocal EnableExtensions
title PokeTama V10.01 - Envoi simple vers Test-2
cd /d "%~dp0"

echo PokeTama V10.01 - envoi vers Moretro3D/Test-2
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

git -c safe.directory="%CD%" remote get-url origin 2>nul | findstr /I /L https://github.com/Moretro3D/Test-2.git >nul
if errorlevel 1 (
  echo ARRET : ce dossier ne pointe pas vers Moretro3D/Test-2.
  pause
  exit /b 1
)

echo Preparation des fichiers V10.01...
git -c safe.directory="%CD%" add TamaPoke.ino tools/build_web.sh tools/audit_final_v9.py web/index.html web/manifest.json ENVOYER_TEST_2.bat
git -c safe.directory="%CD%" diff --cached --quiet
if errorlevel 1 (
  git -c safe.directory="%CD%" commit -m "PokeTama V10.01 - supprime le filet du combat"
  if errorlevel 1 goto :connexion
)

echo Synchronisation securisee avec Test-2...
git -c safe.directory="%CD%" -c http.sslBackend=openssl fetch origin main
if errorlevel 1 goto :connexion
git -c safe.directory="%CD%" merge-base --is-ancestor origin/main HEAD
if errorlevel 1 (
  echo Rattachement de la V10 a la version deja presente sur GitHub...
  git -c safe.directory="%CD%" merge -s ours --no-edit origin/main -m "Rattache PokeTama V10 a Test-2"
  if errorlevel 1 goto :connexion
)

echo Envoi de la V10.01 sans effacement ni force push...
git -c safe.directory="%CD%" -c http.sslBackend=openssl push origin HEAD:main
if errorlevel 1 goto :connexion
echo.
echo TERMINE : ouvre https://github.com/Moretro3D/Test-2/actions
pause
exit /b 0

:connexion
echo.
echo ECHEC : connecte-toi a GitHub si une fenetre de connexion s'affiche.
echo Aucun force push ni effacement n'a ete effectue. Relance ensuite ce fichier.
pause
exit /b 1
