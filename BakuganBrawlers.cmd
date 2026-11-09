@echo off
rem Bakugan: Battle Brawlers - native Windows x64 port.
rem
rem tools\play.ps1 starts the game and records stdout/stderr separately.
rem Double-click this file, or make a shortcut to it.
rem
rem Disc data goes in game\PS3_GAME; settings and pad mapping live in
rem %LOCALAPPDATA%\BakuganRecomp\player.ini.
setlocal
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\play.ps1" -Root "%~dp0."
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
    echo.
    echo El juego termino con codigo %RC%. Logs en %LOCALAPPDATA%\BakuganRecomp.
    pause
)
exit /b %RC%
