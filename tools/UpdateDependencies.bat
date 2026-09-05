@echo off
setlocal

rem This BAT lives in tools\. Resolve the project root explicitly.
set "TOOLS_DIR=%~dp0"
for %%I in ("%TOOLS_DIR%..") do set "PROJECT_ROOT=%%~fI"

cd /d "%PROJECT_ROOT%"

echo Updating OmniGhost DMA dependencies (MemProcFS + LeechCore)...
powershell -NoProfile -ExecutionPolicy Bypass -File "%TOOLS_DIR%Update-DmaDependencies.ps1" -Latest
if errorlevel 1 (
  echo.
  echo Update failed. See "%PROJECT_ROOT%\logs\dependency-update-*.log"
  pause
  exit /b 1
)

echo.
echo Done. Rebuild OmniGhost Release x64 after updating.
pause
