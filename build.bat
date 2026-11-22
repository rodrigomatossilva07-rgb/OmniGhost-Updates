@echo off
setlocal EnableExtensions

rem OmniGhost - CMake launcher build
rem Usage: build.bat [Debug^|Release^|Development^|Publish] [clean]

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"

rem Limpar pasta .cache inteira no INICIO (antes de tudo, para build limpo)
echo [OmniGhost] A limpar pasta .cache completa...
if exist "%ROOT%\.cache" rmdir /s /q "%ROOT%\.cache" >nul 2>&1

if not "%~1"=="" goto :arguments

:menu
cls
echo.
echo   OMNIGHOST - CMAKE BUILD
echo.
echo   [1] Release
echo   [2] Debug
echo   [3] Development
echo   [4] Publish
echo   [5] Sair
echo.
choice /C 12345 /N /M "Escolhe a configuracao"
if errorlevel 5 exit /b 0
if errorlevel 4 set "CONFIG=Publish" & set "ACTION=" & goto :build
if errorlevel 3 set "CONFIG=Development" & set "ACTION=" & goto :build
if errorlevel 2 set "CONFIG=Debug" & set "ACTION=" & goto :build
if errorlevel 1 set "CONFIG=Release" & set "ACTION=" & goto :build

:arguments
set "CONFIG=%~1"
set "ACTION=%~2"

:build

for %%C in (Debug Release Development Publish) do if /I "%CONFIG%"=="%%C" set "VALID_CONFIG=1"
if not defined VALID_CONFIG (
    echo [ERRO] Configuracao invalida: %CONFIG%
    echo Uso: build.bat [Debug^|Release^|Development^|Publish] [clean]
    pause
    exit /b 2
)

set "CMAKE_EXE="
for %%P in (
    "%ProgramFiles%\CMake\bin\cmake.exe"
    "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
) do if not defined CMAKE_EXE if exist "%%~P" set "CMAKE_EXE=%%~P"
if not defined CMAKE_EXE for /f "delims=" %%P in ('where cmake 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%P"
if not defined CMAKE_EXE (
    echo [ERRO] CMake nao foi encontrado.
    echo Instala CMake ou o componente CMake tools do Visual Studio Build Tools.
    echo https://cmake.org/download/
    pause
    exit /b 3
)

set "VCVARS="
for %%P in (
    "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
) do if not defined VCVARS if exist "%%~P" set "VCVARS=%%~P"
if not defined VCVARS (
    echo [ERRO] Nao foi encontrado o compilador C++ x64.
    echo Instala Desktop development with C++ / Visual Studio Build Tools.
    pause
    exit /b 4
)

call "%VCVARS%" x64 >nul 2>&1
if errorlevel 1 (
    echo [ERRO] Nao foi possivel preparar o compilador C++ x64.
    pause
    exit /b 5
)

set "BUILD_DIR=%ROOT%\.cache\cmake\%CONFIG%"
set "LOG_DIR=%ROOT%\.cache\logs"
set "LOG_FILE=%LOG_DIR%\cmake-%CONFIG%.log"
set "OUTPUT_CONFIG=%CONFIG%"
if /I "%CONFIG%"=="Publish" set "OUTPUT_CONFIG=publish"
if /I "%CONFIG%"=="Release" set "OUTPUT_CONFIG=release"
if /I "%CONFIG%"=="Debug" set "OUTPUT_CONFIG=debug"
if /I "%CONFIG%"=="Development" set "OUTPUT_CONFIG=development"
set "LOCK_ROOT=%ROOT%\.cache\locks"
set "LOCK_DIR=%LOCK_ROOT%\build-%CONFIG%.lock"
if not exist "%LOCK_ROOT%" mkdir "%LOCK_ROOT%" >nul 2>&1

rem A Visual Studio build writes shared .tlog files.  A second build of the
rem same configuration corrupts or locks them, so acquire an atomic directory
rem lock before configuring or compiling.
mkdir "%LOCK_DIR%" >nul 2>&1
if errorlevel 1 (
    echo [ERRO] Ja existe uma compilacao %CONFIG% em execucao.
    echo Aguarda que termine antes de abrir outro build.bat para esta configuracao.
    pause
    exit /b 6
)
set "LOCK_HELD=1"

if /I "%ACTION%"=="clean" (
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    rmdir "%LOCK_DIR%" >nul 2>&1
    echo [OK] Build CMake limpa: .cache\cmake\%CONFIG%
    exit /b 0
)

if not exist "%LOG_DIR%" mkdir "%LOG_DIR%" >nul 2>&1
echo [OmniGhost] A configurar CMake (%CONFIG%)...
set "BUILD_PHASE=configure"
"%CMAKE_EXE%" -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 18 2026" -A x64 -DCMAKE_BUILD_TYPE=%CONFIG% > "%LOG_FILE%" 2>&1
if errorlevel 1 goto :failure

echo [OmniGhost] A compilar (%CONFIG%)...
echo [OmniGhost] A usar 8 processos. Os ficheiros compilados aparecem abaixo.
set "BUILD_PHASE=compile"
"%CMAKE_EXE%" --build "%BUILD_DIR%" --target OmniGhost --config %CONFIG% --parallel 8
if errorlevel 1 goto :failure

echo [OK] Compilacao concluida.
echo Executavel: build\%OUTPUT_CONFIG%\OmniGhost.exe
if /I "%CONFIG%"=="Publish" (
    echo [OmniGhost] A preparar e publicar a release stable no GitHub...
    set "BUILD_PHASE=publish"
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\src\project\tools\Publish-Build.ps1" -ProjectDir "%ROOT%" -BuildDir "%ROOT%\build\publish" -ExplicitConfirmed
    if errorlevel 1 goto :failure
    echo [OK] Release stable publicada no GitHub.
)
if /I "%CONFIG%"=="Development" (
    echo [OmniGhost] A preparar a release privada Development...
    set "BUILD_PHASE=development-publish"
    powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\src\project\tools\Publish-Development.ps1" -ProjectDir "%ROOT%" -BuildDir "%ROOT%\build\development"
    if errorlevel 1 goto :failure
    echo [OK] Release Development publicada no canal privado.
)

rem Limpar locks (pasta .cache já foi limpa no inicio)
rmdir "%LOCK_DIR%" >nul 2>&1
exit /b 0

:failure
echo.
if /I "%BUILD_PHASE%"=="configure" (
    echo [ERRO] A configuracao CMake falhou. Detalhes:
    type "%LOG_FILE%"
    echo Log: .cache\logs\cmake-%CONFIG%.log
) else if /I "%BUILD_PHASE%"=="publish" (
    echo [ERRO] A compilacao terminou, mas a publicacao stable no GitHub falhou.
    echo Consulta a saida acima; a release anterior continua intacta.
) else if /I "%BUILD_PHASE%"=="development-publish" (
    echo [ERRO] A compilacao Development terminou, mas a publicacao privada falhou.
    echo Confirma as variaveis Supabase e consulta a saida acima.
) else (
    echo [ERRO] A compilacao falhou. Os erros relevantes estao acima.
    echo Log da configuracao: .cache\logs\cmake-%CONFIG%.log
)
if defined LOCK_HELD rmdir "%LOCK_DIR%" >nul 2>&1
pause
exit /b 1
