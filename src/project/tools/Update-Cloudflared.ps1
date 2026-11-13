[CmdletBinding()]
param(
    [string]$ProjectDir = "",
    [switch]$Latest,
    [switch]$EnsureLatest,
    [switch]$Ensure,
    [switch]$Check,
    [switch]$Validate,
    [switch]$DryRun
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Hashing.ps1')






$securityModule = Join-Path $PSHOME 'Modules\Microsoft.PowerShell.Security\Microsoft.PowerShell.Security.psd1'
if (-not (Test-Path -LiteralPath $securityModule -PathType Leaf)) {
    throw "Microsoft.PowerShell.Security was not found for the active PowerShell host: $securityModule"
}
Import-Module -Name $securityModule -Force -ErrorAction Stop

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
if ($PSScriptRoot) { $ProjectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..')) }
    else { throw "ProjectDir is required when $PSScriptRoot is unavailable." }
}
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

$ExePath = Join-Path $ProjectDir 'src\runtime\cloudflared\cloudflared.exe'
$ManifestPath = Join-Path $ProjectDir 'src\project\config\release\versions.json'
$NoticesPath = Join-Path $ProjectDir 'src\project\support\docs\legal\THIRD_PARTY_NOTICES.txt'
$LogDir = Join-Path $ProjectDir '.cache\logs'
$TempRoot = Join-Path $ProjectDir '.cache\temp\cloudflared_update'
$ApiUrl = 'https://api.github.com/repos/cloudflare/cloudflared/releases/latest'
$AllowedHosts = @('api.github.com', 'github.com', 'objects.githubusercontent.com', 'release-assets.githubusercontent.com')
$AssetName = 'cloudflared-windows-amd64.exe'
$AssetMachine = 0x8664
$AssetArchLabel = 'x64'
$SignerName = 'Cloudflare, Inc.'
$MaxDownloadBytes = 128MB

try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch {}

New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
$Log = Join-Path $LogDir ("cloudflared-update-{0:yyyyMMdd-HHmmss}.log" -f (Get-Date))
$script:LogLines = New-Object System.Collections.Generic.List[string]
$script:Changed = $false

function Write-Log([string]$Message) {
    $line = "[{0:HH:mm:ss}] {1}" -f (Get-Date), $Message
    Write-Host $line
    $script:LogLines.Add($line)
}

function Save-Log {
    try {
        [IO.File]::WriteAllLines($Log, $script:LogLines, (New-Object Text.UTF8Encoding($false)))
    } catch {}
}

function Assert-OfficialGitHubUri([string]$Uri, [string]$Context) {
    $parsed = [Uri]$Uri
    if ($parsed.Scheme -ne 'https') { throw "$Context must use HTTPS: $Uri" }
    if ($AllowedHosts -notcontains $parsed.DnsSafeHost.ToLowerInvariant()) {
        throw "$Context uses an unapproved host: $($parsed.DnsSafeHost)"
    }
}

function Test-CloudflaredSignature([string]$Path, [string]$Context) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    $signature = Get-AuthenticodeSignature $Path
    if ($signature.Status -ne 'Valid') {
        Write-Log "Signature check failed ($Context): status=$($signature.Status)."
        return $false
    }
    $subject = [string]$signature.SignerCertificate.Subject
    if ($subject -notlike "*$SignerName*") {
        Write-Log "Signature check failed ($Context): unexpected signer '$subject'."
        return $false
    }
    return $true
}

function Test-CloudflaredPeMachine([string]$Path, [int]$ExpectedMachine) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        if ($stream.Length -lt 0x100) { return $false }
        $reader = New-Object IO.BinaryReader($stream)
        if ($reader.ReadUInt16() -ne 0x5A4D) { return $false }
        $stream.Position = 0x3C
        $pe = $reader.ReadInt32()
        if ($pe -le 0 -or ($pe + 6) -ge $stream.Length) { return $false }
        $stream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x00004550) { return $false }
        return ($reader.ReadUInt16() -eq $ExpectedMachine)
    } finally {
        $stream.Dispose()
    }
}

function Get-CloudflaredReportedVersion([string]$Path) {
    $probeDir = Join-Path $TempRoot ([guid]::NewGuid().ToString('N'))
    try {
        New-Item -ItemType Directory -Force -Path $probeDir | Out-Null
        Push-Location $probeDir
        try {
            $output = & $Path --version
            $text = (@($output) -join ' ')
            $match = [regex]::Match($text, 'version\s+(\S+)')
            if ($match.Success) { return $match.Groups[1].Value }
        } finally {
            Pop-Location
        }
    } catch {
        Write-Log "--version probe failed: $_"
    } finally {
        if (Test-Path -LiteralPath $probeDir) {
            Remove-Item -LiteralPath $probeDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
    return 'unknown'
}

function Remove-StrayCloudflaredFiles {

    $stray = Join-Path (Split-Path -Parent $ExePath) 'config.yml'
    if (Test-Path -LiteralPath $stray -PathType Leaf) {
        Remove-Item -LiteralPath $stray -Force -ErrorAction SilentlyContinue
        Write-Log 'Removed stray cloudflared config.yml next to the binary.'
    }
}

function Get-CloudflaredPin {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { return $null }
    try {
        $json = [IO.File]::ReadAllText($ManifestPath) | ConvertFrom-Json
    } catch {
        Write-Log "versions.json is unreadable: $_"
        return $null
    }
    $section = $json.PSObject.Properties['cloudflared']
    if (-not $section) { return $null }
    $value = $section.Value
    $version = ''
    $sha256 = ''
    $versionProp = $value.PSObject.Properties['version']
    if ($versionProp) { $version = [string]$versionProp.Value }
    $shaProp = $value.PSObject.Properties['sha256']
    if ($shaProp) {
        $exeProp = $shaProp.Value.PSObject.Properties['cloudflared.exe']
        if ($exeProp) { $sha256 = ([string]$exeProp.Value).ToLowerInvariant() }
    }
    return [pscustomobject]@{ Version = $version; Sha256 = $sha256 }
}

function Set-NoticesVersion([string]$Version) {
    if (-not (Test-Path -LiteralPath $NoticesPath -PathType Leaf)) {
        Write-Log 'THIRD_PARTY_NOTICES.txt missing; version line not synced.'
        return
    }
    $text = [IO.File]::ReadAllText($NoticesPath)
    $pattern = '(?m)^(cloudflared\r?\n\s*Upstream:[^\r\n]*\r?\n\s*Version bundled:).*$'
    if (-not [regex]::IsMatch($text, $pattern)) {
        Write-Log 'THIRD_PARTY_NOTICES.txt cloudflared line not found; version line not synced.'
        return
    }
    $replacement = "`${1} $Version (Windows $AssetArchLabel)."
    $updated = [regex]::Replace($text, $pattern, $replacement)
    if ($updated -eq $text) {
        Write-Log "THIRD_PARTY_NOTICES.txt already shows cloudflared $Version."
        return
    }
    [IO.File]::WriteAllText($NoticesPath, $updated, (New-Object Text.UTF8Encoding($false)))
    Write-Log "THIRD_PARTY_NOTICES.txt synced: Version bundled: $Version (Windows $AssetArchLabel)."
}

function Set-CloudflaredPin([string]$Version, [string]$Sha256) {
    $json = [IO.File]::ReadAllText($ManifestPath) | ConvertFrom-Json
    $architecture = 'win-x86'
    if ($AssetMachine -eq 0x8664) { $architecture = 'win-x64' }
    $section = [pscustomobject]@{
        updated_at = (Get-Date).ToUniversalTime().ToString('o')
        asset = $AssetName
        version = $Version
        architecture = $architecture
        source = 'https://github.com/cloudflare/cloudflared/releases/latest'
        sha256 = [pscustomobject]@{ 'cloudflared.exe' = $Sha256.ToLowerInvariant() }
    }
    $existing = $json.PSObject.Properties['cloudflared']
    if ($existing) { $existing.Value = $section }
    else { $json | Add-Member -MemberType NoteProperty -Name 'cloudflared' -Value $section }
    $serialized = ($json | ConvertTo-Json -Depth 10)
    [IO.File]::WriteAllText($ManifestPath, $serialized + "`n", (New-Object Text.UTF8Encoding($false)))
    Write-Log "versions.json pin synced: cloudflared $Version (sha256=$($Sha256.ToLowerInvariant()))."
    Set-NoticesVersion $Version
}

function Get-CloudflaredGitHubLatest {
    Assert-OfficialGitHubUri $ApiUrl 'cloudflared API'
    $headers = @{ 'User-Agent' = 'OmniGhost-DependencyManager/2'; 'Accept' = 'application/vnd.github+json' }
    $release = Invoke-RestMethod -Uri $ApiUrl -Headers $headers -TimeoutSec 30
    $asset = $release.assets | Where-Object { $_.name -ieq $AssetName } | Select-Object -First 1
    if (-not $asset) { throw "Official cloudflared release $($release.tag_name) has no asset named $AssetName." }
    Assert-OfficialGitHubUri $asset.browser_download_url 'cloudflared asset'
    $sha256 = ''
    $digest = $asset.PSObject.Properties['digest']
    if ($digest -and $digest.Value -like 'sha256:*') {
        $sha256 = ([string]$digest.Value).Substring(7).ToLowerInvariant()
    }
    if (-not $sha256) {
        $match = [regex]::Match([string]$release.body, ([regex]::Escape($AssetName) + '\s*:\s*([0-9a-fA-F]{64})'))
        if ($match.Success) { $sha256 = $match.Groups[1].Value.ToLowerInvariant() }
    }
    if (-not $sha256) { throw "Could not resolve the official SHA-256 for $AssetName from the GitHub release." }
    return [pscustomobject]@{
        Version = [string]$release.tag_name
        Url = [string]$asset.browser_download_url
        Sha256 = $sha256
        Size = [int64]$asset.size
    }
}

function Install-CloudflaredFromGitHub([object]$Release) {
    if (-not $Release) { $Release = Get-CloudflaredGitHubLatest }
    Write-Log "Official cloudflared: $($Release.Version) ($AssetName, sha256=$($Release.Sha256))."
    if ($DryRun) {
        Write-Log "DryRun: would download $($Release.Url) and replace src\runtime\cloudflared\cloudflared.exe."
        return $Release
    }
    $work = Join-Path $TempRoot ([guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    $download = Join-Path $work $AssetName
    try {
        Write-Log "Downloading $($Release.Url) ..."
        $headers = @{ 'User-Agent' = 'OmniGhost-DependencyManager/2' }
        Invoke-WebRequest -Uri $Release.Url -OutFile $download -UseBasicParsing -Headers $headers -TimeoutSec 300
        $size = (Get-Item -LiteralPath $download).Length
        if ($Release.Size -gt 0 -and $size -ne $Release.Size) {
            throw "Downloaded size mismatch: $size bytes <> official $($Release.Size) bytes."
        }
        $hash = Get-OmniGhostSha256 -LiteralPath $download
        if ($hash -ne $Release.Sha256) {
            throw "Downloaded cloudflared SHA-256 mismatch: expected=$($Release.Sha256) actual=$hash."
        }
        Write-Log 'Downloaded SHA-256 matches the official GitHub release digest.'
        if (-not (Test-CloudflaredSignature $download 'download')) {
            throw "Downloaded cloudflared is not validly signed by $SignerName."
        }
        if (-not (Test-CloudflaredPeMachine $download $AssetMachine)) {
            throw ("Downloaded cloudflared PE machine is not 0x{0:X}." -f $AssetMachine)
        }
        $version = Get-CloudflaredReportedVersion $download
        $targetDir = Split-Path -Parent $ExePath
        if (-not (Test-Path -LiteralPath $targetDir -PathType Container)) {
            New-Item -ItemType Directory -Force -Path $targetDir | Out-Null
        }
        try {
            Copy-Item -LiteralPath $download -Destination $ExePath -Force
        } catch {
            throw "Could not replace cloudflared.exe (is the file locked by another process?): $_"
        }
        Set-CloudflaredPin $Release.Version $hash
        Write-Log "Installed cloudflared $($Release.Version) -> src\runtime\cloudflared\cloudflared.exe."
        $script:Changed = $true
        return $Release
    } finally {
        if (Test-Path -LiteralPath $work) { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
    }
}

function Adopt-LocalCloudflared {
    Write-Log 'cloudflared.exe differs from the versions.json pin; verifying the Cloudflare signature before adopting it...'
    if (-not (Test-CloudflaredSignature $ExePath 'local adoption')) { return $false }
    if (-not (Test-CloudflaredPeMachine $ExePath $AssetMachine)) {
        Write-Log 'Local cloudflared PE machine is unexpected; refusing to adopt.'
        return $false
    }
    $hash = Get-OmniGhostSha256 -LiteralPath $ExePath
    $version = Get-CloudflaredReportedVersion $ExePath
    Set-CloudflaredPin $version $hash
    Write-Log "Adopted signed local cloudflared $version (sha256=$hash); pin synced without download."
    $script:Changed = $true
    return $true
}

function Invoke-CloudflaredEnsure {
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        if ($DryRun) {
            Write-Log 'DryRun: cloudflared.exe is missing; would download the official latest release.'
            return
        }
        Write-Log 'cloudflared.exe is missing; downloading the official latest release...'
        Install-CloudflaredFromGitHub $null | Out-Null
        return
    }
    $pin = Get-CloudflaredPin
    $localHash = Get-OmniGhostSha256 -LiteralPath $ExePath
    if ($pin -and $pin.Sha256 -eq $localHash) {
        Write-Log "cloudflared OK: $($pin.Version) matches the versions.json pin (no download needed)."
        return
    }
    if ($DryRun) {
        Write-Log 'DryRun: cloudflared.exe differs from the versions.json pin; would adopt a signed local file or re-download.'
        return
    }
    if (Adopt-LocalCloudflared) { return }
    Write-Log 'Local cloudflared could not be verified; refreshing from the official GitHub release...'
    Install-CloudflaredFromGitHub $null | Out-Null
}

function Invoke-CloudflaredEnsureLatest {
    Invoke-CloudflaredEnsure
    if ($DryRun) { return }
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) { throw 'cloudflared.exe is missing after the ensure step.' }
    $localHash = Get-OmniGhostSha256 -LiteralPath $ExePath
    try {
        $latestRelease = Get-CloudflaredGitHubLatest
    } catch {
        $pin = Get-CloudflaredPin
        if ($pin -and $pin.Sha256 -eq $localHash) {
            Write-Log "GitHub check unavailable ($($_.Exception.Message)); keeping the pinned cloudflared $($pin.Version)."
            return
        }
        throw
    }
    $pin = Get-CloudflaredPin
    if ($localHash -eq $latestRelease.Sha256) {
        if ($pin -and $pin.Version -eq $latestRelease.Version -and $pin.Sha256 -eq $localHash) {
            Write-Log "cloudflared is CURRENT: $($latestRelease.Version) (SHA-256 verified against the official digest)."
            return
        }
        Set-CloudflaredPin $latestRelease.Version $localHash
        $script:Changed = $true
        Write-Log "cloudflared binary already matches official $($latestRelease.Version); metadata synced."
        return
    }
    if ($pin -and $pin.Sha256 -eq $localHash) {
        Write-Log "Official asset for $($latestRelease.Version) changed upstream; refreshing the local copy."
        Install-CloudflaredFromGitHub $latestRelease | Out-Null
        return
    }
    Write-Log "Update required: local pin=$($pin.Version) <> official latest=$($latestRelease.Version)."
    Install-CloudflaredFromGitHub $latestRelease | Out-Null
}

function Assert-CloudflaredFinalState {
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) { throw 'cloudflared.exe is missing after the update flow.' }
    if (-not (Test-CloudflaredSignature $ExePath 'final verification')) {
        throw "cloudflared.exe is not a valid $SignerName-signed binary; refusing to continue."
    }
    $pin = Get-CloudflaredPin
    $hash = Get-OmniGhostSha256 -LiteralPath $ExePath
    if (-not $pin -or -not $pin.Sha256 -or $pin.Sha256 -ne $hash) {
        throw "cloudflared.exe is not in sync with the versions.json pin (pin=$($pin.Sha256) file=$hash)."
    }
    Remove-StrayCloudflaredFiles
    Write-Log "cloudflared final state OK: version=$($pin.Version) sha256=$hash."
}

Write-Log "ProjectDir=$ProjectDir"

if ($Validate) {
    if (-not (Test-Path -LiteralPath $ExePath -PathType Leaf)) {
        Write-Log "INVALID: missing $ExePath"
        Save-Log
        exit 1
    }
    $pin = Get-CloudflaredPin
    $localHash = Get-OmniGhostSha256 -LiteralPath $ExePath
    $signatureOk = Test-CloudflaredSignature $ExePath 'validate'
    Write-Log "local sha256=$localHash"
    Write-Log "pin sha256=$(if ($pin) { $pin.Sha256 } else { '<missing>' })"
    Write-Log "pin version=$(if ($pin) { $pin.Version } else { '<missing>' })"
    Write-Log "signature valid=$signatureOk"
    if (-not $pin -or -not $pin.Sha256 -or $pin.Sha256 -ne $localHash -or -not $signatureOk) {
        Write-Log 'INVALID: cloudflared state is not consistent.'
        Save-Log
        exit 1
    }
    Write-Log 'VALID: cloudflared matches the versions.json pin and is Cloudflare-signed.'
    Save-Log
    exit 0
}

if ($Check) {
    if (Test-Path -LiteralPath $ExePath -PathType Leaf) {
        $pin = Get-CloudflaredPin
        $localHash = Get-OmniGhostSha256 -LiteralPath $ExePath
        Write-Log "local sha256=$localHash"
        if ($pin) {
            Write-Log "pin version=$($pin.Version) sha256=$($pin.Sha256)"
            Write-Log "signature valid=$(Test-CloudflaredSignature $ExePath 'check')"
        } else {
            Write-Log 'pin: <missing section in versions.json>'
        }
    } else {
        Write-Log "local: cloudflared.exe is missing ($ExePath)."
    }
    try {
        $latestRelease = Get-CloudflaredGitHubLatest
        Write-Log "GitHub latest: $($latestRelease.Version) (sha256=$($latestRelease.Sha256))."
        if ($pin -and $pin.Version -eq $latestRelease.Version -and $localHash -eq $latestRelease.Sha256) {
            Write-Log 'Status: CURRENT.'
        } else {
            Write-Log 'Status: UPDATE AVAILABLE.'
        }
    } catch {
        Write-Log "GitHub check unavailable: $_"
        Write-Log ("Stack: " + ($_.ScriptStackTrace -replace '\r?\n', ' | '))
    }
    Save-Log
    exit 0
}

try {
    if ($Latest) {
        Install-CloudflaredFromGitHub $null | Out-Null
    } elseif ($EnsureLatest) {
        Invoke-CloudflaredEnsureLatest
    } elseif ($Ensure) {
        Invoke-CloudflaredEnsure
    } else {
        Write-Log 'No mode selected. Use -Latest, -EnsureLatest, -Ensure, -Check or -Validate.'
        Save-Log
        exit 2
    }

    if (-not $DryRun) { Assert-CloudflaredFinalState }

    if ($script:Changed) {
        Save-Log
        Write-Log "cloudflared updated successfully. Commit the refreshed exe, versions.json and THIRD_PARTY_NOTICES.txt before a Publish build."
    }
    exit 0
} catch {
    Write-Log "ERROR: $_"
    Save-Log
    exit 1
}
