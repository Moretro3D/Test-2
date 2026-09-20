@echo off
chcp 65001 >nul
title PokeTama Moretro3D V9.61 - Envoi GitHub
cd /d "%~dp0"

echo ============================================================
echo   PokeTama Moretro3D V9.61 - Publication Test-1
echo ============================================================
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo [ERREUR] Git for Windows n'est pas installe.
  pause
  exit /b 1
)

findstr /C:"poketama-path-change" "web\configurator.js" >nul 2>nul
if errorlevel 1 (
  echo [ERREUR] Le configurateur V9.61 est absent de ce dossier.
  pause
  exit /b 1
)
set "POKETAMA_SAFE_DIR=%CD:\=/%"
git config --global --add safe.directory "%POKETAMA_SAFE_DIR%"
git config --global user.name "Moretro3D"
git config --global user.email "morgan.duncas@gmail.com"

if not exist ".git" git init
git branch -M main
git remote remove origin >nul 2>nul
git remote add origin https://github.com/Moretro3D/Test-1.git

echo [1/4] Recuperation du depot distant...
git fetch origin main
if errorlevel 1 goto :fail
git reset --mixed origin/main

echo [2/4] Preparation des fichiers V9.61...
git add -A

echo [3/4] Creation du commit...
git commit -m "PokeTama V9.61 configurateur public"
if errorlevel 1 echo Aucun nouveau changement a committer, verification du push...

echo [4/4] Publication sur GitHub...
git push -u origin main
if errorlevel 1 goto :fail

git fetch origin main >nul 2>nul
git show origin/main:web/configurator.js | findstr /C:"poketama-path-change" >nul 2>nul
if errorlevel 1 goto :fail
echo.
echo ============================================================
echo PUBLICATION TERMINEE - V9.61 VERIFIEE SUR GITHUB
echo ============================================================
start "" "https://github.com/Moretro3D/Test-1/actions"
pause
exit /b 0

:fail
echo.
echo [ERREUR] La V9.61 n'a pas pu etre publiee ou verifiee.
echo Connecte-toi a GitHub si une fenetre d'authentification apparait,
echo puis relance ce fichier.
pause
exit /b 1
