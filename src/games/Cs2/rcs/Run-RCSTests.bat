@echo off
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found.
  exit /b 1
)
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSROOT=%%I"
if not defined VSROOT (
  echo Visual Studio 2022 or newer was not found.
  exit /b 1
)
call "%VSROOT%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0"
cl /nologo /std:c++20 /EHsc /W4 /Fe:rcs_tests.exe rcs_tests.cpp rcs_controller.cpp
if errorlevel 1 (popd & exit /b 1)
rcs_tests.exe
set "RESULT=%ERRORLEVEL%"
del /q rcs_tests.exe rcs_tests.obj rcs_controller.obj >nul 2>nul
popd
exit /b %RESULT%
