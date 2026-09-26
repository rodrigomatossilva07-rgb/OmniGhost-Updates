[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$ReleaseDirectory,
    [Parameter(Mandatory=$true)][string]$Version
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$ReleaseDirectory = [IO.Path]::GetFullPath($ReleaseDirectory)
$config = Get-Content -LiteralPath (Join-Path $ProjectDir 'config\release\release-publish.json') -Raw | ConvertFrom-Json
$thumb = ([string]$config.signingCertificateThumbprint -replace '\s','').ToUpperInvariant()
$subject = [string]$config.expectedPublisherSubject
if ($config.requireAuthenticode -eq $true -and ($thumb -notmatch '^[0-9A-F]{40}$' -or [string]::IsNullOrWhiteSpace($subject))) {
    throw 'Commercial trust is not configured. Run Configure-ReleaseTrust.ps1 in the signing environment.'
}
$trustHeaderPath = Join-Path $ProjectDir 'src\updater\release_trust.h'
if (-not (Test-Path -LiteralPath $trustHeaderPath -PathType Leaf)) { throw 'release_trust.h is missing.' }
$trustHeader = [IO.File]::ReadAllText($trustHeaderPath)
if ($trustHeader -notmatch 'Configured\s*=\s*true' -or $trustHeader -notmatch [regex]::Escape($thumb)) {
    throw 'The compiled client trust anchor is not configured for this publisher. Run Configure-ReleaseTrust.ps1 and rebuild Release|x64 before packaging.'
}
$zip = Join-Path $ReleaseDirectory 'OmniGhost.zip'
$manifest = Join-Path $ReleaseDirectory 'update.json'
$sigPath = Join-Path $ReleaseDirectory 'update.json.sig'
foreach ($path in @($zip,$manifest,$sigPath)) { if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing commercial asset: $path" } }
$m = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ([string]$m.version -ne $Version) { throw "update.json version '$($m.version)' != '$Version'" }
$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
$zipSize = (Get-Item -LiteralPath $zip).Length
if ([string]$m.packages[0].sha256 -ne $zipHash -or [int64]$m.packages[0].size -ne [int64]$zipSize) { throw 'OmniGhost.zip does not match update.json.' }

# Verify detached manifest signature with the PUBLIC half of the configured certificate.
$cert = $null
foreach ($store in @('CurrentUser','LocalMachine')) {
    try { $cert = Get-Item -LiteralPath "Cert:\$store\My\$thumb" -ErrorAction Stop; break } catch {}
}
if (-not $cert) { throw 'Configured public certificate is not available in the local certificate store for validation.' }
$rsa = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($cert)
if ($null -eq $rsa) { throw 'Configured certificate is not RSA.' }
try {
    $manifestBytes = [IO.File]::ReadAllBytes($manifest)
    $signatureBytes = [Convert]::FromBase64String(([IO.File]::ReadAllText($sigPath)).Trim())
    if (-not $rsa.VerifyData($manifestBytes, $signatureBytes, [System.Security.Cryptography.HashAlgorithmName]::SHA256, [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
        throw 'Detached update.json signature is invalid.'
    }
} finally { if ($rsa -is [IDisposable]) { $rsa.Dispose() } }

# Verify the executable actually packaged for customers, not merely the local build copy.
$temp = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-Validate-' + [guid]::NewGuid().ToString('N'))
try {
    [IO.Compression.ZipFile]::ExtractToDirectory($zip, $temp)
    $exe = Join-Path $temp 'OmniGhost.exe'
    $auth = Get-AuthenticodeSignature -LiteralPath $exe
    if ($auth.Status -ne [System.Management.Automation.SignatureStatus]::Valid) { throw "Packaged OmniGhost.exe Authenticode invalid: $($auth.StatusMessage)" }
    if (-not $auth.SignerCertificate) { throw 'Packaged OmniGhost.exe does not expose a signer certificate.' }
    if (($auth.SignerCertificate.Thumbprint -replace '\s','').ToUpperInvariant() -ne $thumb) { throw 'Packaged OmniGhost.exe signer thumbprint does not match the trusted publisher.' }
    if ([string]$auth.SignerCertificate.Subject -ne $subject) { throw 'Packaged OmniGhost.exe signer subject does not match expectedPublisherSubject.' }
    if (-not $auth.TimeStamperCertificate) { throw 'Packaged OmniGhost.exe is missing an RFC3161 timestamp.' }
    $signTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
    if (-not (Test-Path -LiteralPath $signTool -PathType Leaf)) { throw "Required SignTool is missing: $signTool" }
    & $signTool verify /pa /tw /v $exe
    if ($LASTEXITCODE -ne 0) { throw "Packaged OmniGhost.exe failed SignTool verification ($LASTEXITCODE)." }

    # Ensure this executable was actually compiled after release_trust.h was configured.
    # A stale executable can still be signed after the fact, but would not enforce
    # publisher/manifest verification at runtime. The public thumbprint is harmless
    # trust metadata and must be embedded in the compiled client.
    $exeBytes = [IO.File]::ReadAllBytes($exe)
    $thumbBytes = [Text.Encoding]::ASCII.GetBytes($thumb)
    $containsThumb = $false
    for ($i = 0; $i -le $exeBytes.Length - $thumbBytes.Length; $i++) {
        $match = $true
        for ($j = 0; $j -lt $thumbBytes.Length; $j++) {
            if ($exeBytes[$i + $j] -ne $thumbBytes[$j]) { $match = $false; break }
        }
        if ($match) { $containsThumb = $true; break }
    }
    if (-not $containsThumb) { throw 'Packaged OmniGhost.exe was not rebuilt with the configured release trust anchor.' }

    $installManifest = Join-Path $temp 'install-manifest.sha256'
    $installSig = Join-Path $temp 'install-manifest.sha256.sig'
    if (-not (Test-Path -LiteralPath $installManifest -PathType Leaf) -or -not (Test-Path -LiteralPath $installSig -PathType Leaf)) {
        throw 'Commercial runtime is missing the signed installation integrity manifest.'
    }
    $sidecarManifest = Join-Path $ReleaseDirectory 'install-manifest.sha256'
    $sidecarSig = Join-Path $ReleaseDirectory 'install-manifest.sha256.sig'
    if (-not (Test-Path -LiteralPath $sidecarManifest -PathType Leaf) -or
        -not (Test-Path -LiteralPath $sidecarSig -PathType Leaf) -or
        (Get-FileHash -LiteralPath $installManifest -Algorithm SHA256).Hash -ine
            (Get-FileHash -LiteralPath $sidecarManifest -Algorithm SHA256).Hash -or
        (Get-FileHash -LiteralPath $installSig -Algorithm SHA256).Hash -ine
            (Get-FileHash -LiteralPath $sidecarSig -Algorithm SHA256).Hash) {
        throw 'Install manifest sidecars do not match the customer ZIP.'
    }
    $installRsa = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($cert)
    if ($null -eq $installRsa) { throw 'Configured certificate is not RSA.' }
    try {
        $installBytes = [IO.File]::ReadAllBytes($installManifest)
        $installSignature = [Convert]::FromBase64String(([IO.File]::ReadAllText($installSig)).Trim())
        if (-not $installRsa.VerifyData($installBytes, $installSignature, [System.Security.Cryptography.HashAlgorithmName]::SHA256, [System.Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
            throw 'Detached install-manifest.sha256 signature is invalid.'
        }
    } finally { if ($installRsa -is [IDisposable]) { $installRsa.Dispose() } }

    $listed = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    $root = [IO.Path]::GetFullPath($temp).TrimEnd('\') + '\'
    foreach ($line in [IO.File]::ReadAllLines($installManifest)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $parts = $line.Split([char]9)
        if ($parts.Count -ne 3 -or $parts[0] -notmatch '^[0-9a-fA-F]{64}$' -or $parts[1] -notmatch '^[0-9]+$') {
            throw 'Invalid entry in signed install manifest.'
        }
        $relative = $parts[2].Replace('/', '\')
        if ([IO.Path]::IsPathRooted($relative) -or $relative.Split('\') -contains '..' -or
            $relative.Split('\') -contains '.' -or -not $listed.Add($relative)) {
            throw "Unsafe or duplicate path in signed install manifest: $relative"
        }
        $target = [IO.Path]::GetFullPath((Join-Path $temp $relative))
        if (-not $target.StartsWith($root, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $target -PathType Leaf)) {
            throw "Missing or escaping file in signed install manifest: $relative"
        }
        $item = Get-Item -LiteralPath $target
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            [uint64]$item.Length -ne [uint64]$parts[1] -or
            (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -ine $parts[0]) {
            throw "Runtime file differs from signed install manifest: $relative"
        }
    }
    if (-not $listed.Contains('OmniGhost.exe')) { throw 'Signed install manifest omits OmniGhost.exe.' }
    foreach ($file in Get-ChildItem -LiteralPath $temp -Recurse -File) {
        $relative = $file.FullName.Substring($root.Length).Replace('/', '\')
        if ($relative -in @('install-manifest.sha256','install-manifest.sha256.sig')) { continue }
        if (-not $listed.Contains($relative)) { throw "Unlisted customer runtime file: $relative" }
    }
} finally { Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue }
Write-Host '[OmniGhost Commercial] Authenticode, compiled publisher trust, update/install manifest signatures, version, size and SHA-256 validated.'
