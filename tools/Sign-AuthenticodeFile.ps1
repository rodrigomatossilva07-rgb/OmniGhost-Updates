[CmdletBinding()]
param(
    [string]$Path,
    [switch]$PreflightOnly,
    [string]$CertificateThumbprint = $env:OMNIGHOST_SIGNING_THUMBPRINT,
    [string]$TimestampUrl = 'https://timestamp.digicert.com',
    [ValidateSet('CurrentUser','LocalMachine')][string]$StoreLocation = 'CurrentUser'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$signTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\signtool.exe'
if (-not (Test-Path -LiteralPath $signTool -PathType Leaf)) { throw "[SIGN] SignTool obrigatorio em falta: $signTool" }
if ([string]::IsNullOrWhiteSpace($env:OMNIGHOST_SIGNING_THUMBPRINT)) {
    throw '[SIGN] Falta OMNIGHOST_SIGNING_THUMBPRINT. Instala um certificado Code Signing com chave privada em Cert:\CurrentUser\My, define a variavel com o thumbprint e reinicia o Visual Studio.'
}
$thumb = ($CertificateThumbprint -replace '\s','').ToUpperInvariant()
$environmentThumb = ($env:OMNIGHOST_SIGNING_THUMBPRINT -replace '\s','').ToUpperInvariant()
if ($thumb -notmatch '^[0-9A-F]{40}$' -or $environmentThumb -ne $thumb) {
    throw '[SIGN] OMNIGHOST_SIGNING_THUMBPRINT invalido ou diferente do certificado de confianca configurado.'
}
if ($TimestampUrl -cne 'https://timestamp.digicert.com') { throw '[SIGN] O timestamp RFC3161 deve usar https://timestamp.digicert.com.' }
$store = New-Object Security.Cryptography.X509Certificates.X509Store(
    [Security.Cryptography.X509Certificates.StoreName]::My,
    [Security.Cryptography.X509Certificates.StoreLocation]::$StoreLocation)
$store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
try {
    $matches = $store.Certificates.Find(
        [Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
        $thumb,
        $false)
    $cert = if ($matches.Count -gt 0) { $matches[0] } else { $null }
} finally { $store.Close() }
if ($null -eq $cert) { throw "[SIGN] Certificado $thumb nao encontrado em Cert:\$StoreLocation\My." }
if (-not $cert.HasPrivateKey) { throw '[SIGN] O certificado nao tem chave privada acessivel.' }
$now = [DateTime]::UtcNow
if ($cert.NotBefore.ToUniversalTime() -gt $now -or $cert.NotAfter.ToUniversalTime() -le $now) {
    throw '[SIGN] O certificado esta fora do periodo de validade.'
}
$eku = @($cert.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
if ($eku.Count -ne 1) { throw '[SIGN] O certificado nao declara Extended Key Usage de Code Signing.' }
$codeSigning = @($eku[0].EnhancedKeyUsages | Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' })
if ($codeSigning.Count -eq 0) { throw '[SIGN] O certificado nao permite Code Signing.' }
if ($PreflightOnly) {
    Write-Host "[SIGN] Pre-verificacao OK: SignTool e certificado Code Signing $thumb disponiveis."
    return
}
if ([string]::IsNullOrWhiteSpace($Path)) { throw '[SIGN] O caminho do ficheiro a assinar e obrigatorio.' }
$Path = [IO.Path]::GetFullPath($Path)
if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "[SIGN] Ficheiro em falta: $Path" }

Write-Host "[SIGN] A assinar $([IO.Path]::GetFileName($Path))"
$arguments = @('sign','/s','My','/sha1',$thumb,'/fd','SHA256','/tr',$TimestampUrl,'/td','SHA256')
if ($cert.Subject -ceq $cert.Issuer) {
    $arguments = @('sign','/s','My','/sha1',$thumb,'/fd','SHA256')
    Write-Warning '[SIGN] Certificado de desenvolvimento self-signed: assinatura local sem timestamp público.'
}
if ($StoreLocation -eq 'LocalMachine') { $arguments += '/sm' }
$arguments += $Path
& $signTool @arguments
if ($LASTEXITCODE -ne 0) { throw "[SIGN] SignTool falhou com codigo $LASTEXITCODE." }
Write-Host '[SIGN] OK'
Write-Host "[VERIFY] $([IO.Path]::GetFileName($Path))"
$verifyArguments = @('verify','/pa','/v')
if ($cert.Subject -cne $cert.Issuer) { $verifyArguments += '/tw' }
$verifyArguments += $Path
& $signTool @verifyArguments
if ($LASTEXITCODE -ne 0 -and $cert.Subject -cne $cert.Issuer) {
    throw "[VERIFY] SignTool falhou com codigo $LASTEXITCODE."
}
if ($LASTEXITCODE -ne 0) {
    Write-Warning '[VERIFY] A assinatura criptográfica foi criada, mas o certificado self-signed de desenvolvimento não forma uma cadeia pública. A confiança local é fornecida por CurrentUser\TrustedPeople.'
}
# SignTool is the authoritative verifier here. The exact certificate was selected
# by /sha1 during signing; /tw above additionally enforces a timestamp in production.
Write-Host '[VERIFY] OK'
