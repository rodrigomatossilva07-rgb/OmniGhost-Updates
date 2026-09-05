[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Path,
    [Parameter(Mandatory=$true)][string]$CertificateThumbprint,
    [Parameter(Mandatory=$false)][string]$TimestampUrl = 'http://timestamp.digicert.com',
    [ValidateSet('CurrentUser','LocalMachine')][string]$StoreLocation = 'CurrentUser'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Path = [IO.Path]::GetFullPath($Path)
$thumb = ($CertificateThumbprint -replace '\s','').ToUpperInvariant()
if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "File not found: $Path" }
$cert = Get-Item -LiteralPath "Cert:\$StoreLocation\My\$thumb" -ErrorAction Stop
if (-not $cert.HasPrivateKey) { throw 'Signing certificate does not have a private key.' }

$signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $signtool) {
    $kits = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $kits) {
        $signtool = Get-ChildItem -LiteralPath $kits -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name -Descending |
            ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
    }
}
if (-not $signtool) { throw 'signtool.exe not found. Install the Windows SDK.' }
$signtoolPath = if ($signtool -is [string]) { $signtool } else { $signtool.Source }
$args = @('sign','/sha1',$thumb,'/fd','SHA256','/tr',$TimestampUrl,'/td','SHA256')
if ($StoreLocation -eq 'LocalMachine') { $args += '/sm' }
$args += $Path
& $signtoolPath @args
if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE" }
$signature = Get-AuthenticodeSignature -LiteralPath $Path
if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) { throw "Authenticode validation failed after signing: $($signature.StatusMessage)" }
if (-not $signature.SignerCertificate -or ($signature.SignerCertificate.Thumbprint -replace '\s','').ToUpperInvariant() -ne $thumb) { throw 'Signed file is not signed by the configured certificate.' }
Write-Host "[OmniGhost Authenticode] Signed and verified: $Path"
