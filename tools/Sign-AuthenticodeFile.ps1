[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Path,
    [Parameter(Mandatory = $true)][string]$CertificateThumbprint,
    [Parameter(Mandatory = $true)][string]$TimestampUrl
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleaseTrustTools.ps1')
$certificate = Get-ReleaseSigningCertificate -Thumbprint $CertificateThumbprint -RequirePrivateKey
$file = [IO.Path]::GetFullPath($Path)
if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw 'The Authenticode input file does not exist.' }
$timestamp = $null
if (-not [Uri]::TryCreate($TimestampUrl, [UriKind]::Absolute, [ref]$timestamp) -or $timestamp.Scheme -cne 'https') {
    throw 'An HTTPS timestamp URL is required.'
}
$signTool = Get-ReleaseSignTool
& $signTool sign /fd SHA256 /sha1 $certificate.Thumbprint /s My /tr $TimestampUrl /td SHA256 $file
if ($LASTEXITCODE -ne 0) { throw "SignTool signing failed with exit code $LASTEXITCODE." }
& $signTool verify /pa /all /v $file
if ($LASTEXITCODE -ne 0) { throw "SignTool verification failed with exit code $LASTEXITCODE." }
$signature = Get-AuthenticodeSignature -LiteralPath $file
if ($signature.Status -ne 'Valid' -or $null -eq $signature.SignerCertificate -or
    $signature.SignerCertificate.Thumbprint -cne $certificate.Thumbprint) {
    throw 'The Authenticode signer does not match the selected trusted publisher.'
}
$global:LASTEXITCODE = 0
