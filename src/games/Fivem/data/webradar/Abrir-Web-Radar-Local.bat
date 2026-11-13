@echo off
setlocal
cd /d "%~dp0"
where node.exe >nul 2>nul
if errorlevel 1 (
  echo [OmniGhost] Node.js nao foi encontrado no PATH.
  echo Instala o Node.js LTS e volta a executar este ficheiro.
  pause
  exit /b 1
)
start "" "http://127.0.0.1:8877/#preview-local"
node.exe local_server.js
if errorlevel 1 pause
