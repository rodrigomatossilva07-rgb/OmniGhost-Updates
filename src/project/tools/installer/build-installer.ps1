<#
.SYNOPSIS
    Builds the OmniGhost NSIS installer

.DESCRIPTION
    Builds the NSIS installer for OmniGhost with specified build configuration

.PARAMETER BuildType
    Build type: Debug, Release, Development, Publish (default: Publish)

.PARAMETER Version
    Version string (default: from CMakeLists.txt)

.PARAMETER SignTool
    Path to signtool.exe for code signing (optional)

.PARAMETER CertificateThumbprint
    Certificate thumbprint for code signing (optional)

.EXAMPLE
    .\build-installer.ps1 -BuildType Publish -Version 1.3.4

.EXAMPLE
    .\build-installer.ps1 -BuildType Release -SignTool "C:\signtool.exe" -CertificateThumbprint "ABC123"
#>

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet('Debug', 'Release', 'Development', 'Publish')]
    [string]$BuildType = 'Publish',

    [Parameter(Mandatory=$false)]
    [string]$Version = '',

    [Parameter(Mandatory=$false)]
    [string]$SignTool = '',

    [Parameter(Mandatory=$false)]
    [string]$CertificateThumbprint = '',

    [Parameter(Mandatory=$false)]
    [switch]$Clean
)

# Set strict mode
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The installer tooling lives under src/project/tools/installer.  Resolve the
# repository root explicitly instead of assuming it is the direct parent.
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = [System.IO.Path]::GetFullPath((Join-Path $ScriptDir '..\..\..'))

# Load version from CMakeLists.txt if not provided
if (-not $Version) {
    $CMakeLists = Get-Content "$ProjectRoot\CMakeLists.txt" -Raw
    if ($CMakeLists -match 'project\(OmniGhost\s+VERSION\s+(\d+\.\d+\.\d+)') {
        $Version = $matches[1]
        Write-Host "Using version from CMakeLists.txt: $Version" -ForegroundColor Green
    } else {
        throw "Could not determine version from CMakeLists.txt"
    }
}

Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "OmniGhost Installer Build" -ForegroundColor Cyan
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "Build Type: $BuildType" -ForegroundColor Yellow
Write-Host "Version: $Version" -ForegroundColor Yellow
Write-Host ""

# Check for NSIS
$NsisPath = "${env:ProgramFiles}\NSIS\makensis.exe"
if (-not (Test-Path $NsisPath)) {
    $NsisPath = "${env:ProgramFiles(x86)}\NSIS\makensis.exe"
}
if (-not (Test-Path $NsisPath)) {
    throw "NSIS not found. Please install NSIS 3.x from https://nsis.sourceforge.io/"
}
Write-Host "NSIS found: $NsisPath" -ForegroundColor Green

# Check for Python (required for custom targets)
$PythonPath = (Get-Command python -ErrorAction SilentlyContinue).Source
if (-not $PythonPath) {
    $PythonPath = (Get-Command python3 -ErrorAction SilentlyContinue).Source
}
if (-not $PythonPath) {
    Write-Warning "Python not found in PATH. Custom targets (GenerateEmbeddedResources, ProtectStrings) will be disabled."
    Write-Host "Install Python from https://python.org/downloads/ and add to PATH" -ForegroundColor Yellow
}

# Build directory
$BuildDir = "$ProjectRoot\build\$BuildType"
$ExePath = "$BuildDir\OmniGhost.exe"
$InstallerOutputDir = "$ProjectRoot\build\$BuildType\installer"

if (-not (Test-Path $ExePath)) {
    throw "Executable not found at $ExePath. Please build the project first:"
    Write-Host "  cmake --preset windows-$BuildType" -ForegroundColor Yellow
    Write-Host "  cmake --build --preset build-$BuildType --parallel 8" -ForegroundColor Yellow
}

# Clean installer output
if ($Clean -or -not (Test-Path $InstallerOutputDir)) {
    if (Test-Path $InstallerOutputDir) {
        Remove-Item -Recurse -Force $InstallerOutputDir
    }
    New-Item -ItemType Directory -Force -Path $InstallerOutputDir | Out-Null
}

# Build NSIS installer
$NsisScript = "$ScriptDir\omnighost.nsi"
$NsisArgs = @(
    "/DBUILD_TYPE=$BuildType",
    "/DPRODUCT_VERSION=$Version",
    "/DOUTPUT_DIR=$BuildDir",
    "/DOUTPUT_DIR_INSTALLER=$InstallerOutputDir",
    "/DOUTPUT_DIR_RESOURCES=$BuildDir\resources"
)

Write-Host "Building NSIS installer..." -ForegroundColor Cyan
$Result = & $NsisPath $NsisArgs $NsisScript

if ($LASTEXITCODE -ne 0) {
    throw "NSIS build failed with exit code $LASTEXITCODE"
}

# Find generated installer
$InstallerPath = Get-ChildItem "$InstallerOutputDir\OmniGhost-*.exe" | Select-Object -First 1
if (-not $InstallerPath) {
    throw "Installer not found in $InstallerOutputDir"
}

Write-Host ""
Write-Host "Installer built successfully!" -ForegroundColor Green
Write-Host "Output: $($InstallerPath.FullName)" -ForegroundColor Green
Write-Host "Size: $([math]::Round($InstallerPath.Length / 1MB, 2)) MB" -ForegroundColor Yellow

# Code signing (if requested)
if ($SignTool -and $CertificateThumbprint) {
    Write-Host ""
    Write-Host "Signing installer..." -ForegroundColor Cyan
    
    $SignArgs = @(
        "sign",
        "/fd", "SHA256",
        "/tr", "http://timestamp.sectigo.com",
        "/td", "SHA256",
        "/sha1", $CertificateThumbprint,
        $InstallerPath.FullName
    )
    
    $SignResult = & $SignTool $SignArgs
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Installer signed successfully!" -ForegroundColor Green
    } else {
        Write-Warning "Signing failed with exit code $LASTEXITCODE"
    }
}

# Verify installer
Write-Host ""
Write-Host "Verifying installer..." -ForegroundColor Cyan
$Hash = Get-FileHash -Algorithm SHA256 -Path $InstallerPath.FullName
Write-Host "SHA256: $($Hash.Hash)" -ForegroundColor Yellow

# Generate SHA256 file
$Hash.Hash | Set-Content "$InstallerOutputDir\$(Split-Path $InstallerPath -Leaf).sha256"

Write-Host ""
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "Installer Build Complete!" -ForegroundColor Cyan
Write-Host "===========================================" -ForegroundColor Cyan
Write-Host "Installer: $($InstallerPath.FullName)" -ForegroundColor Green
Write-Host "SHA256: $($Hash.Hash)" -ForegroundColor Yellow
Write-Host ""
Write-Host "To install silently:" -ForegroundColor Cyan
Write-Host "  $($InstallerPath.Name) /S" -ForegroundColor Yellow
Write-Host ""
Write-Host "To install with custom directory:" -ForegroundColor Cyan
Write-Host "  $($InstallerPath.Name) /S /D=C:\Path\To\OmniGhost" -ForegroundColor Yellow
