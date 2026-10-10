[CmdletBinding()]
param([string]$ProjectDir = '')

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Split-Path -Parent $PSScriptRoot }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
. (Join-Path $ProjectDir 'tools\ReleaseTrustTools.ps1')

function Assert([bool]$Condition, [string]$Message) { if (-not $Condition) { throw $Message } }
function Assert-Rejected([scriptblock]$Operation, [string]$Expected) {
    $failure = ''
    try { & $Operation } catch { $failure = $_.Exception.Message }
    Assert ($failure.Contains($Expected)) "Expected rejection '$Expected'; got '$failure'."
}
foreach ($name in @('ReleaseTrustTools','Configure-ReleaseTrust','Sign-UpdateManifest','Sign-AuthenticodeFile','Test-CommercialPreflight','Validate-ReleaseArtifacts','Package-Release','package_release','publish_github_release')) {
    $tokens = $null
    $parseErrors = $null
    [void][Management.Automation.Language.Parser]::ParseFile((Join-Path $ProjectDir "tools\$name.ps1"), [ref]$tokens, [ref]$parseErrors)
    Assert (@($parseErrors).Count -eq 0) "PowerShell parse failed: $name."
}
Assert-Rejected { Get-ReleaseSigningCertificate -Thumbprint 'invalid' } '40-character'
Assert-Rejected { Get-ReleaseSigningCertificate -Thumbprint ('0' * 40) } 'is not installed'
Assert ((ConvertTo-CppStringLiteral 'CN="Publisher\Test"') -ceq '"CN=\"Publisher\\Test\""') 'C++ publisher escaping is invalid.'

$certificates = @(Get-ChildItem Cert:\CurrentUser\My,Cert:\CurrentUser\Root)
$certificate = $null
foreach ($candidate in $certificates) {
    $rsa = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($candidate)
    if ($null -eq $rsa) { continue }
    try { if ($rsa.KeySize -ge 2048) { $certificate = $candidate; break } }
    finally { $rsa.Dispose() }
}
Assert ($null -ne $certificate) 'No RSA public certificate is available to test client pinning.'
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost Trust Test ' + [Guid]::NewGuid().ToString('N'))
[void](New-Item -ItemType Directory -Path $fixture)
$headerPath = Join-Path $fixture 'release_trust.h'
$manifestPath = Join-Path $fixture 'manifest.json'
$signaturePath = Join-Path $fixture 'manifest.json.sig'
$policy = [pscustomobject]@{signingCertificateThumbprint=$certificate.Thumbprint;expectedPublisherSubject=$certificate.Subject}
$header = @(
    'inline constexpr char PublisherSubject[] = ' + (ConvertTo-CppStringLiteral $certificate.Subject) + ';'
    'inline constexpr char CertificateThumbprint[] = ' + (ConvertTo-CppStringLiteral $certificate.Thumbprint) + ';'
    'inline constexpr char PublicCertificateDerBase64[] = ' + (ConvertTo-CppStringLiteral ([Convert]::ToBase64String($certificate.RawData))) + ';'
    'inline constexpr bool Configured = true;'
) -join "`n"
try {
    [IO.File]::WriteAllText($headerPath, $header, (New-Object Text.UTF8Encoding($false)))
    Assert-ReleaseTrustMatches -Policy $policy -HeaderPath $headerPath -Certificate $certificate
    $publicCertificate = Get-ReleasePublicCertificate -Policy $policy -HeaderPath $headerPath
    try { Assert ($publicCertificate.Thumbprint -ceq $certificate.Thumbprint) 'Public pin decode selected the wrong certificate.' }
    finally { $publicCertificate.Dispose() }
    [IO.File]::WriteAllText($manifestPath, '{"regression":"reject unauthenticated bytes"}')
    [IO.File]::WriteAllText($signaturePath, [Convert]::ToBase64String((New-Object byte[] 256)))
    Assert-Rejected { Assert-ReleaseManifestSignature -InputPath $manifestPath -SignaturePath $signaturePath -Certificate $certificate } 'Invalid detached'
    $policy.signingCertificateThumbprint = '0' * 40
    Assert-Rejected { Assert-ReleaseTrustMatches -Policy $policy -HeaderPath $headerPath -Certificate $certificate } 'does not match'
    $policy.signingCertificateThumbprint = $certificate.Thumbprint
    [IO.File]::WriteAllText($headerPath, $header.Replace('Configured = true', 'Configured = false'), (New-Object Text.UTF8Encoding($false)))
    Assert-Rejected { Assert-ReleaseTrustMatches -Policy $policy -HeaderPath $headerPath -Certificate $certificate } 'trust header differs'
} finally {
    Remove-Item -LiteralPath $headerPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $manifestPath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $signaturePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $fixture -Force
}
foreach ($candidate in $certificates) {
    $eku = @($candidate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
    if ($candidate.Subject -ceq $candidate.Issuer -and $eku.Count -eq 1 -and
        @($eku[0].EnhancedKeyUsages | Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' }).Count -gt 0) {
        $pinned = Get-ReleaseSigningCertificate -Thumbprint $candidate.Thumbprint
        Assert ($pinned.Thumbprint -ceq $candidate.Thumbprint) 'Pinned self-signed Code Signing certificate was not accepted.'
        break
    }
}
$packager = [IO.File]::ReadAllText((Join-Path $ProjectDir 'tools\Package-Release.ps1'))
Assert ($packager.Contains("if (`$Mode -in @('Commercial', 'Publish')) { `$parameters.Commercial = `$true }")) 'Publish must enter the mandatory signing path.'
$directPackager = [IO.File]::ReadAllText((Join-Path $ProjectDir 'tools\package_release.ps1'))
Assert ($directPackager.Contains('$Commercial = $Commercial -or $PublishBuild')) 'Direct Publish packaging must require signing.'
$crypto = [IO.File]::ReadAllText((Join-Path $ProjectDir 'src\updater\crypto.cpp'))
Assert ($crypto.Contains('CERT_E_UNTRUSTEDROOT') -and $crypto.Contains('CERT_E_CHAINING') -and
        $crypto.Contains('CertCompareCertificateName')) 'Pinned self-signed Authenticode fallback is incomplete.'
$workflow = [IO.File]::ReadAllText((Join-Path $ProjectDir '.github\workflows\release.yml'))
foreach ($name in @('update.json.sig','install-manifest.sha256.sig')) {
    Assert ([regex]::Matches($workflow, [regex]::Escape('artifacts/release/' + $name)).Count -eq 2) "CI must upload and publish $name."
}
Write-Host '[OmniGhost Trust Tests] Parsing, certificate rejection, C++ escaping and policy/client pin mismatch: PASS'
$global:LASTEXITCODE = 0
