[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ProjectDir,
    [Parameter(Mandatory)][string]$Executable,
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',
    [string]$PackageIdentityName = 'Omnighost',
    [string]$PackagePublisher = 'CN=Omnighost Development',
    [string]$PublisherDisplayName = 'Omnighost',
    [string]$PackageVersion = '',
    [string]$SigningEnabled = 'true',
    [string]$CertificateThumbprint = $env:OMNIGHOST_MSIX_CERT_THUMBPRINT,
    [string]$OutputDirectory = '',
    [string]$TimestampUrl = 'https://timestamp.digicert.com'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SigningEnabledValue = [Convert]::ToBoolean($SigningEnabled)

function Find-WindowsSdkTool([string]$Name) {
    $kitsBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    $tool = Get-ChildItem -LiteralPath $kitsBin -Filter $Name -File -Recurse -ErrorAction SilentlyContinue |
        Where-Object { $_.Directory.Name -eq 'x64' } |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending |
        Select-Object -First 1
    if ($null -eq $tool) { throw "[MSIX] Windows SDK tool not found: $Name" }
    return $tool.FullName
}

function Convert-ToMsixVersion([string]$Value) {
    if ($Value -notmatch '^(\d+)\.(\d+)\.(\d+)(?:\.(\d+))?$') {
        throw "[MSIX] Invalid package version '$Value'. Expected Major.Minor.Build[.Revision]."
    }
    $parts = @([int]$Matches[1], [int]$Matches[2], [int]$Matches[3], $(if ($Matches[4]) { [int]$Matches[4] } else { 0 }))
    if (@($parts | Where-Object { $_ -lt 0 -or $_ -gt 65535 }).Count -ne 0) {
        throw '[MSIX] Every version component must be between 0 and 65535.'
    }
    return ($parts -join '.')
}

function New-PackageAsset([string]$Source, [string]$Destination, [int]$Width, [int]$Height) {
    Add-Type -AssemblyName System.Drawing
    $sourceImage = [Drawing.Image]::FromFile($Source)
    try {
        $bitmap = New-Object Drawing.Bitmap $Width, $Height
        try {
            $graphics = [Drawing.Graphics]::FromImage($bitmap)
            try {
                $graphics.Clear([Drawing.Color]::Transparent)
                $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
                $graphics.CompositingQuality = [Drawing.Drawing2D.CompositingQuality]::HighQuality
                $scale = [Math]::Min($Width / $sourceImage.Width, $Height / $sourceImage.Height)
                $drawWidth = [int][Math]::Round($sourceImage.Width * $scale)
                $drawHeight = [int][Math]::Round($sourceImage.Height * $scale)
                $x = [int](($Width - $drawWidth) / 2)
                $y = [int](($Height - $drawHeight) / 2)
                $graphics.DrawImage($sourceImage, $x, $y, $drawWidth, $drawHeight)
            } finally { $graphics.Dispose() }
            $bitmap.Save($Destination, [Drawing.Imaging.ImageFormat]::Png)
        } finally { $bitmap.Dispose() }
    } finally { $sourceImage.Dispose() }
}

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$Executable = [IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "[MSIX] Executable not found: $Executable" }
if ($Platform -cne 'x64') { throw "[MSIX] Only x64 is supported by this package. Received: $Platform" }

if ([string]::IsNullOrWhiteSpace($PackageVersion)) {
    $PackageVersion = (Get-Content -LiteralPath (Join-Path $ProjectDir 'config\release\version.txt') -Raw).Trim()
}
$PackageVersion = Convert-ToMsixVersion $PackageVersion
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) { $OutputDirectory = Join-Path $ProjectDir 'publish\msix' }
$OutputDirectory = [IO.Path]::GetFullPath((Join-Path $OutputDirectory '.'))

$template = Join-Path $ProjectDir 'config\msix\Package.appxmanifest'
$logo = Join-Path $ProjectDir 'resources\OmniGhost.png'
if (-not (Test-Path -LiteralPath $template -PathType Leaf)) { throw "[MSIX] Manifest template not found: $template" }
if (-not (Test-Path -LiteralPath $logo -PathType Leaf)) { throw "[MSIX] Logo not found: $logo" }

$stage = Join-Path $ProjectDir ".cache\msix\$Configuration\$Platform"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
$assets = New-Item -ItemType Directory -Path (Join-Path $stage 'Assets') -Force
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
Copy-Item -LiteralPath $Executable -Destination (Join-Path $stage 'OmniGhost.exe') -Force

New-PackageAsset $logo (Join-Path $assets 'StoreLogo.png') 50 50
New-PackageAsset $logo (Join-Path $assets 'Square44x44Logo.png') 44 44
New-PackageAsset $logo (Join-Path $assets 'Square150x150Logo.png') 150 150
New-PackageAsset $logo (Join-Path $assets 'Wide310x150Logo.png') 310 150
New-PackageAsset $logo (Join-Path $assets 'Square310x310Logo.png') 310 310

$manifest = (Get-Content -LiteralPath $template -Raw).
    Replace('@@IDENTITY_NAME@@', [Security.SecurityElement]::Escape($PackageIdentityName)).
    Replace('@@PUBLISHER@@', [Security.SecurityElement]::Escape($PackagePublisher)).
    Replace('@@PUBLISHER_DISPLAY_NAME@@', [Security.SecurityElement]::Escape($PublisherDisplayName)).
    Replace('@@VERSION@@', $PackageVersion)
[IO.File]::WriteAllText((Join-Path $stage 'AppxManifest.xml'), $manifest, (New-Object Text.UTF8Encoding($false)))

$makeAppx = Find-WindowsSdkTool 'makeappx.exe'
$signTool = Find-WindowsSdkTool 'signtool.exe'
$package = Join-Path $OutputDirectory 'Omnighost.msix'
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Force }

Write-Host "[MSIX] Packing $package"
& $makeAppx pack /d $stage /p $package /o
if ($LASTEXITCODE -ne 0) { throw "[MSIX] MakeAppx failed with exit code $LASTEXITCODE." }

if ($SigningEnabledValue) {
    $thumb = ($CertificateThumbprint -replace '\s', '').ToUpperInvariant()
    if ($thumb -notmatch '^[0-9A-F]{40}$') {
        throw '[MSIX] Signing is enabled but OMNIGHOST_MSIX_CERT_THUMBPRINT (or PackageCertificateThumbprint) is missing/invalid. Run tools\Initialize-MsixDevelopmentCertificate.ps1 once.'
    }
    $store = New-Object Security.Cryptography.X509Certificates.X509Store(
        [Security.Cryptography.X509Certificates.StoreName]::My,
        [Security.Cryptography.X509Certificates.StoreLocation]::CurrentUser)
    $store.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
    try {
        $matches = $store.Certificates.Find(
            [Security.Cryptography.X509Certificates.X509FindType]::FindByThumbprint,
            $thumb,
            $false)
        $cert = if ($matches.Count -gt 0) { $matches[0] } else { $null }
    } finally { $store.Close() }
    if ($null -eq $cert -or -not $cert.HasPrivateKey) { throw "[MSIX] Signing certificate $thumb with private key was not found in CurrentUser\My." }
    if ($cert.Subject -cne $PackagePublisher) {
        throw "[MSIX] Manifest Publisher '$PackagePublisher' must exactly match certificate subject '$($cert.Subject)'."
    }
    Write-Host "[MSIX] Signing with SHA-256 certificate $thumb"
    $signArgs = @('sign', '/s', 'My', '/sha1', $thumb, '/fd', 'SHA256')
    $isSelfSignedDevelopment = $cert.Subject -ceq $cert.Issuer
    if (-not $isSelfSignedDevelopment -and -not [string]::IsNullOrWhiteSpace($TimestampUrl)) {
        $signArgs += @('/tr', $TimestampUrl, '/td', 'SHA256')
    } elseif ($isSelfSignedDevelopment) {
        Write-Warning '[MSIX] Development certificate is self-signed; no public timestamp is requested.'
    }
    $signArgs += $package
    & $signTool @signArgs
    if ($LASTEXITCODE -ne 0) { throw "[MSIX] SignTool failed with exit code $LASTEXITCODE." }
    & $signTool verify /pa /v $package
    if ($LASTEXITCODE -ne 0 -and -not $isSelfSignedDevelopment) {
        throw "[MSIX] Signature verification failed with exit code $LASTEXITCODE."
    }
    if ($LASTEXITCODE -ne 0) {
        Write-Warning '[MSIX] Package signature was created with the selected self-signed development certificate; public chain validation is intentionally unavailable.'
    }
}

Write-Host "[MSIX] Created: $package"
