[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [Parameter(Mandatory = $true)][string]$CertificateThumbprint
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleaseTrustTools.ps1')
$certificate = Get-ReleaseSigningCertificate -Thumbprint $CertificateThumbprint -RequirePrivateKey
$inputBytes = [IO.File]::ReadAllBytes([IO.Path]::GetFullPath($InputPath))
$privateKey = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($certificate)
if ($null -eq $privateKey) { throw 'The certificate RSA signing key is unavailable.' }
try {
    $signature = $privateKey.SignData($inputBytes, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($OutputPath), [Convert]::ToBase64String($signature) + "`n", (New-Object Text.UTF8Encoding($false)))
    Assert-ReleaseManifestSignature -InputPath $InputPath -SignaturePath $OutputPath -Certificate $certificate
} finally {
    $privateKey.Dispose()
}
$global:LASTEXITCODE = 0
Write-Host '[OmniGhost Trust] Detached RSA/SHA-256 manifest signature generated and verified.'
