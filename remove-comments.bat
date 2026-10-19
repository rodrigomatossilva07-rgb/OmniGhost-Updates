@echo off
setlocal
chcp 65001 >nul
set "PROJECT_ROOT=%~dp0"
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%PROJECT_ROOT%tools\Remove-ProjectComments.ps1" -ProjectDir "%PROJECT_ROOT%." %*
set "EXIT_CODE=%ERRORLEVEL%"
endlocal & exit /b %EXIT_CODE%
