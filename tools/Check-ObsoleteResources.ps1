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


$resourceOutput = Join-Path $env:TEMP "resources_$([guid]::NewGuid()).txt"
$process = Start-Process -FilePath $dumpbin -ArgumentList "/RESOURCES", "/NOLOGO", "`"$ExecutablePath`"" -PassThru -RedirectStandardOutput $resourceOutput -Wait -NoNewWindow
if ($process.ExitCode -ne 0) {
    throw "dumpbin /RESOURCES failed with exit code $($process.ExitCode)"
}

$output = Get-Content $resourceOutput -Raw
Remove-Item $resourceOutput -Force


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


$obsoleteTypes = @{
    "MANIFEST" = @("MANIFEST")
    "VERSION" = @("VERSION")
    "ICON" = @()
    "CURSOR" = @()
    "BITMAP" = @()
    "DIALOG" = @()
    "MENU" = @()
    "STRING" = @()
    "ACCELERATOR" = @()
    "RC_DATA" = @()
}

$unexpectedResources = @()
foreach ($res in $resources) {
    if ($obsoleteTypes.ContainsKey($res.Type)) {

        $obsoleteNames = $obsoleteTypes[$res.Type]
        if ($obsoleteNames.Count -gt 0 -and $obsoleteNames -contains $res.Name) {
            $unexpectedResources += $res
        }
    } else {

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