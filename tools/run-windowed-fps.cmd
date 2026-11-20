@echo off
setlocal
set "PS3_FPS_TITLE=1"
set "PS3_FRAME_GEN=fsr3"
echo FSR 3.1 experimental. La interpolacion se activa en combates compatibles.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run.ps1" -WindowMode Windowed %*
exit /b %ERRORLEVEL%
