@echo off
setlocal EnableExtensions
title PokeTama V10.31 - Deploiement officiel Test-1
cd /d "%~dp0"

echo PokeTama V10 - deploiement officiel vers Moretro3D/Test-1
echo.
where git >nul 2>nul
if errorlevel 1 (
  echo ERREUR : Git pour Windows est introuvable.
  pause
  exit /b 1
)
set "POKETAMA_SAFE_DIR=%CD:\=/%"
git config --global --add safe.directory "%POKETAMA_SAFE_DIR%"
if errorlevel 1 goto :connexion
if not exist ".git\HEAD" git init
git -c safe.directory="%CD%" branch -M main
git -c safe.directory="%CD%" config user.name >nul 2>nul || git -c safe.directory="%CD%" config user.name "Moretro3D"
git -c safe.directory="%CD%" config user.email >nul 2>nul || git -c safe.directory="%CD%" config user.email "contact@moretro3d.fr"
git -c safe.directory="%CD%" remote remove origin >nul 2>nul
git -c safe.directory="%CD%" remote add origin https://github.com/Moretro3D/Test-1.git

echo Recuperation securisee de Test-1...
git -c safe.directory="%CD%" -c http.sslBackend=openssl fetch origin main
if errorlevel 1 goto :connexion
git -c safe.directory="%CD%" reset --mixed origin/main
if errorlevel 1 goto :connexion

echo Preparation de la version officielle V10.31...
git -c safe.directory="%CD%" add -A
git -c safe.directory="%CD%" diff --cached --quiet
if errorlevel 1 (
  git -c safe.directory="%CD%" commit -m "PokeTama V10.31 - plateforme roche pixel art et combat descendu"
  if errorlevel 1 goto :connexion
)

echo Envoi officiel de la V10 sans effacement ni force push...
git -c safe.directory="%CD%" -c http.sslBackend=openssl push -u origin main
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
