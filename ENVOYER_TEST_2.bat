@echo off
setlocal EnableExtensions
title PokeTama V9.98 - Envoi simple vers Test-2
cd /d "%~dp0"

echo PokeTama V9.98 - envoi vers Moretro3D/Test-2
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

echo Verification de la version presente sur GitHub...
git -c safe.directory="%CD%" -c http.sslBackend=openssl fetch origin main
if errorlevel 1 goto :connexion
git -c safe.directory="%CD%" merge-base --is-ancestor origin/main HEAD
if errorlevel 1 (
  echo ARRET : Test-2 contient de nouveaux changements. Aucun fichier n'a ete ecrase.
  pause
  exit /b 1
)

echo Envoi de la V9.98 sans effacement ni force push...
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
