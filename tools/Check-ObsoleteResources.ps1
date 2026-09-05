[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ExecutablePath,
    [Parameter(Mandatory=$false)][string]$AllowListPath = ""
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
    throw "Executable not found: $ExecutablePath"
}

Write-Host "[ObsoleteResources] Checking for obsolete resources in: $ExecutablePath"

# Use dumpbin to list resources
$dumpbin = "dumpbin.exe"
if (-not (Get-Command $dumpbin -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $vcPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vcPath) {
            $dumpbin = Join-Path $vcPath 'VC\Tools\MSVC\*' -ChildPath 'bin\Hostx64\x64\dumpbin.exe'
            $dumpbin = Resolve-Path $dumpbin | Select-Object -First 1 -ExpandProperty FullName
        }
    }
}

if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) {
    throw "dumpbin.exe not found. Install Visual Studio with C++ tools."
}

# Extract resources
$resourceOutput = Join-Path $env:TEMP "resources_$([guid]::NewGuid()).txt"
$process = Start-Process -FilePath $dumpbin -ArgumentList "/RESOURCES", "/NOLOGO", "`"$ExecutablePath`"" -PassThru -RedirectStandardOutput $resourceOutput -Wait -NoNewWindow
if ($process.ExitCode -ne 0) {
    throw "dumpbin /RESOURCES failed with exit code $($process.ExitCode)"
}

$output = Get-Content $resourceOutput -Raw
Remove-Item $resourceOutput -Force

# Parse resource output
$resources = @()
$currentType = ""
$currentName = ""
$currentLang = ""
$inResource = $false

foreach ($line in $output -split "`r?`n") {
    $line = $line.Trim()
    if ($line -match '^Type\s+([A-Z0-9_]+)') {
        $currentType = $matches[1]
        $inResource = $true
    } elseif ($inResource -and $line -match '^Name\s+(.+)') {
        $currentName = $matches[1]
    } elseif ($inResource -and $line -match '^Language\s+([A-Z0-9_]+)') {
        $currentLang = $matches[1]
    } elseif ($inResource -and $line -match '^\s*$') {
        if ($currentType -or $currentName -or $currentLang) {
            $resources += [pscustomobject]@{
                Type = $currentType
                Name = $currentName
                Language = $currentLang
            }
        }
        $currentType = ""
        $currentName = ""
        $currentLang = ""
        $inResource = $false
    }
}

Write-Host "[ObsoleteResources] Found $($resources.Count) resources"

# Define obsolete/known-good resource types and names
$obsoleteTypes = @{
    "MANIFEST" = @("MANIFEST")  # Embedded manifests are handled separately
    "VERSION" = @("VERSION")   # Version info is expected
    "ICON" = @()               # Icons are expected
    "CURSOR" = @()             # Cursors are expected
    "BITMAP" = @()             # Bitmaps are expected
    "DIALOG" = @()             # Dialogs are expected
    "MENU" = @()               # Menus are expected
    "STRING" = @()             # String tables are expected
    "ACCELERATOR" = @()        # Accelerators are expected
    "RC_DATA" = @()            # Raw data is expected
}

$unexpectedResources = @()
foreach ($res in $resources) {
    if ($obsoleteTypes.ContainsKey($res.Type)) {
        # Check if this specific name is in the obsolete list for this type
        $obsoleteNames = $obsoleteTypes[$res.Type]
        if ($obsoleteNames.Count -gt 0 -and $obsoleteNames -contains $res.Name) {
            $unexpectedResources += $res
        }
    } else {
        # Unknown resource type
        $unexpectedResources += $res
    }
}

if ($unexpectedResources.Count -gt 0) {
    Write-Error "[FAIL] Unexpected/obsolete resources found ($($unexpectedResources.Count)):"
    $unexpectedResources | ForEach-Object {
        Write-Host "  Type: $($_.Type), Name: $($_.Name), Lang: $($_.Language)"
    }
    exit 1
}

Write-Host "[PASS] No obsolete resources found ($($resources.Count) resources checked)"

exit 0