<#
.SYNOPSIS
  Transactional MemProcFS + LeechCore dependency manager for OmniGhost.

.DESCRIPTION
  third_party\dma_stack is the only managed source of truth. Updates are staged,
  compatibility-gated, validated, and rolled back on failure. Legacy managed copies
  under DMALibrary\libs are deliberately removed. The root libs\ directory is an
  intentional local runtime fallback bundle and is preserved.

.EXAMPLE
  .\tools\Update-DmaDependencies.ps1 -MemProcFSZip path\mpfs.zip -LeechCoreZip path\lc.zip
  .\tools\Update-DmaDependencies.ps1 -Latest
  .\tools\Update-DmaDependencies.ps1 -Validate
  .\tools\Update-DmaDependencies.ps1 -Doctor
  .\tools\Update-DmaDependencies.ps1 -Status
  .\tools\Update-DmaDependencies.ps1 -Rollback
  .\tools\Update-DmaDependencies.ps1 -DryRun -Latest
#>
[CmdletBinding()]
param(
    [string]$MemProcFSZip = "",
    [string]$LeechCoreZip = "",
    [switch]$Latest,
    [switch]$DryRun,
    [switch]$Validate,
    [switch]$Check,
    [switch]$Build,
    [switch]$Status,
    [switch]$Doctor,
    [switch]$Rollback
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$Stack = Join-Path $Root "third_party\dma_stack"
$Staging = Join-Path $Root "third_party\dma_stack.staging"
$Backup = Join-Path $Root "third_party\dma_stack.backup"
$LogDir = Join-Path $Root "logs"
$TempRoot = Join-Path $Root ".temp\dma_update"
$MaxArchiveEntries = 10000
$MaxExpandedBytes = 2GB

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Log = Join-Path $LogDir ("dependency-update-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))

# Windows PowerShell 5.1 may default to older TLS policies.
try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

function Write-Log([string]$Message) {
    $line = "[{0:HH:mm:ss}] {1}" -f (Get-Date), $Message
    Write-Host $line
    Add-Content -LiteralPath $Log -Value $line -Encoding UTF8
}

function Get-Sha256([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Test-PeX64([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $stream = [System.IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 0x100) { return $false }
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { return $false }
        $stream.Position = 0x3C
        $pe = $reader.ReadInt32()
        if ($pe -le 0 -or ($pe + 6) -ge $stream.Length) { return $false }
        $stream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x00004550) { return $false }
        return ($reader.ReadUInt16() -eq 0x8664)
    } finally {
        $stream.Dispose()
    }
}

function Assert-OfficialGitHubUri([string]$Uri, [string]$Context) {
    $parsed = [Uri]$Uri
    if ($parsed.Scheme -ne "https") { throw "$Context must use HTTPS: $Uri" }
    $allowed = @("api.github.com", "github.com", "objects.githubusercontent.com", "release-assets.githubusercontent.com")
    if ($allowed -notcontains $parsed.DnsSafeHost.ToLowerInvariant()) {
        throw "$Context uses an unapproved host: $($parsed.DnsSafeHost)"
    }
}

function Assert-SafeArchivePath([string]$Name) {
    $normalized = ($Name -replace '\\','/').Trim()
    if ([string]::IsNullOrWhiteSpace($normalized)) { throw "Archive contains an empty path." }
    if ($normalized.StartsWith("/") -or $normalized.StartsWith("\")) { throw "Archive contains rooted path: $Name" }
    if ($normalized -match '^[A-Za-z]:') { throw "Archive contains drive-qualified path: $Name" }

    $segments = $normalized.Split('/')
    $reserved = @("CON","PRN","AUX","NUL","COM1","COM2","COM3","COM4","COM5","COM6","COM7","COM8","COM9",
                  "LPT1","LPT2","LPT3","LPT4","LPT5","LPT6","LPT7","LPT8","LPT9")
    foreach ($segment in $segments) {
        if ($segment -eq "" -or $segment -eq ".") { continue }
        if ($segment -eq "..") { throw "Archive path traversal rejected: $Name" }
        if ($segment.Contains(":")) { throw "Archive alternate stream/colon rejected: $Name" }
        if ($segment.EndsWith(".") -or $segment.EndsWith(" ")) { throw "Archive trailing dot/space rejected: $Name" }
        $stem = [IO.Path]::GetFileNameWithoutExtension($segment).ToUpperInvariant()
        if ($reserved -contains $stem) { throw "Archive reserved Windows name rejected: $Name" }
    }
    return $normalized
}

function Expand-ZipNormalized([string]$Zip, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    $destinationFull = [IO.Path]::GetFullPath($Destination).TrimEnd('\') + '\'
    $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    [long]$expanded = 0
    try {
        if ($archive.Entries.Count -gt $MaxArchiveEntries) {
            throw "Archive has too many entries: $($archive.Entries.Count) > $MaxArchiveEntries"
        }
        foreach ($entry in $archive.Entries) {
            $normalized = Assert-SafeArchivePath $entry.FullName
            if ($normalized.EndsWith('/')) { continue }

            if (-not $seen.Add($normalized)) {
                throw "Archive contains a case-insensitive duplicate path: $normalized"
            }

            $expanded += [long]$entry.Length
            if ($expanded -gt $MaxExpandedBytes) {
                throw "Archive expanded size exceeds safety limit of $MaxExpandedBytes bytes."
            }

            $relative = $normalized -replace '/','\'
            $target = [IO.Path]::GetFullPath((Join-Path $Destination $relative))
            if (-not $target.StartsWith($destinationFull, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive path escapes staging directory: $normalized"
            }

            $dir = Split-Path -Parent $target
            if (-not (Test-Path -LiteralPath $dir)) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $target, $true)
        }
    } finally {
        $archive.Dispose()
    }
}

function Find-File([string]$Directory, [string]$Name) {
    $hit = Get-ChildItem -LiteralPath $Directory -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ieq $Name } | Select-Object -First 1
    if ($hit) { return $hit.FullName }
    return $null
}

function Get-Required([string]$Directory, [string[]]$Names) {
    foreach ($name in $Names) {
        if (-not (Find-File $Directory $name)) { throw "Missing required file: $name under $Directory" }
    }
}

function Write-VersionsJson([string]$StackDir, [hashtable]$Meta) {
    $Meta | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath (Join-Path $StackDir "versions.json") -Encoding UTF8
}

function Install-FromExtracts([string]$MemProcDir, [string]$LeechCoreDir, [string]$Destination) {
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Recurse -Force }
    foreach ($dir in @("include","lib","bin","data","plugins")) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Destination $dir) | Out-Null
    }

    $mpfsDll = Find-File $MemProcDir "leechcore.dll"
    $lcDll = Find-File $LeechCoreDir "leechcore.dll"
    $mpfsLib = Find-File $MemProcDir "leechcore.lib"
    $lcLib = Find-File $LeechCoreDir "leechcore.lib"

    $mpfsDllHash = Get-Sha256 $mpfsDll
    $lcDllHash = Get-Sha256 $lcDll
    $mpfsLibHash = Get-Sha256 $mpfsLib
    $lcLibHash = Get-Sha256 $lcLib
    Write-Log "LeechCore DLL SHA256 MemProcFS=$mpfsDllHash"
    Write-Log "LeechCore DLL SHA256 Standalone=$lcDllHash"
    Write-Log "LeechCore LIB SHA256 MemProcFS=$mpfsLibHash"
    Write-Log "LeechCore LIB SHA256 Standalone=$lcLibHash"

    if ($mpfsDllHash -ne $lcDllHash -or $mpfsLibHash -ne $lcLibHash) {
        throw "COMPATIBILITY CHECK FAILED: MemProcFS and standalone LeechCore binaries differ."
    }
    Write-Log "COMPATIBILITY = VERIFIED_BY_IDENTICAL_BINARIES"

    Copy-Item (Find-File $MemProcDir "vmmdll.h") (Join-Path $Destination "include\vmmdll.h") -Force
    Copy-Item (Find-File $LeechCoreDir "leechcore.h") (Join-Path $Destination "include\leechcore.h") -Force
    Copy-Item (Find-File $MemProcDir "vmm.lib") (Join-Path $Destination "lib\vmm.lib") -Force
    Copy-Item $mpfsLib (Join-Path $Destination "lib\leechcore.lib") -Force
    $ftdImportLib = Find-File $LeechCoreDir "FTD3XX.lib"
    if (-not $ftdImportLib) { $ftdImportLib = Find-File $MemProcDir "FTD3XX.lib" }
    if ($ftdImportLib) { Copy-Item $ftdImportLib (Join-Path $Destination "lib\FTD3XX.lib") -Force }
    Copy-Item (Find-File $MemProcDir "vmm.dll") (Join-Path $Destination "bin\vmm.dll") -Force
    Copy-Item $mpfsDll (Join-Path $Destination "bin\leechcore.dll") -Force

    foreach ($name in @("FTD3XX.dll","FTD3XXWU.dll","dbghelp.dll","symsrv.dll","tinylz4.dll","vmmyara.dll",
                         "leechcore_device_hvsavedstate.dll","leechcore_device_rawtcp.dll","leechcore_driver.dll")) {
        $file = Find-File $MemProcDir $name
        if (-not $file) { $file = Find-File $LeechCoreDir $name }
        if ($file) { Copy-Item $file (Join-Path $Destination "bin\$name") -Force }
    }


    $plugins = Get-ChildItem -LiteralPath $MemProcDir -Recurse -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ieq "plugins" } | Select-Object -First 1
    if ($plugins) {
        Copy-Item -Path (Join-Path $plugins.FullName "*") -Destination (Join-Path $Destination "plugins") -Recurse -Force
    }

    foreach ($dll in Get-ChildItem -LiteralPath (Join-Path $Destination "bin") -Filter *.dll -File) {
        if (-not (Test-PeX64 $dll.FullName)) { throw "$($dll.Name) is not a PE AMD64 binary." }
    }

    $meta = @{
        updated_at = (Get-Date).ToUniversalTime().ToString("o")
        architecture = "win-x64"
        source = "Update-DmaDependencies.ps1"
        compatibility = "VERIFIED_BY_IDENTICAL_BINARIES"
        memprocfs = @{
            asset = (Split-Path -Leaf $MemProcFSZip)
            sha256 = @{
                "vmm.dll" = (Get-Sha256 (Join-Path $Destination "bin\vmm.dll"))
                "vmm.lib" = (Get-Sha256 (Join-Path $Destination "lib\vmm.lib"))
                "vmmdll.h" = (Get-Sha256 (Join-Path $Destination "include\vmmdll.h"))
            }
        }
        leechcore = @{
            asset = (Split-Path -Leaf $LeechCoreZip)
            header_source = "LeechCore standalone package"
            sha256 = @{
                "leechcore.dll" = (Get-Sha256 (Join-Path $Destination "bin\leechcore.dll"))
                "leechcore.lib" = (Get-Sha256 (Join-Path $Destination "lib\leechcore.lib"))
                "leechcore.h" = (Get-Sha256 (Join-Path $Destination "include\leechcore.h"))
            }
        }
    }
    Write-VersionsJson $Destination $meta
}

function Remove-LegacyManagedCopies {
    $legacyFiles = @(
        "DMALibrary\libs\vmmdll.h", "DMALibrary\libs\leechcore.h",
        "DMALibrary\libs\vmm.lib", "DMALibrary\libs\leechcore.lib",
        "DMALibrary\libs\info.db", "DMALibrary\info.db", "third_party\dma_stack\data\info.db"
    )
    foreach ($relative in $legacyFiles) {
        $path = Join-Path $Root $relative
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Remove-Item -LiteralPath $path -Force
            Write-Log "Removed legacy managed copy: $relative"
        }
    }
    foreach ($relativeDir in @("DMALibrary\libs")) {
        $dir = Join-Path $Root $relativeDir
        if (Test-Path -LiteralPath $dir -PathType Container) {
            $remaining = @(Get-ChildItem -LiteralPath $dir -Force -ErrorAction SilentlyContinue)
            if ($remaining.Count -eq 0) { Remove-Item -LiteralPath $dir -Force }
        }
    }
}

function Assert-ManifestHash([object]$Expected, [string]$Path, [string]$Label) {
    if (-not $Expected) { throw "INVALID: manifest hash missing for $Label" }
    $actual = Get-Sha256 $Path
    if ($actual -ne ([string]$Expected).ToLowerInvariant()) {
        throw "INVALID: $Label hash mismatch. expected=$Expected actual=$actual"
    }
}

function Invoke-Validate {
    Write-Log "Validating canonical DMA stack: $Stack"
    $required = @(
        "include\vmmdll.h","include\leechcore.h",
        "lib\vmm.lib","lib\leechcore.lib",
        "bin\vmm.dll","bin\leechcore.dll","bin\FTD3XX.dll",
        "versions.json","managed-files.json"
    )
    foreach ($relative in $required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Stack $relative) -PathType Leaf)) {
            throw "INVALID: missing $relative"
        }
    }

    foreach ($dll in Get-ChildItem -LiteralPath (Join-Path $Stack "bin") -Filter *.dll -File) {
        if (-not (Test-PeX64 $dll.FullName)) { throw "INVALID: $($dll.Name) is not AMD64." }
    }

    $manifest = Get-Content -LiteralPath (Join-Path $Stack "versions.json") -Raw | ConvertFrom-Json
    Assert-ManifestHash $manifest.memprocfs.sha256.'vmm.dll' (Join-Path $Stack "bin\vmm.dll") "vmm.dll"
    Assert-ManifestHash $manifest.memprocfs.sha256.'vmm.lib' (Join-Path $Stack "lib\vmm.lib") "vmm.lib"
    Assert-ManifestHash $manifest.memprocfs.sha256.'vmmdll.h' (Join-Path $Stack "include\vmmdll.h") "vmmdll.h"
    Assert-ManifestHash $manifest.leechcore.sha256.'leechcore.dll' (Join-Path $Stack "bin\leechcore.dll") "leechcore.dll"
    Assert-ManifestHash $manifest.leechcore.sha256.'leechcore.lib' (Join-Path $Stack "lib\leechcore.lib") "leechcore.lib"
    Assert-ManifestHash $manifest.leechcore.sha256.'leechcore.h' (Join-Path $Stack "include\leechcore.h") "leechcore.h"

    $legacyManaged = @(
        "DMALibrary\libs\vmmdll.h","DMALibrary\libs\leechcore.h","DMALibrary\libs\vmm.lib","DMALibrary\libs\leechcore.lib",
        "DMALibrary\info.db"
    )
    foreach ($relative in $legacyManaged) {
        if (Test-Path -LiteralPath (Join-Path $Root $relative)) {
            throw "INVALID: legacy managed dependency still exists: $relative"
        }
    }

    if (Test-Path -LiteralPath (Join-Path $Stack "data\info.db") -PathType Leaf) {
        throw "INVALID: info.db is not part of the OmniGhost runtime dependency set."
    }

    $projectPath = Join-Path $Root "OmiGhost.vcxproj"
    $project = Get-Content -LiteralPath $projectPath -Raw
    if ($project -match 'DMALibrary\\libs') { throw "INVALID: OmiGhost.vcxproj still references DMALibrary\libs." }
    if ($project -notmatch 'third_party\\dma_stack\\include') { throw "INVALID: canonical include path not configured." }
    if ($project -notmatch 'third_party\\dma_stack\\lib') { throw "INVALID: canonical lib path not configured." }
    if ($project -notmatch 'third_party\\dma_stack\\bin') { throw "INVALID: canonical runtime path not configured." }

    Write-Log "DMA dependency stack: VALID"
}

function Invoke-Doctor {
    Invoke-Validate
    $custom = Join-Path $Root "runtime\own"
    if (Test-Path -LiteralPath $custom) {
        foreach ($dll in Get-ChildItem -LiteralPath $custom -Filter *.dll -File) {
            if (-not (Test-PeX64 $dll.FullName)) { throw "INVALID custom runtime: $($dll.Name) is not AMD64." }
            Write-Log "Custom runtime OK: $($dll.Name) sha256=$(Get-Sha256 $dll.FullName)"
        }
    }
    Write-Log "Doctor: PASS"
}

function Invoke-Status {
    Write-Log "Canonical stack=$Stack"
    if (-not (Test-Path -LiteralPath (Join-Path $Stack "versions.json"))) {
        Write-Log "Status: versions.json missing"
        return
    }
    Get-Content -LiteralPath (Join-Path $Stack "versions.json") | ForEach-Object { Write-Log $_ }
}

function Invoke-Rollback {
    if (-not (Test-Path -LiteralPath $Backup -PathType Container)) {
        throw "No rollback backup exists at $Backup"
    }
    $failed = Join-Path $Root ("third_party\dma_stack.failed-{0:yyyyMMdd-HHmmss}" -f (Get-Date))
    if (Test-Path -LiteralPath $Stack) { Rename-Item -LiteralPath $Stack -NewName (Split-Path -Leaf $failed) }
    Rename-Item -LiteralPath $Backup -NewName "dma_stack"
    Remove-LegacyManagedCopies
    Invoke-Validate
    Write-Log "Rollback completed. Previous active stack preserved as $(Split-Path -Leaf $failed)"
}

Write-Log "Root=$Root"
Write-Log "Log=$Log"

if ($Rollback) { Invoke-Rollback; return }
if ($Status) { Invoke-Status; return }
if ($Validate) { Invoke-Validate; return }
if ($Doctor) { Invoke-Doctor; return }

if ($Check -or $Latest) {
    $apiMp = "https://api.github.com/repos/ufrisk/MemProcFS/releases/latest"
    $apiLc = "https://api.github.com/repos/ufrisk/LeechCore/releases/latest"
    Assert-OfficialGitHubUri $apiMp "MemProcFS API"
    Assert-OfficialGitHubUri $apiLc "LeechCore API"
    Write-Log "Querying official GitHub releases..."
    $headers = @{ "User-Agent" = "OmniGhost-DependencyManager/2" }
    $mp = Invoke-RestMethod -Uri $apiMp -Headers $headers
    $lc = Invoke-RestMethod -Uri $apiLc -Headers $headers
    Write-Log "Latest MemProcFS tag=$($mp.tag_name)"
    Write-Log "Latest LeechCore tag=$($lc.tag_name)"

    $mpAsset = $mp.assets | Where-Object {
        $_.name -match 'files_and_binaries' -and $_.name -match 'win_x64' -and $_.name -notmatch 'linux|macos|aarch|arm'
    } | Select-Object -First 1
    $lcAsset = $lc.assets | Where-Object {
        $_.name -match 'files_and_binaries' -and $_.name -match 'win_x64' -and $_.name -notmatch 'LeechAgent|linux|macos|aarch|arm'
    } | Select-Object -First 1
    if (-not $mpAsset) { throw "No official MemProcFS Windows x64 files_and_binaries asset found." }
    if (-not $lcAsset) { throw "No official LeechCore Windows x64 files_and_binaries asset found." }

    Assert-OfficialGitHubUri $mpAsset.browser_download_url "MemProcFS asset"
    Assert-OfficialGitHubUri $lcAsset.browser_download_url "LeechCore asset"
    Write-Log "MemProcFS asset=$($mpAsset.name)"
    Write-Log "LeechCore asset=$($lcAsset.name)"

    if ($Check -and -not $Latest) {
        Invoke-Status
        return
    }

    if ($Latest) {
        $downloadDir = Join-Path $TempRoot ([guid]::NewGuid().ToString("N"))
        New-Item -ItemType Directory -Force -Path $downloadDir | Out-Null
        $MemProcFSZip = Join-Path $downloadDir $mpAsset.name
        $LeechCoreZip = Join-Path $downloadDir $lcAsset.name
        if ($DryRun) {
            Write-Log "DryRun: would download $($mpAsset.browser_download_url)"
            Write-Log "DryRun: would download $($lcAsset.browser_download_url)"
            return
        }
        Invoke-WebRequest -Uri $mpAsset.browser_download_url -OutFile $MemProcFSZip -UseBasicParsing -Headers $headers
        Invoke-WebRequest -Uri $lcAsset.browser_download_url -OutFile $LeechCoreZip -UseBasicParsing -Headers $headers
    }
}

if (-not $MemProcFSZip -or -not $LeechCoreZip) {
    throw "Provide -MemProcFSZip and -LeechCoreZip, or use -Latest."
}
if (-not (Test-Path -LiteralPath $MemProcFSZip -PathType Leaf)) { throw "MemProcFS zip not found: $MemProcFSZip" }
if (-not (Test-Path -LiteralPath $LeechCoreZip -PathType Leaf)) { throw "LeechCore zip not found: $LeechCoreZip" }

if ($DryRun) {
    Write-Log "DryRun: local archives validated as present; no files changed."
    return
}

$work = Join-Path $TempRoot ([guid]::NewGuid().ToString("N"))
$mpExt = Join-Path $work "memprocfs"
$lcExt = Join-Path $work "leechcore"
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    Write-Log "Extracting MemProcFS into isolated staging..."
    Expand-ZipNormalized $MemProcFSZip $mpExt
    Write-Log "Extracting LeechCore into isolated staging..."
    Expand-ZipNormalized $LeechCoreZip $lcExt
    Get-Required $mpExt @("vmmdll.h","vmm.lib","vmm.dll","leechcore.dll","leechcore.lib")
    Get-Required $lcExt @("leechcore.h","leechcore.lib","leechcore.dll")

    Install-FromExtracts $mpExt $lcExt $Staging

    # Validate staging by temporarily targeting it through the same basic file/hash rules.
    foreach ($required in @("include\vmmdll.h","include\leechcore.h","lib\vmm.lib","lib\leechcore.lib",
                            "bin\vmm.dll","bin\leechcore.dll","versions.json")) {
        if (-not (Test-Path -LiteralPath (Join-Path $Staging $required))) {
            throw "Staging validation failed: missing $required"
        }
    }

    if (Test-Path -LiteralPath $Backup) { Remove-Item -LiteralPath $Backup -Recurse -Force }
    if (Test-Path -LiteralPath $Stack) {
        Rename-Item -LiteralPath $Stack -NewName "dma_stack.backup"
        Write-Log "Backup created: $Backup"
    }
    Rename-Item -LiteralPath $Staging -NewName "dma_stack"
    Write-Log "Canonical stack installed: $Stack"

    Remove-LegacyManagedCopies
    Invoke-Validate

    if ($Build) {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        $msbuild = $null
        if (Test-Path -LiteralPath $vswhere) {
            $msbuild = & $vswhere -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe 2>$null |
                Select-Object -First 1
        }
        if (-not $msbuild) {
            throw "MSBuild was requested but could not be found."
        }

        Write-Log "Building Release|x64..."
        & $msbuild (Join-Path $Root "OmiGhost.vcxproj") /p:Configuration=Release /p:Platform=x64 /m
        if ($LASTEXITCODE -ne 0) { throw "Release x64 build failed." }
        Write-Log "Build PASS"
    }

    Write-Log "Dependencies updated successfully."
} catch {
    Write-Log "ERROR: $_"
    if (Test-Path -LiteralPath $Stack) {
        $failed = Join-Path $Root ("third_party\dma_stack.failed-{0:yyyyMMdd-HHmmss}" -f (Get-Date))
        Rename-Item -LiteralPath $Stack -NewName (Split-Path -Leaf $failed) -ErrorAction SilentlyContinue
    }
    if (Test-Path -LiteralPath $Backup) {
        Rename-Item -LiteralPath $Backup -NewName "dma_stack" -ErrorAction SilentlyContinue
        Write-Log "Rollback restored previous canonical stack."
    }
    throw
} finally {
    if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
    if (Test-Path -LiteralPath $Staging) { Remove-Item -LiteralPath $Staging -Recurse -Force -ErrorAction SilentlyContinue }
}
