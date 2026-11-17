@echo off
echo [DEBUG] Starting build.bat
echo [DEBUG] Starting build.bat >> build_debug.log

REM Re-entry guard - file-based lock for cross-process safety
if exist build.lock (
    echo [WARN] Build already running (lock file exists), exiting
    exit /b 0
)
echo %DATE% %TIME% > build.lock

REM =============================================================================
REM OmniGhost - CMake Build Script (Publish Configuration)
REM =============================================================================
REM Builds the project using CMake with Publish preset (single EXE, optimized)
REM Auto-installs CMake/vcpkg/VS Build Tools if missing
REM =============================================================================

setlocal enabledelayedexpansion

echo [DEBUG] setlocal done >> build_debug.log

REM Cleanup lock on exit
set "CLEANUP_DONE=0"

REM =============================================================================
REM Configuration
REM =============================================================================
set CONFIG=Publish
set PRESET=windows-publish
set BUILD_PRESET=build-publish
set JOBS=8
set CMAKE_MIN_VERSION=3.25

REM Colors (ANSI)
set RED=\033[31m
set GREEN=\033[32m
set YELLOW=\033[33m
set BLUE=\033[34m
set CYAN=\033[36m
set RESET=\033[0m

REM =============================================================================
REM Helper Functions (using exit /b for proper return from code blocks)
REM =============================================================================
:LOG_INFO
    echo %CYAN%[INFO]%RESET% %~1
    echo [INFO] %~1 >> build_debug.log
    exit /b 0

:LOG_WARN
    echo %YELLOW%[WARN]%RESET% %~1
    exit /b 0

:LOG_ERROR
    echo %RED%[ERROR]%RESET% %~1
    exit /b 0

:LOG_OK
    echo %GREEN%[OK]%RESET% %~1
    exit /b 0

:LOG_STEP
    echo %BLUE%>>%RESET% %~1
    echo %BLUE%[DEBUG]%RESET% %~1 >> build_debug.log
    exit /b 0

:CHECK_CMD
    echo [DEBUG] Checking for %1 >> build_debug.log
    where %1 >nul 2>nul
    if %errorlevel% equ 0 (
        call :LOG_OK "%1 found"
        exit /b 0
    ) else (
        call :LOG_WARN "%1 not found"
        REM Try common CMake locations
        if "%~1"=="cmake" (
            echo [DEBUG] Checking CMake locations... >> build_debug.log
            echo [DEBUG] Checking C:\Program Files\CMake\bin\cmake.exe >> build_debug.log
            if exist "C:\Program Files\CMake\bin\cmake.exe" (
                echo [DEBUG] Found at C:\Program Files\CMake\bin >> build_debug.log
                set "CMAKE_PATH=C:\Program Files\CMake\bin"
                call :LOG_OK "Found CMake at %CMAKE_PATH%"
                exit /b 0
            )
            echo [DEBUG] Checking WinGet path... >> build_debug.log
            echo [DEBUG] Checking WinGet path... >> build_debug.log
            powershell -NoProfile -Command "Test-Path 'C:\Users\%USERNAME%\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin\cmake.exe'" >nul 2>&1
            if %errorlevel% equ 0 (
                echo [DEBUG] Found at WinGet path >> build_debug.log
                set "CMAKE_PATH=C:\Users\%USERNAME%\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin"
                call :LOG_OK "Found CMake at %CMAKE_PATH%"
                exit /b 0
            )
            if exist "C:\Program Files\CMake\bin\cmake.exe" (
                set "CMAKE_PATH=C:\Program Files\CMake\bin"
                call :LOG_OK "Found CMake at %CMAKE_PATH%"
                exit /b 0
            )
        )
        call :LOG_WARN "%1 not found"
        exit /b 1
    )

:INSTALL_CMAKE
    call :LOG_STEP "Installing CMake %CMAKE_MIN_VERSION%+ via winget..."
    winget install --id Kitware.CMake --version %CMAKE_MIN_VERSION% --accept-source-agreements --accept-package-agreements --silent >nul 2>&1
    if %errorlevel% neq 0 (
        call :LOG_WARN "winget install failed, trying alternative..."
        winget install --id Kitware.CMake --accept-source-agreements --accept-package-agreements --silent >nul 2>&1
    )
    if %errorlevel% equ 0 (
        call :LOG_OK "CMake installed successfully"
        REM Refresh PATH
        for /f "tokens=2* delims= " %%a in ('reg query "HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment" /v Path 2^>nul') do set "SYSPATH=%%b"
        set "PATH=%SYSPATH%;%PATH%"
        exit /b 0
    ) else (
        call :LOG_ERROR "Failed to install CMake automatically"
        call :LOG_ERROR "Please install manually: https://cmake.org/download/"
        exit /b 1
    )

:INSTALL_VCPKG
    call :LOG_STEP "Setting up vcpkg..."
    if not exist "%USERPROFILE%\vcpkg" (
        git clone https://github.com/microsoft/vcpkg.git "%USERPROFILE%\vcpkg" >nul 2>&1
        if %errorlevel% neq 0 (
            call :LOG_ERROR "Failed to clone vcpkg"
            exit /b 1
        )
    )
    call :LOG_STEP "Bootstrapping vcpkg..."
    cmd /c "%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat" >nul 2>&1
    if %errorlevel% neq 0 (
        call :LOG_ERROR "vcpkg bootstrap failed"
        exit /b 1
    )
    setx VCPKG_ROOT "%USERPROFILE%\vcpkg" /M >nul 2>&1
    call :LOG_OK "vcpkg ready at %USERPROFILE%\vcpkg"
    exit /b 0

:ENSURE_VS
    call :LOG_STEP "Checking Visual Studio Build Tools..."
    if not defined VCINSTALLDIR (
        for /f "tokens=*" %%a in ('vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2^>nul') do (
            set VS_PATH=%%a
        )
        if defined VS_PATH (
            call :LOG_OK "Visual Studio found: !VS_PATH!"
            call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64
        ) else (
            call :LOG_STEP "Installing Visual Studio Build Tools..."
            winget install --id Microsoft.VisualStudio.2022.BuildTools --accept-source-agreements --accept-package-agreements --silent >nul 2>&1
            if %errorlevel% neq 0 (
                call :LOG_ERROR "Failed to install VS Build Tools"
                exit /b 1
            )
            for /f "tokens=*" %%a in ('vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath 2^>nul') do (
                set VS_PATH=%%a
            )
            if defined VS_PATH (
                call "!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat" x64
            )
        )
    )
    call :LOG_OK "Visual Studio environment ready"
    exit /b 0

:SHOW_BANNER
    echo %BLUE%============================================================%RESET%
    echo %BLUE%  OmniGhost - CMake Build (%CONFIG%)%RESET%
    echo %BLUE%============================================================%RESET%
    echo.
    exit /b 0

REM =============================================================================
REM Main Build Flow
REM =============================================================================
call :SHOW_BANNER

REM -----------------------------------------------------------------------------
REM Step 1: Ensure CMake - Use known WinGet installation path
REM -----------------------------------------------------------------------------
call :LOG_STEP "Checking dependencies..."
set "CMAKE_PATH=C:\Users\%USERNAME%\AppData\Local\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\cmake-4.4.4-windows-x86_64\bin"
if exist "%CMAKE_PATH%\cmake.exe" (
    call :LOG_OK "CMake found at %CMAKE_PATH%"
    set "PATH=%CMAKE_PATH%;%PATH%"
) else (
    call :LOG_STEP "Checking dependencies..."
    call :CHECK_CMD cmake
    if %errorlevel% neq 0 (
        call :INSTALL_CMAKE
        call :CHECK_CMD cmake
        if %errorlevel% neq 0 (
            call :LOG_ERROR "CMake still not available after install"
            goto :CLEANUP_EXIT
        )
    ) else (
        call :LOG_OK "CMake already installed"
    )
    if defined CMAKE_PATH (
        set "PATH=%CMAKE_PATH%;%PATH%"
        call :LOG_OK "Added CMake to PATH: %CMAKE_PATH%"
    )
)

REM -----------------------------------------------------------------------------
REM Step 2: Ensure vcpkg
REM -----------------------------------------------------------------------------
if not defined VCPKG_ROOT (
    call :LOG_WARN "VCPKG_ROOT not set"
    if exist "%USERPROFILE%\vcpkg" (
        set VCPKG_ROOT=%USERPROFILE%\vcpkg
        call :LOG_OK "Using vcpkg at %VCPKG_ROOT%"
    ) else (
        call :INSTALL_VCPKG
    )
) else (
    call :LOG_OK "vcpkg found at %VCPKG_ROOT%"
)

REM -----------------------------------------------------------------------------
REM Step 3: Ensure Visual Studio Build Tools
REM -----------------------------------------------------------------------------
call :ENSURE_VS

REM -----------------------------------------------------------------------------
REM Step 4: Configure CMake
REM -----------------------------------------------------------------------------
echo.
call :LOG_STEP "Configuring CMake with preset: %PRESET%..."
if defined CMAKE_PATH (
    "%CMAKE_PATH%\cmake.exe" --preset %PRESET%
) else (
    cmake --preset %PRESET%
)
if %errorlevel% neq 0 (
    call :LOG_ERROR "CMake configure failed"
    goto :CLEANUP_EXIT
)
call :LOG_OK "CMake configured successfully"

REM -----------------------------------------------------------------------------
REM Step 5: Build
REM -----------------------------------------------------------------------------
echo.
call :LOG_STEP "Building with preset: %BUILD_PRESET% (%JOBS% parallel jobs)..."
if defined CMAKE_PATH (
    "%CMAKE_PATH%\cmake.exe" --build --preset %BUILD_PRESET% --parallel %JOBS%
) else (
    cmake --build --preset %BUILD_PRESET% --parallel %JOBS%
)
if %errorlevel% neq 0 (
    call :LOG_ERROR "Build failed"
    goto :CLEANUP_EXIT
)
call :LOG_OK "Build completed successfully"

REM -----------------------------------------------------------------------------
REM Step 6: Verify Output
REM -----------------------------------------------------------------------------
echo.
call :LOG_STEP "Verifying output..."
if exist "build\Publish\OmniGhost.exe" (
    call :LOG_OK "Executable verified: build\Publish\OmniGhost.exe"
    for %%A in (build\Publish\OmniGhost.exe) do (
        echo  Size: %%~zA bytes
        echo  Modified: %%~tA
    )
) else (
    call :LOG_ERROR "Executable not found at expected location"
    goto :CLEANUP_EXIT
)

echo.
echo %GREEN%============================================================%RESET%
echo %GREEN%  Build Successful!%RESET%
echo %GREEN%============================================================%RESET%
echo.
echo Output: build\Publish\OmniGhost.exe
echo.
echo %BLUE%Done.%RESET%

:CLEANUP_EXIT
REM Cleanup lock file
del /f /q build.lock 2>nul

endlocal
exit /b %errorlevel%