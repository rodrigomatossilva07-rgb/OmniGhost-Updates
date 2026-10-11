@echo off
setlocal

:: Run-OmniGhostPowerShell.cmd
:: PowerShell bridge for MSBuild to invoke OmniGhost tooling scripts.
:: Usage: Run-OmniGhostPowerShell.cmd <command> [args...]

set "SCRIPT_DIR=%~dp0"

if "%~1"=="" (
    echo Uso: %~nx0 ^<comando^> [argumentos...]
    echo.
    echo Comandos disponiveis:
    echo   generate-embedded-offsets       ^<ProjectDir^> ^<Configuration^>
    echo   generate-embedded-runtime       ^<ProjectDir^> ^<VcToolsRedistDir^> ^<Configuration^>
    echo   validate-embedded-runtime       ^<ProjectDir^> ^<Configuration^> ^<Platform^>
    echo   generate-embedded-resources     ^<ProjectDir^> ^<Configuration^> ^<Platform^>
    echo   publish-build                   ^<ProjectDir^> ^<BuildDir^>
    echo   cleanup-verified-build          ^<ProjectDir^>
    echo   validate-embedded-offsets       ^<ProjectDir^> ^<Executable^> ^<Configuration^> ^<Platform^>
    echo   validate-project                ^<ProjectDir^>
    echo   prepare-publish-version         ^<ProjectDir^>
    echo   ensure-metadata                 ^<ProjectDir^>
    echo   validate-distribution           ^<ProjectDir^> ^<Configuration^>
    echo   generate-build-metadata         ^<ProjectDir^> ^<Configuration^> ^<Platform^> ^<ReleaseChannel^> ^<WindowsSdkVersion^>
    exit /b 1
)

set "COMMAND=%~1"
shift

:: Find PowerShell
set "PS_CMD=powershell.exe"
where powershell.exe >nul 2>nul || set "PS_CMD=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"

:: Helper to invoke a .ps1 script with arguments, handling paths with spaces
set "INVOKE_PS=^& \"%SCRIPT_DIR:~0,-1%\""

if "%COMMAND%"=="generate-embedded-offsets" (
    if "%~2"=="" (
        echo Erro: generate-embedded-offsets requer ProjectDir e Configuration
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Build-EmbeddedOffsets.ps1\" -ProjectDir \"%~1\" -Configuration \"%~2\""
    exit /b
)

if "%COMMAND%"=="generate-embedded-runtime" (
    if "%~3"=="" (
        echo Erro: generate-embedded-runtime requer ProjectDir, VcToolsRedistDir e Configuration
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Build-EmbeddedRuntime.ps1\" -ProjectDir \"%~1\" -VcToolsRedistDir \"%~2\" -Configuration \"%~3\""
    exit /b
)

if "%COMMAND%"=="validate-embedded-runtime" (
    if "%~3"=="" (
        echo Erro: validate-embedded-runtime requer ProjectDir, Configuration e Platform
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Validate-EmbeddedRuntime.ps1\" -ProjectDir \"%~1\" -Configuration \"%~2\" -Platform \"%~3\""
    exit /b
)

if "%COMMAND%"=="generate-embedded-resources" (
    if "%~3"=="" (
        echo Erro: generate-embedded-resources requer ProjectDir, Configuration e Platform
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Build-EmbeddedResources.ps1\" -ProjectDir \"%~1\" -Configuration \"%~2\" -Platform \"%~3\""
    exit /b
)

if "%COMMAND%"=="publish-build" (
    if "%~2"=="" (
        echo Erro: publish-build requer ProjectDir e BuildDir
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Publish-Build.ps1\" -ProjectDir \"%~1\" -BuildDir \"%~2\""
    exit /b
)

if "%COMMAND%"=="cleanup-verified-build" (
    if "%~1"=="" (
        echo Erro: cleanup-verified-build requer ProjectDir
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Cleanup-PublishWorkspace.ps1\" -ProjectDir \"%~1\""
    exit /b
)

if "%COMMAND%"=="validate-embedded-offsets" (
    if "%~4"=="" (
        echo Erro: validate-embedded-offsets requer ProjectDir, Executable, Configuration e Platform
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Validate-EmbeddedOffsets.ps1\" -ProjectDir \"%~1\" -Executable \"%~2\" -Configuration \"%~3\" -Platform \"%~4\""
    exit /b
)

if "%COMMAND%"=="validate-project" (
    if "%~1"=="" (
        echo Erro: validate-project requer ProjectDir
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Validate-Project.ps1\" -ProjectDir \"%~1\""
    exit /b
)

if "%COMMAND%"=="prepare-publish-version" (
    if "%~1"=="" (
        echo Erro: prepare-publish-version requer ProjectDir
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Prepare-PublishVersion.ps1\" -ProjectDir \"%~1\""
    exit /b
)

if "%COMMAND%"=="ensure-metadata" (
    if "%~1"=="" (
        echo Erro: ensure-metadata requer ProjectDir
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Ensure-ProjectMetadata.ps1\" -ProjectDir \"%~1\""
    exit /b
)

if "%COMMAND%"=="validate-distribution" (
    if "%~2"=="" (
        echo Erro: validate-distribution requer ProjectDir e Configuration
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Validate-DistributionBuild.ps1\" -ProjectDir \"%~1\" -Configuration \"%~2\""
    exit /b
)

if "%COMMAND%"=="generate-build-metadata" (
    if "%~5"=="" (
        echo Erro: generate-build-metadata requer ProjectDir, Configuration, Platform, ReleaseChannel e WindowsSdkVersion
        exit /b 1
    )
    "%PS_CMD%" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "& \"%SCRIPT_DIR%Generate-BuildMetadata.ps1\" -ProjectDir \"%~1\" -Configuration \"%~2\" -Architecture \"%~3\" -ReleaseChannel \"%~4\" -WindowsSdkVersion \"%~5\""
    exit /b
)

echo Comando desconhecido: %COMMAND%
exit /b 1