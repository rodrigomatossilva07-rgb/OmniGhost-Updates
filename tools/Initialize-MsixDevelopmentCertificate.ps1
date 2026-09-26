[CmdletBinding()]
param(
    [string]$Subject = 'CN=Omnighost Development',
    [int]$ValidYears = 3
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$existing = Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -ceq $Subject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(30) } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if ($null -eq $existing) {
    $existing = New-SelfSignedCertificate -Type CodeSigningCert -Subject $Subject `
        -CertStoreLocation Cert:\CurrentUser\My -HashAlgorithm SHA256 `
        -KeyAlgorithm RSA -KeyLength 3072 -KeyExportPolicy NonExportable `
        -NotAfter (Get-Date).AddYears($ValidYears)
}

$trustedPath = "Cert:\CurrentUser\TrustedPeople\$($existing.Thumbprint)"
if (-not (Test-Path -LiteralPath $trustedPath)) {
    $temporaryCertificate = Join-Path ([IO.Path]::GetTempPath()) "Omnighost-$($existing.Thumbprint).cer"
    try {
        Export-Certificate -Cert $existing -FilePath $temporaryCertificate -Force | Out-Null
        Import-Certificate -FilePath $temporaryCertificate -CertStoreLocation Cert:\CurrentUser\TrustedPeople | Out-Null
    } finally {
        Remove-Item -LiteralPath $temporaryCertificate -Force -ErrorAction SilentlyContinue
    }
}

[Environment]::SetEnvironmentVariable('OMNIGHOST_MSIX_CERT_THUMBPRINT', $existing.Thumbprint, 'User')
[Environment]::SetEnvironmentVariable('OMNIGHOST_SIGNING_THUMBPRINT', $existing.Thumbprint, 'User')
$env:OMNIGHOST_MSIX_CERT_THUMBPRINT = $existing.Thumbprint
$env:OMNIGHOST_SIGNING_THUMBPRINT = $existing.Thumbprint

Write-Host '[MSIX] Development certificate ready.'
Write-Host "[MSIX] Subject: $($existing.Subject)"
Write-Host "[MSIX] Thumbprint: $($existing.Thumbprint)"
Write-Warning 'This self-signed certificate is trusted only for local development. Do not distribute it as a customer trust solution.'
