@echo off
setlocal
set "ROOT=%~dp0"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo [OmniGhost] Visual Studio Installer ^(vswhere.exe^) nao encontrado.
    exit /b 1
)

for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -property installationPath`) do set "VSROOT=%%I"
set "MSBUILD=%VSROOT%\MSBuild\Current\Bin\amd64\MSBuild.exe"
if not exist "%MSBUILD%" (
    echo [OmniGhost] MSBuild x64 nao encontrado.
    exit /b 1
)

"%MSBUILD%" "%ROOT%OmiGhost.vcxproj" /p:Configuration=Publish /p:Platform=x64 /v:q
exit /b %ERRORLEVEL%
