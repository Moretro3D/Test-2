@echo off
setlocal EnableExtensions
title PokeTama V10.15 - Deploiement officiel Test-1
cd /d "%~dp0"

echo PokeTama V10 - deploiement officiel vers Moretro3D/Test-1
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

git -c safe.directory="%CD%" remote set-url origin https://github.com/Moretro3D/Test-1.git
if errorlevel 1 goto :connexion

echo Preparation de la version officielle V10.15...
git -c safe.directory="%CD%" add -A
git -c safe.directory="%CD%" diff --cached --quiet
if errorlevel 1 (
  git -c safe.directory="%CD%" commit -m "PokeTama V10.15 - cabane pixel art vers les arenes"
  if errorlevel 1 goto :connexion
)

echo Synchronisation securisee avec Test-1...
git -c safe.directory="%CD%" -c http.sslBackend=openssl fetch origin main
if errorlevel 1 goto :connexion
git -c safe.directory="%CD%" merge-base --is-ancestor origin/main HEAD
if errorlevel 1 (
  git -c safe.directory="%CD%" merge -s ours --allow-unrelated-histories --no-edit origin/main -m "Rattache PokeTama V10 a Test-1"
  if errorlevel 1 goto :connexion
)

echo Envoi officiel de la V10 sans effacement ni force push...
git -c safe.directory="%CD%" -c http.sslBackend=openssl push origin HEAD:main
if errorlevel 1 goto :connexion
echo.
echo TERMINE : la V10 officielle est envoyee sur Test-1.
echo Ouvre https://github.com/Moretro3D/Test-1/actions pour suivre la compilation.
pause
exit /b 0

:connexion
echo.
echo ECHEC : consulte le message Git affiche juste au-dessus.
echo Aucun force push ni effacement n'a ete effectue.
pause
exit /b 1
