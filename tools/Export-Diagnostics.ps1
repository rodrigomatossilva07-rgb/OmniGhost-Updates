[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [Parameter(Mandatory=$false)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false

function Redact-Text([string]$Text) {
    if ($null -eq $Text) { return '' }
    $value = $Text
    foreach ($candidate in @($env:USERPROFILE, $env:USERNAME)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            $value = [regex]::Replace($value, [regex]::Escape($candidate), '<redacted-user>', 'IgnoreCase')
        }
    }
    # Conservative support-bundle redaction. Never preserve common secret forms.
    $value = [regex]::Replace($value, '(?im)(authorization\s*[:=]\s*)([^\r\n]+)', '$1<redacted>')
    $value = [regex]::Replace($value, '(?im)((?:license|licence|token|secret|password|api[_-]?key)\s*[:=]\s*)([^\s\r\n]+)', '$1<redacted>')
    $value = [regex]::Replace($value, '(?i)([?&](?:token|key|secret|password)=)[^&\s]+', '$1<redacted>')
    return $value
}

function Copy-RedactedText([string]$Source, [string]$Destination) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { return }
    $raw = [IO.File]::ReadAllText($Source)
    $safe = Redact-Text $raw
    $parent = Split-Path -Parent $Destination
    [IO.Directory]::CreateDirectory($parent) | Out-Null
    [IO.File]::WriteAllText($Destination, $safe, $Utf8NoBom)
}

function Copy-WhitelistedFile([string]$Relative, [string]$Staging) {
    $source = Join-Path $ProjectDir $Relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { return }
    $destination = Join-Path $Staging $Relative
    Copy-RedactedText $source $destination
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$localData = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'OmniGhost'
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $supportDir = Join-Path $localData 'support'
    [IO.Directory]::CreateDirectory($supportDir) | Out-Null
    $OutputPath = Join-Path $supportDir ("OmniGhost-Diagnostics-$timestamp.zip")
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
[IO.Directory]::CreateDirectory((Split-Path -Parent $OutputPath)) | Out-Null

$staging = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-Diagnostics-' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($staging) | Out-Null
try {
    foreach ($relative in @(
        'config\release\version.txt',
        'docs\validation-summary.json',
        'docs\SBOM.json',
        'docs\roadmap-status.json',
        'config\release\versions.json',
        'third_party\dma_stack\managed-files.json'
    )) {
        Copy-WhitelistedFile $relative $staging
    }

    $versionFile = Join-Path $ProjectDir 'config\release\version.txt'
    $projectVersion = 'unknown'
    if (Test-Path -LiteralPath $versionFile -PathType Leaf) {
        $projectVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
        if ([string]::IsNullOrWhiteSpace($projectVersion)) { $projectVersion = 'unknown' }
    }

    $system = [ordered]@{
        generated_utc = (Get-Date).ToUniversalTime().ToString('o')
        project_version = $projectVersion
        powershell = $PSVersionTable.PSVersion.ToString()
        os = [Environment]::OSVersion.VersionString
        process_architecture = [Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture.ToString()
        os_architecture = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString()
        source_root = '<redacted-project-root>'
    } | ConvertTo-Json -Depth 4
    [IO.File]::WriteAllText((Join-Path $staging 'system.json'), $system + [Environment]::NewLine, $Utf8NoBom)

    $logDir = Join-Path $localData 'logs'
    if (Test-Path -LiteralPath $logDir -PathType Container) {
        $logsOut = Join-Path $staging 'logs'
        [IO.Directory]::CreateDirectory($logsOut) | Out-Null
        $candidates = @(Get-ChildItem -LiteralPath $logDir -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Length -le 8MB -and $_.Extension -in @('.txt', '.log', '.jsonl') } |
            Sort-Object LastWriteTimeUtc -Descending |
            Select-Object -First 6)
        foreach ($file in $candidates) {
            Copy-RedactedText $file.FullName (Join-Path $logsOut $file.Name)
        }
        $archive = Join-Path $logDir 'archive'
        if (Test-Path -LiteralPath $archive -PathType Container) {
            $archiveOut = Join-Path $logsOut 'archive'
            [IO.Directory]::CreateDirectory($archiveOut) | Out-Null
            foreach ($file in @(Get-ChildItem -LiteralPath $archive -File -ErrorAction SilentlyContinue |
                    Where-Object { $_.Length -le 8MB } |
                    Sort-Object LastWriteTimeUtc -Descending |
                    Select-Object -First 4)) {
                Copy-RedactedText $file.FullName (Join-Path $archiveOut $file.Name)
            }
        }
    }

    $builtExe = Join-Path $ProjectDir 'build\OmniGhost.exe'
    if (Test-Path -LiteralPath $builtExe -PathType Leaf) {
        $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $builtExe
        $exeInfo = [ordered]@{
            file = 'OmniGhost.exe'
            size = (Get-Item -LiteralPath $builtExe).Length
            sha256 = $hash.Hash.ToLowerInvariant()
        } | ConvertTo-Json
        [IO.File]::WriteAllText((Join-Path $staging 'executable.json'), $exeInfo + [Environment]::NewLine, $Utf8NoBom)
    }

    $readme = @(
        'OmniGhost diagnostic support bundle',
        '',
        'This bundle is intentionally allowlisted and redacted.',
        'It does not include configuration files, licence.dat, updater tokens, memory dumps, or arbitrary user data.',
        'Review the archive before sharing it with a third party.'
    ) -join [Environment]::NewLine
    [IO.File]::WriteAllText((Join-Path $staging 'README.txt'), $readme + [Environment]::NewLine, $Utf8NoBom)

    if (Test-Path -LiteralPath $OutputPath) { Remove-Item -LiteralPath $OutputPath -Force }
    Compress-Archive -Path (Join-Path $staging '*') -DestinationPath $OutputPath -CompressionLevel Optimal
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash.ToLowerInvariant()
    Write-Host "Diagnostics bundle: $OutputPath"
    Write-Host "SHA256: $hash"
}
finally {
    Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
}
