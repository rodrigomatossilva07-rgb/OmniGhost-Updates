[CmdletBinding()]
param(
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$OutputDir,
    [string]$Destination
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if (-not $OutputDir) { $OutputDir = Join-Path $ProjectDir 'build\Publish' }
if (-not $Destination) { $Destination = Join-Path $ProjectDir 'artifacts\OmniGhost-runtime.zip' }
$OutputDir = [IO.Path]::GetFullPath($OutputDir)
$Destination = [IO.Path]::GetFullPath($Destination)

$exe = Join-Path $OutputDir 'OmniGhost.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Runtime executable missing: $exe" }
$unexpected = @(Get-ChildItem -LiteralPath $OutputDir -Recurse -Force -File | Where-Object FullName -ne $exe)
if ($unexpected.Count -gt 0) { throw "Customer runtime must contain only OmniGhost.exe: $($unexpected[0].FullName)" }

$stage = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-single-exe-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
try {
    Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'OmniGhost.exe') -Force
    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    if (Test-Path -LiteralPath $Destination) { Remove-Item -LiteralPath $Destination -Force }
    Compress-Archive -LiteralPath (Join-Path $stage 'OmniGhost.exe') -DestinationPath $Destination -CompressionLevel Optimal
    $stream = [IO.File]::OpenRead($Destination)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { $hash = ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant() }
        finally { $sha.Dispose() }
    }
    finally { $stream.Dispose() }
    Write-Host "Runtime package (single EXE): $Destination" -ForegroundColor Green
    Write-Host "SHA256: $hash"
}
finally { Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue }
