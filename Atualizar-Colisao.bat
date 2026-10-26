@echo off
setlocal
cd /d "%~dp0"

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\collision\Update-CollisionData.ps1" %*
set "result=%ERRORLEVEL%"

echo.
if not "%result%"=="0" echo [ERRO] A atualizacao da colisao terminou com o codigo %result%.
if "%result%"=="0" echo [OK] Pacotes de colisao atualizados e validados.
pause
exit /b %result%
