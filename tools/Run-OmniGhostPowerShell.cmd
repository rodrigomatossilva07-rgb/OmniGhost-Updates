@echo off
setlocal EnableExtensions DisableDelayedExpansion

if "%~1"=="" goto :usage
if "%~2"=="" goto :usage

set "ACTION=%~1"
set "PROJECT_DIR=%~f2"
set "SCRIPT="

if /I "%ACTION%"=="validate-project" set "SCRIPT=%PROJECT_DIR%\tools\Validate-Project.ps1"
if /I "%ACTION%"=="ensure-metadata" set "SCRIPT=%PROJECT_DIR%\tools\Ensure-ProjectMetadata.ps1"
if /I "%ACTION%"=="generate-build-metadata" set "SCRIPT=%PROJECT_DIR%\tools\Generate-BuildMetadata.ps1"
if /I "%ACTION%"=="generate-embedded-offsets" set "SCRIPT=%PROJECT_DIR%\tools\Build-EmbeddedOffsets.ps1"
if /I "%ACTION%"=="generate-embedded-runtime" set "SCRIPT=%PROJECT_DIR%\tools\Build-EmbeddedRuntime.ps1"
if /I "%ACTION%"=="generate-embedded-resources" set "SCRIPT=%PROJECT_DIR%\tools\Build-EmbeddedResources.ps1"
if /I "%ACTION%"=="build-private-static-vmm" set "SCRIPT=%PROJECT_DIR%\tools\Build-PrivateStaticVmm.ps1"
if /I "%ACTION%"=="increment-version" set "SCRIPT=%PROJECT_DIR%\tools\Increment-Version.ps1"
if /I "%ACTION%"=="prepare-publish-version" set "SCRIPT=%PROJECT_DIR%\tools\Prepare-PublishVersion.ps1"
if /I "%ACTION%"=="package-release" set "SCRIPT=%PROJECT_DIR%\tools\package_release.ps1"
if /I "%ACTION%"=="restore-version" set "SCRIPT=%PROJECT_DIR%\tools\Restore-Version.ps1"
if /I "%ACTION%"=="repair-latest" set "SCRIPT=%PROJECT_DIR%\tools\Repair-LatestRelease.ps1"
if /I "%ACTION%"=="validate-distribution" set "SCRIPT=%PROJECT_DIR%\tools\Validate-DistributionBuild.ps1"
if /I "%ACTION%"=="publish-build" set "SCRIPT=%PROJECT_DIR%\tools\Publish-Build.ps1"
if /I "%ACTION%"=="compress-assets" set "SCRIPT=%PROJECT_DIR%\tools\Compress-Assets.ps1"

if not defined SCRIPT (
    echo [OmniGhost PowerShell] Unknown action: %ACTION%
    exit /b 64
)

if not exist "%SCRIPT%" (
    echo [OmniGhost PowerShell] Script not found: %SCRIPT%
    exit /b 66
)

set "PS_EXE="

rem Optional explicit override for CI or developer machines.
if defined OMNIGHOST_POWERSHELL (
    if exist "%OMNIGHOST_POWERSHELL%" set "PS_EXE=%OMNIGHOST_POWERSHELL%"
)

rem Windows PowerShell 5.1 is the compatibility baseline on Windows.
if not defined PS_EXE (
    if exist "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" (
        set "PS_EXE=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
    )
)

rem PowerShell 7+ fallback for systems where Windows PowerShell is unavailable.
if not defined PS_EXE (
    if exist "%ProgramFiles%\PowerShell\7\pwsh.exe" set "PS_EXE=%ProgramFiles%\PowerShell\7\pwsh.exe"
)

if not defined PS_EXE (
    for /f "delims=" %%P in ('where pwsh.exe 2^>nul') do if not defined PS_EXE set "PS_EXE=%%P"
)

if not defined PS_EXE (
    for /f "delims=" %%P in ('where powershell.exe 2^>nul') do if not defined PS_EXE set "PS_EXE=%%P"
)

if not defined PS_EXE (
    echo [OmniGhost PowerShell] No compatible PowerShell engine was found.
    echo [OmniGhost PowerShell] Supported engines: Windows PowerShell 5.1 and PowerShell 7+.
    exit /b 69
)

echo [OmniGhost PowerShell] Engine: %PS_EXE%

if /I "%ACTION%"=="package-release" goto :package
if /I "%ACTION%"=="generate-build-metadata" goto :build_metadata
if /I "%ACTION%"=="repair-latest" goto :repair_latest
if /I "%ACTION%"=="validate-distribution" goto :validate_distribution
if /I "%ACTION%"=="publish-build" goto :publish_build
if /I "%ACTION%"=="compress-assets" goto :compress_assets
if /I "%ACTION%"=="generate-embedded-runtime" goto :embedded_runtime
if /I "%ACTION%"=="generate-embedded-offsets" goto :embedded_data
if /I "%ACTION%"=="generate-embedded-resources" goto :embedded_data
if /I "%ACTION%"=="build-private-static-vmm" goto :private_static_vmm
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%"
exit /b %ERRORLEVEL%

:private_static_vmm
set "PLATFORM_TOOLSET=%~3"
if not defined PLATFORM_TOOLSET set "PLATFORM_TOOLSET=v145"
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -PlatformToolset "%PLATFORM_TOOLSET%"
exit /b %ERRORLEVEL%

:embedded_runtime
set "CONFIGURATION=%~4"
if not defined CONFIGURATION set "CONFIGURATION=Release"
set "PLATFORM=%~5"
if not defined PLATFORM set "PLATFORM=x64"
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -VcToolsRedistDir "%~3" -Configuration "%CONFIGURATION%" -Platform "%PLATFORM%"
exit /b %ERRORLEVEL%

:embedded_data
set "CONFIGURATION=%~3"
if not defined CONFIGURATION set "CONFIGURATION=Release"
set "PLATFORM=%~4"
if not defined PLATFORM set "PLATFORM=x64"
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -Configuration "%CONFIGURATION%" -Platform "%PLATFORM%"
exit /b %ERRORLEVEL%

:build_metadata
set "CONFIGURATION=%~3"
set "ARCHITECTURE=%~4"
set "RELEASE_CHANNEL=%~5"
if not defined ARCHITECTURE set "ARCHITECTURE=x64"
if not defined RELEASE_CHANNEL set "RELEASE_CHANNEL=stable"
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -Configuration "%CONFIGURATION%" -Architecture "%ARCHITECTURE%" -ReleaseChannel "%RELEASE_CHANNEL%"
exit /b %ERRORLEVEL%

:repair_latest
if "%~3"=="" (
    "%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%"
) else (
    "%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -Tag "%~3"
)
exit /b %ERRORLEVEL%

:validate_distribution
if "%~3"=="" goto :usage
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -Configuration "%~3"
exit /b %ERRORLEVEL%

:publish_build
if "%~3"=="" goto :usage
set "BUILD_DIR=%~f3"
set "VALIDATE_ONLY=%~4"
if /I "%VALIDATE_ONLY%"=="true" (
    "%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -BuildDir "%BUILD_DIR%" -ValidateOnly -ExplicitConfirmed
) else (
    "%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -BuildDir "%BUILD_DIR%" -ExplicitConfirmed
)
exit /b %ERRORLEVEL%

:compress_assets
set "CONFIGURATION=%~3"
if not defined CONFIGURATION set "CONFIGURATION=Release"
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -Configuration "%CONFIGURATION%"
exit /b %ERRORLEVEL%

:package
if "%~3"=="" goto :usage
if "%~4"=="" goto :usage
set "BUILD_DIR=%~f3"
set "LOG_FILE=%~f4"
"%PS_EXE%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%SCRIPT%" -ProjectDir "%PROJECT_DIR%" -BuildDir "%BUILD_DIR%" -LogFile "%LOG_FILE%"
exit /b %ERRORLEVEL%

:usage
echo Usage:
echo   Run-OmniGhostPowerShell.cmd validate-project PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd ensure-metadata PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd generate-build-metadata PROJECT_DIR [CONFIGURATION] [ARCHITECTURE] [RELEASE_CHANNEL]
echo   Run-OmniGhostPowerShell.cmd generate-embedded-offsets PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd generate-embedded-runtime PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd generate-embedded-resources PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd build-private-static-vmm PROJECT_DIR [PLATFORM_TOOLSET]
echo   Run-OmniGhostPowerShell.cmd increment-version PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd prepare-publish-version PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd package-release PROJECT_DIR BUILD_DIR LOG_FILE
echo   Run-OmniGhostPowerShell.cmd restore-version PROJECT_DIR
echo   Run-OmniGhostPowerShell.cmd repair-latest PROJECT_DIR [TAG]
echo   Run-OmniGhostPowerShell.cmd validate-distribution PROJECT_DIR CONFIGURATION
echo   Run-OmniGhostPowerShell.cmd publish-build PROJECT_DIR BUILD_DIR [true-for-preflight]
echo   Run-OmniGhostPowerShell.cmd compress-assets PROJECT_DIR [CONFIGURATION]
exit /b 64