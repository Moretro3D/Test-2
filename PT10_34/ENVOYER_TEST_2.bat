@echo off
setlocal EnableExtensions
title PokeTama V10.34 - Envoi vers Test-2
cd /d "%~dp0"

echo PokeTama V10.34 - envoi vers Moretro3D/Test-2
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
git -c safe.directory="%CD%" remote add origin https://github.com/Moretro3D/Test-2.git

echo Recuperation securisee de Test-2...
git -c safe.directory="%CD%" -c http.sslBackend=openssl fetch origin main
if errorlevel 1 goto :connexion
git -c safe.directory="%CD%" reset --mixed origin/main
if errorlevel 1 goto :connexion

echo Preparation des fichiers V10.34...
git -c safe.directory="%CD%" add -A
git -c safe.directory="%CD%" diff --cached --quiet
if errorlevel 1 (
  git -c safe.directory="%CD%" commit -m "PokeTama V10.34 - miniatures Boite et boutons uniformes"
  if errorlevel 1 goto :connexion
)

echo Envoi de la V10.34 sans effacement ni force push...
git -c safe.directory="%CD%" -c http.sslBackend=openssl push -u origin main
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
