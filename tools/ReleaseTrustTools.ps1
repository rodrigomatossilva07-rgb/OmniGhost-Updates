Set-StrictMode -Version Latest

function Get-ReleaseSigningCertificate {
    param([Parameter(Mandatory = $true)][string]$Thumbprint, [switch]$RequirePrivateKey)
    $normalized = ($Thumbprint -replace '\s', '').ToUpperInvariant()
    if ($normalized -notmatch '^[0-9A-F]{40}$') { throw 'A 40-character signing certificate thumbprint is required.' }
    $certificate = Get-Item -LiteralPath "Cert:\CurrentUser\My\$normalized" -ErrorAction SilentlyContinue
    if ($null -eq $certificate) { throw "Signing certificate $normalized is not installed in CurrentUser\My." }
    if ($RequirePrivateKey -and -not $certificate.HasPrivateKey) { throw 'The signing certificate has no accessible private key.' }
    $now = [DateTime]::UtcNow
    if ($certificate.NotBefore.ToUniversalTime() -gt $now -or $certificate.NotAfter.ToUniversalTime() -le $now) {
        throw 'The signing certificate is outside its validity period.'
    }
    $eku = @($certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.37' })
    if ($eku.Count -ne 1 -or @($eku[0].EnhancedKeyUsages | Where-Object { $_.Value -eq '1.3.6.1.5.5.7.3.3' }).Count -eq 0) {
        throw 'The signing certificate lacks the Code Signing EKU.'
    }
    $constraints = @($certificate.Extensions | Where-Object { $_.Oid.Value -eq '2.5.29.19' })
    if ($constraints.Count -gt 0 -and $constraints[0].CertificateAuthority) { throw 'A certificate authority key cannot be used as the release publisher.' }
    if ($certificate.Subject -ceq $certificate.Issuer) {
        throw 'A self-signed development/test certificate cannot configure commercial release trust. Install a CA-issued Code Signing certificate.'
    }
    $publicKey = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($certificate)
    if ($null -eq $publicKey) { throw 'Release manifest signatures require an RSA certificate.' }
    try { if ($publicKey.KeySize -lt 2048) { throw 'The RSA signing key must contain at least 2048 bits.' } }
    finally { $publicKey.Dispose() }
    $chain = New-Object Security.Cryptography.X509Certificates.X509Chain
    try {
        $chain.ChainPolicy.ApplicationPolicy.Add((New-Object Security.Cryptography.Oid '1.3.6.1.5.5.7.3.3'))
        $chain.ChainPolicy.RevocationMode = [Security.Cryptography.X509Certificates.X509RevocationMode]::Online
        $chain.ChainPolicy.RevocationFlag = [Security.Cryptography.X509Certificates.X509RevocationFlag]::ExcludeRoot
        $chain.ChainPolicy.UrlRetrievalTimeout = [TimeSpan]::FromSeconds(10)
        if (-not $chain.Build($certificate)) {
            $status = ($chain.ChainStatus | ForEach-Object { $_.Status.ToString() }) -join ', '
            throw "The Code Signing certificate chain is not trusted or revocation could not be checked: $status."
        }
    } finally { $chain.Dispose() }
    return $certificate
}

function Get-ReleaseSignTool {
    $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $tools = @(Get-ChildItem -LiteralPath $sdkRoot -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Leaf })
    if ($tools.Count -eq 0) { throw 'The Windows SDK x64 SignTool is not installed.' }
    return $tools[0]
}

function ConvertTo-CppStringLiteral {
    param([AllowEmptyString()][string]$Value)
    return '"' + $Value.Replace('\', '\\').Replace('"', '\"').Replace("`r", '\r').Replace("`n", '\n') + '"'
}

function Assert-ReleaseTrustMatches {
    param(
        [Parameter(Mandatory = $true)]$Policy,
        [Parameter(Mandatory = $true)][string]$HeaderPath,
        [Parameter(Mandatory = $true)][Security.Cryptography.X509Certificates.X509Certificate2]$Certificate
    )
    $thumbprint = ([string]$Policy.signingCertificateThumbprint -replace '\s', '').ToUpperInvariant()
    if ($thumbprint -cne $Certificate.Thumbprint -or [string]$Policy.expectedPublisherSubject -cne $Certificate.Subject) {
        throw 'Release policy publisher/thumbprint does not match the selected signing certificate. Run Configure-ReleaseTrust.ps1 and rebuild.'
    }
    $header = [IO.File]::ReadAllText($HeaderPath, [Text.Encoding]::UTF8)
    foreach ($declaration in @(
        ('inline constexpr char PublisherSubject[] = ' + (ConvertTo-CppStringLiteral $Certificate.Subject) + ';'),
        ('inline constexpr char CertificateThumbprint[] = ' + (ConvertTo-CppStringLiteral $thumbprint) + ';'),
        ('inline constexpr char PublicCertificateDerBase64[] = ' + (ConvertTo-CppStringLiteral ([Convert]::ToBase64String($Certificate.RawData))) + ';'),
        'inline constexpr bool Configured = true;'
    )) {
        if (-not $header.Contains($declaration)) {
            throw 'The compiled client trust header differs from release policy/certificate. Run Configure-ReleaseTrust.ps1, commit both files and rebuild.'
        }
    }
}

function Get-ReleasePublicCertificate {
    param([Parameter(Mandatory = $true)]$Policy, [Parameter(Mandatory = $true)][string]$HeaderPath)
    $header = [IO.File]::ReadAllText($HeaderPath, [Text.Encoding]::UTF8)
    $match = [regex]::Match($header, 'PublicCertificateDerBase64\[\]\s*=\s*"([A-Za-z0-9+/=]+)";')
    if (-not $match.Success) { throw 'The client public signing certificate is not configured. Run Configure-ReleaseTrust.ps1 and rebuild.' }
    $bytes = [Convert]::FromBase64String($match.Groups[1].Value)
    $certificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2 -ArgumentList @(,$bytes)
    try { Assert-ReleaseTrustMatches -Policy $Policy -HeaderPath $HeaderPath -Certificate $certificate }
    catch { $certificate.Dispose(); throw }
    return $certificate
}

function Assert-ReleaseManifestSignature {
    param(
        [Parameter(Mandatory = $true)][string]$InputPath,
        [Parameter(Mandatory = $true)][string]$SignaturePath,
        [Parameter(Mandatory = $true)][Security.Cryptography.X509Certificates.X509Certificate2]$Certificate
    )
    $publicKey = [Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPublicKey($Certificate)
    if ($null -eq $publicKey) { throw 'The pinned certificate must provide an RSA key of at least 2048 bits.' }
    try {
        if ($publicKey.KeySize -lt 2048) { throw 'The pinned certificate must provide an RSA key of at least 2048 bits.' }
        $bytes = [IO.File]::ReadAllBytes($InputPath)
        $signature = [Convert]::FromBase64String([IO.File]::ReadAllText($SignaturePath, [Text.Encoding]::UTF8).Trim())
        if (-not $publicKey.VerifyData($bytes, $signature, [Security.Cryptography.HashAlgorithmName]::SHA256, [Security.Cryptography.RSASignaturePadding]::Pkcs1)) {
            throw "Invalid detached RSA/SHA-256 signature: $InputPath."
        }
    } finally { $publicKey.Dispose() }
}
