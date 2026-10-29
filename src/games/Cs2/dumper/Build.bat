@echo off
setlocal

pushd "%~dp0" >nul || (
    echo [ERRO] Nao foi possivel abrir a pasta do CS2 Dumper.
    pause
    exit /b 1
)

where cargo >nul 2>&1 || (
    echo [ERRO] Cargo nao foi encontrado no PATH.
    echo Instala o Rust em https://rustup.rs/ e abre novamente este ficheiro.
    popd >nul
    pause
    exit /b 1
)

echo [CS2 Dumper] A executar cargo build...
cargo build
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

if not "%BUILD_EXIT_CODE%"=="0" (
    echo.
    echo [ERRO] A compilacao terminou com o codigo %BUILD_EXIT_CODE%.
) else (
    echo.
    echo [OK] Compilacao concluida.
    echo Executavel: %~dp0target\debug\cs2-dumper.exe
)

popd >nul
pause
exit /b %BUILD_EXIT_CODE%
