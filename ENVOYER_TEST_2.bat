@echo off
setlocal EnableExtensions EnableDelayedExpansion
title PokeTama V9.88 - Envoi vers Test-2 uniquement
cd /d "%~dp0"

set "REPO_URL=https://github.com/Moretro3D/Test-2.git"
echo ============================================================
echo  PokeTama V9.88 - ENVOI SECURISE VERS TEST-2
echo ============================================================
echo Destination unique : %REPO_URL%
echo Ce lanceur ne peut pas envoyer vers Test-1.
echo.

where git >nul 2>nul
if errorlevel 1 (
  echo ERREUR : Git pour Windows n'est pas installe ou pas dans le PATH.
  pause
  exit /b 1
)

git config --global --add safe.directory "%CD%" >nul 2>nul
if not exist ".git" git init

git config user.name >nul 2>nul
if errorlevel 1 git config user.name "MoRetro3D"
git config user.email >nul 2>nul
if errorlevel 1 git config user.email "moretro3d@users.noreply.github.com"

git remote get-url origin >nul 2>nul
if errorlevel 1 (
  git remote add origin "%REPO_URL%"
) else (
  for /f "delims=" %%R in ('git remote get-url origin') do set "CURRENT_REMOTE=%%R"
  if /I not "!CURRENT_REMOTE!"=="%REPO_URL%" git remote set-url origin "%REPO_URL%"
)

for /f "delims=" %%R in ('git remote get-url origin') do set "FINAL_REMOTE=%%R"
if /I not "!FINAL_REMOTE!"=="%REPO_URL%" (
  echo ARRET SECURISE : la destination Git ne correspond pas a Test-2.
  pause
  exit /b 1
)

git add -A
git diff --cached --quiet
if errorlevel 1 git commit -m "PokeTama V9.88 - Boite ronde et navigation basse"
git branch -M main

echo.
echo Envoi vers Test-2...
echo Verification de la version deja presente sur Test-2...
git fetch origin main >nul 2>nul
git push --force-with-lease -u origin main
if errorlevel 1 (
  echo.
  echo ECHEC : verifie que Test-2 existe et que tu es connecte a GitHub.
  pause
  exit /b 1
)

echo.
echo ENVOI TERMINE.
echo Ouvre : https://github.com/Moretro3D/Test-2/actions
echo Puis : https://moretro3d.github.io/Test-2/
pause
