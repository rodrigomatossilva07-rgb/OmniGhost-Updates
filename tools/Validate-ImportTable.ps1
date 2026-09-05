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

if (-not $AllowListPath) {
    $AllowListPath = Join-Path (Split-Path -Parent $ExecutablePath) "import-allowlist.txt"
}

Write-Host "[ImportTable] Validating import table for: $ExecutablePath"

# Default allowlist for OmniGhost
$defaultAllowList = @(
    # System DLLs
    "kernel32.dll", "ntdll.dll", "user32.dll", "gdi32.dll", "advapi32.dll",
    "shell32.dll", "ole32.dll", "oleaut32.dll", "comdlg32.dll", "version.dll",
    "ws2_32.dll", "winhttp.dll", "wininet.dll", "bcrypt.dll", "wintrust.dll",
    "crypt32.dll", "shlwapi.dll", "version.dll", "advapi32.dll", "setupapi.dll",
    "dwmapi.dll", "imm32.dll", "windowscodecs.lib", "d3d11.dll", "dxgi.dll",
    "d3dcompiler.lib", "bcrypt.lib", "wintrust.lib", "crypt32.lib",
    "shlwapi.lib", "version.lib", "advapi32.lib",
    
    # CRT / Universal CRT
    "vcruntime140.dll", "vcruntime140_1.dll", "msvcrt.dll", "ucrtbase.dll",
    "concrt140.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll",
    
    # DMA / DMA stack
    "leechcore.dll", "vmm.dll", "vmmdll.dll",
    
    # System
    "kernelbase.dll", "sechost.dll", "rpcrt4.dll", "sspicli.dll",
    "cryptbase.dll", "cfgmgr32.dll", "devobj.dll", "propsys.dll",
    "uxtheme.dll", "dwmapi.dll", "clbcatq.dll", "oleacc.dll"
]

# Load custom allowlist if provided
$allowList = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($dll in $defaultAllowList) {
    $allowList.Add($dll) | Out-Null
}

if ($AllowListPath -and (Test-Path -LiteralPath $AllowListPath)) {
    $custom = Get-Content -LiteralPath $AllowListPath | Where-Object { $_ -and $_[0] -ne '#' }
    foreach ($dll in $custom) {
        $allowList.Add($dll.Trim()) | Out-Null
    }
    Write-Host "[ImportTable] Loaded $($custom.Count) additional allowed DLLs from $AllowListPath"
}

# Use dumpbin to extract import table
$dumpbin = "dumpbin.exe"
if (-not (Get-Command $dumpbin -ErrorAction SilentlyContinue)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
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

Write-Host "[ImportTable] Using dumpbin: $dumpbin"

# Extract import table
$imports = @()
$process = Start-Process -FilePath $dumpbin -ArgumentList "/IMPORTS", "/NOLOGO", "`"$ExecutablePath`"" -PassThru -RedirectStandardOutput (Join-Path $env:TEMP "imports.txt") -Wait -NoNewWindow
if ($process.ExitCode -ne 0) {
    throw "dumpbin failed with exit code $($process.ExitCode)"
}

$output = Get-Content (Join-Path $env:TEMP "imports.txt") -Raw

# Parse import table
$currentDll = ""
$imports = @()
foreach ($line in $output -split "`r?`n") {
    $line = $line.Trim()
    if ($line -match '^([A-Za-z0-9_\.]+)\s*$') {
        $currentDll = $matches[1].ToLower()
    } elseif ($line -match '^\s+([A-Za-z0-9_]+)') {
        if ($currentDll) {
            $imports += "$currentDll:$($matches[1])"
        }
    }
}

Write-Host "[ImportTable] Found $($imports.Count) imported functions from $($imports | Group-Object { $_ -split ':' } | Measure-Object).Count DLLs"

# Check for unauthorized imports
$unauthorized = @()
foreach ($imp in $imports) {
    $dll = ($imp -split ':')[0]
    if (-not $allowList.Contains($dll)) {
        $unauthorized += $imp
    }
}

if ($unauthorized.Count -gt 0) {
    Write-Error "[FAIL] Unauthorized imports detected ($($unauthorized.Count)): "
    $unauthorized | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host "[PASS] All imports are authorized ($($imports | Group-Object { $_ -split ':' } | Measure-Object).Count DLLs, $($imports.Count) functions)"

exit 0