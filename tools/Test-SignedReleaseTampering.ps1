[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$ReleaseDirectory
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$config = Get-Content -LiteralPath (Join-Path $ProjectDir 'config\release\release-publish.json') -Raw | ConvertFrom-Json
$thumb = ([string]$config.signingCertificateThumbprint -replace '\s','').ToUpperInvariant()
if ($thumb -notmatch '^[0-9A-F]{40}$') { throw 'No real signing certificate configured for tamper tests.' }
$certificate = $null
foreach ($store in @('CurrentUser','LocalMachine')) {
    try { $certificate = Get-Item -LiteralPath "Cert:\$store\My\$thumb" -ErrorAction Stop; break } catch {}
}
if (-not $certificate) { throw 'Configured public certificate is unavailable.' }
$rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
if (-not $rsa) { throw 'Configured certificate is not RSA.' }
$temp = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-Tamper-' + [guid]::NewGuid().ToString('N'))
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory((Join-Path $ReleaseDirectory 'OmniGhost.zip'), $temp)
    $exe = Join-Path $temp 'OmniGhost.exe'
    if ((Get-AuthenticodeSignature -LiteralPath $exe).Status -ne 'Valid') {
        throw 'Baseline EXE signature is not valid.'
    }
    $stream = [IO.File]::Open($exe, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    try {
        if ($stream.Length -le 4096) { throw 'EXE too small for tamper test.' }
        $stream.Position = 4096
        $original = $stream.ReadByte()
        $stream.Position = 4096
        $stream.WriteByte([byte]($original -bxor 1))
    } finally { $stream.Dispose() }
    if ((Get-AuthenticodeSignature -LiteralPath $exe).Status -eq 'Valid') {
        throw 'Tampered EXE was incorrectly accepted by Authenticode.'
    }

    foreach ($name in @('install-manifest.sha256','update.json')) {
        $manifest = Join-Path $ReleaseDirectory $name
        $signature = [Convert]::FromBase64String(([IO.File]::ReadAllText("$manifest.sig")).Trim())
        $bytes = [IO.File]::ReadAllBytes($manifest)
        $algorithm = [Security.Cryptography.HashAlgorithmName]::SHA256
        $padding = [Security.Cryptography.RSASignaturePadding]::Pkcs1
        if (-not $rsa.VerifyData($bytes, $signature, $algorithm, $padding)) {
            throw "Baseline signature invalid: $name"
        }
        $bytes[$bytes.Length - 1] = [byte]($bytes[$bytes.Length - 1] -bxor 1)
        if ($rsa.VerifyData($bytes, $signature, $algorithm, $padding)) {
            throw "Tampered manifest was accepted: $name"
        }
    }
    Write-Host '[Security] Tampered EXE and signed manifests were rejected.'
} finally {
    if ($rsa -is [IDisposable]) { $rsa.Dispose() }
    Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
}
