[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$InputPath,
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [ValidateSet('CurrentUser','LocalMachine')][string]$StoreLocation = 'CurrentUser'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$InputPath = [IO.Path]::GetFullPath($InputPath)
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$thumb = ($CertificateThumbprint -replace '\s','').ToUpperInvariant()
$cert = Get-Item -LiteralPath "Cert:\$StoreLocation\My\$thumb" -ErrorAction Stop
if (-not $cert.HasPrivateKey) { throw 'Signing certificate does not have a private key.' }
$rsa = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($cert)
if ($null -eq $rsa) { throw 'Manifest signing currently requires an RSA signing certificate.' }
try {
    $bytes = [IO.File]::ReadAllBytes($InputPath)
    $sig = $rsa.SignData($bytes, [System.Security.Cryptography.HashAlgorithmName]::SHA256, [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)
} finally {
    if ($rsa -is [IDisposable]) { $rsa.Dispose() }
}
$dir = Split-Path -Parent $OutputPath
if ($dir) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($OutputPath, [Convert]::ToBase64String($sig) + [Environment]::NewLine, $utf8)
Write-Host "[OmniGhost Manifest] Signed: $InputPath -> $OutputPath"
