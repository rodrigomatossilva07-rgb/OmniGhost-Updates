@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "CONFIG=%~1"
if not defined CONFIG set "CONFIG=Release"

if /I "%CONFIG%"=="clean" goto :clean
if /I not "%CONFIG%"=="Release" if /I not "%CONFIG%"=="Debug" (
  echo Usage: build.bat [Release^|Debug^|clean]
  exit /b 2
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [ERROR] Visual Studio Installer ^(vswhere.exe^) was not found.
  exit /b 1
)

set "MSBUILD="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%I"
if not defined MSBUILD (
  echo [ERROR] MSBuild with the C++ workload was not found.
  exit /b 1
)

echo [FiveM Dumper] Building %CONFIG% x64...
"%MSBUILD%" "%ROOT%FiveMOffsetDumper.sln" /m /t:Build /p:Configuration=%CONFIG% /p:Platform=x64 /v:minimal
if errorlevel 1 exit /b %errorlevel%

echo.
echo [FiveM Dumper] Build completed:
echo   %ROOT%build\%CONFIG%\fivem_offset_dumper.exe
exit /b 0

:clean
if exist "%ROOT%build" rmdir /s /q "%ROOT%build"
echo [FiveM Dumper] Build directory removed.
exit /b 0
