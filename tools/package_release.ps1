[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$BuildDir,

    [string]$Repository = 'rodrigomatossilva07-rgb/OmniGhost-Updates',

    [string]$LogFile,

    [switch]$SkipChangelog,

    [string]$SemanticAnalysisPath,

    [switch]$PublishBuild,
    [switch]$Commercial
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Hashing.ps1')

# Packaging is intentionally local-only. Remote publication is owned exclusively by Publish-Release.ps1.
# This script has no code path capable of creating or modifying a remote Release.

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$BuildDir = [System.IO.Path]::GetFullPath((Join-Path $BuildDir '.'))
if (-not $LogFile) {
    $LogFile = Join-Path $ProjectDir 'artifacts\release-build.log'
}
$LogFile = [System.IO.Path]::GetFullPath($LogFile)

$VersionFile = Join-Path $ProjectDir 'version.txt'
$ManifestPath = Join-Path $ProjectDir 'artifacts\update.json'
$ArtifactOutputDir = Join-Path $ProjectDir 'artifacts\release'
$ZipName = 'OmniGhost.zip'
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
$HistoryPath = Join-Path $ProjectDir 'artifacts\changelog-history.json'
$TransactionBackupDir = Join-Path $ProjectDir 'artifacts\release-transaction-backup'
$ManifestBackupPath = Join-Path $TransactionBackupDir 'update.json.bak'
$HistoryBackupPath = Join-Path $TransactionBackupDir 'changelog-history.json.bak'
$ReleaseOutputDir = $null
$DownloadsZipPath = $null
$DownloadsZipExistedBefore = $false
$DownloadsZipBackupPath = Join-Path $TransactionBackupDir 'OmniGhost.zip.bak'
$ManifestExistedBefore = $false
$HistoryExistedBefore = $false
$StageDir = Join-Path ([System.IO.Path]::GetTempPath()) ('OmniGhost-Package-' + [guid]::NewGuid().ToString('N'))

function Write-ReleaseLog {
    param(
        [string]$Message,
        [ValidateSet('INFO', 'WARN', 'ERROR')][string]$Level = 'INFO'
    )

    $Line = "[$((Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))] [$Level] $Message"
    $Directory = Split-Path -Parent $LogFile
    if ($Directory) { New-Item -ItemType Directory -Path $Directory -Force | Out-Null }
    Add-Content -LiteralPath $LogFile -Value $Line -Encoding UTF8

    if ($Level -eq 'WARN') { Write-Warning "[OmniGhost Release] $Message" }
    elseif ($Level -eq 'ERROR') { Write-Error "[OmniGhost Release] $Message" -ErrorAction Continue }
    else { Write-Host "[OmniGhost Release] $Message" }
}

function Remove-WithRetry {
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) { return $true }
    $lastError = $null
    for ($Attempt = 1; $Attempt -le 8; $Attempt++) {
        try {
            Get-ChildItem -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue |
                ForEach-Object { $_.Attributes = 'Normal' }
            $item = Get-Item -LiteralPath $Path -Force -ErrorAction SilentlyContinue
            if ($item) { $item.Attributes = 'Normal' }
            Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $Path)) { return $true }
        }
        catch {
            $lastError = $_.Exception.Message
            Write-ReleaseLog -Level WARN -Message "Limpeza tentativa $Attempt/8 falhou para '$Path': $lastError"
            if ($Attempt -lt 8) { Start-Sleep -Milliseconds (250 * $Attempt) }
        }
    }

    if (-not (Test-Path -LiteralPath $Path)) { return $true }
    Write-ReleaseLog -Level WARN -Message "Pasta bloqueada: $Path. Último erro: $lastError"
    return $false
}

function Get-DownloadsFolder {
    $GuidName = '{374DE290-123F-4565-9164-39C4925E467B}'
    $RegistryPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders'

    try {
        $Properties = Get-ItemProperty -LiteralPath $RegistryPath -ErrorAction Stop
        $Property = $Properties.PSObject.Properties[$GuidName]
        if ($null -ne $Property -and -not [string]::IsNullOrWhiteSpace([string]$Property.Value)) {
            $Resolved = [Environment]::ExpandEnvironmentVariables([string]$Property.Value)
            if (-not [string]::IsNullOrWhiteSpace($Resolved)) {
                return [System.IO.Path]::GetFullPath($Resolved)
            }
        }
    }
    catch {
        # Fall back to the standard user-profile Downloads directory.
    }

    $Profile = [Environment]::GetFolderPath('UserProfile')
    if ([string]::IsNullOrWhiteSpace($Profile)) {
        $Profile = $env:USERPROFILE
    }
    if ([string]::IsNullOrWhiteSpace($Profile)) {
        throw 'Não foi possível localizar a pasta do perfil do utilizador.'
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Profile 'Downloads'))
}

function Get-ReleaseOutputDirectory {
    return Join-Path (Get-DownloadsFolder) 'OmniGhost-Release'
}

function Get-LowerSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return Get-OmniGhostSha256 -LiteralPath $Path
}

function Assert-CopyMatches {
    param(
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Destination -PathType Leaf)) {
        throw "Cópia em falta: $Destination"
    }
    $SourceItem = Get-Item -LiteralPath $Source
    $DestinationItem = Get-Item -LiteralPath $Destination
    if ([int64]$SourceItem.Length -ne [int64]$DestinationItem.Length) {
        throw "O tamanho da cópia difere do original: $Destination"
    }
    if ((Get-LowerSha256 -Path $Source) -ne (Get-LowerSha256 -Path $Destination)) {
        throw "O SHA-256 da cópia difere do original: $Destination"
    }
}

function Backup-ReleaseTransaction {
    New-Item -ItemType Directory -Path $TransactionBackupDir -Force | Out-Null

    $script:ManifestExistedBefore = Test-Path -LiteralPath $ManifestPath -PathType Leaf
    $script:HistoryExistedBefore = Test-Path -LiteralPath $HistoryPath -PathType Leaf

    if ($script:ManifestExistedBefore) {
        Copy-Item -LiteralPath $ManifestPath -Destination $ManifestBackupPath -Force
    }
    else {
        Remove-Item -LiteralPath $ManifestBackupPath -Force -ErrorAction SilentlyContinue
    }

    if ($script:HistoryExistedBefore) {
        Copy-Item -LiteralPath $HistoryPath -Destination $HistoryBackupPath -Force
    }
    else {
        Remove-Item -LiteralPath $HistoryBackupPath -Force -ErrorAction SilentlyContinue
    }

    $script:DownloadsZipExistedBefore = Test-Path -LiteralPath $DownloadsZipPath -PathType Leaf
    if ($script:DownloadsZipExistedBefore) {
        Copy-Item -LiteralPath $DownloadsZipPath -Destination $DownloadsZipBackupPath -Force
    }
    else {
        Remove-Item -LiteralPath $DownloadsZipBackupPath -Force -ErrorAction SilentlyContinue
    }
}

function Restore-ReleaseTransaction {
    if ($script:ManifestExistedBefore -and (Test-Path -LiteralPath $ManifestBackupPath -PathType Leaf)) {
        Copy-Item -LiteralPath $ManifestBackupPath -Destination $ManifestPath -Force
    }
    elseif (-not $script:ManifestExistedBefore) {
        Remove-Item -LiteralPath $ManifestPath -Force -ErrorAction SilentlyContinue
    }

    if ($script:HistoryExistedBefore -and (Test-Path -LiteralPath $HistoryBackupPath -PathType Leaf)) {
        Copy-Item -LiteralPath $HistoryBackupPath -Destination $HistoryPath -Force
    }
    elseif (-not $script:HistoryExistedBefore) {
        Remove-Item -LiteralPath $HistoryPath -Force -ErrorAction SilentlyContinue
    }

    if ($script:DownloadsZipExistedBefore -and
        (Test-Path -LiteralPath $DownloadsZipBackupPath -PathType Leaf)) {
        Copy-Item -LiteralPath $DownloadsZipBackupPath -Destination $DownloadsZipPath -Force
    }
    elseif (-not $script:DownloadsZipExistedBefore) {
        Remove-Item -LiteralPath $DownloadsZipPath -Force -ErrorAction SilentlyContinue
    }

    Remove-Item -LiteralPath $ArtifactOutputDir -Recurse -Force -ErrorAction SilentlyContinue
    if ($script:ReleaseOutputDir) {
        Remove-Item -LiteralPath $script:ReleaseOutputDir -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Complete-ReleaseTransaction {
    Remove-Item -LiteralPath $TransactionBackupDir -Recurse -Force -ErrorAction SilentlyContinue
}

# Start with a fresh log for each packaging attempt.
$script:DownloadsZipPath = Join-Path (Get-DownloadsFolder) $ZipName
$LogDirectory = Split-Path -Parent $LogFile
if ($LogDirectory) { New-Item -ItemType Directory -Path $LogDirectory -Force | Out-Null }
[System.IO.File]::WriteAllText($LogFile, '', $Utf8NoBom)
Write-ReleaseLog "Projeto: $ProjectDir"
Write-ReleaseLog "Build: $BuildDir"
Write-ReleaseLog ("PowerShell: {0} ({1})" -f $PSVersionTable.PSVersion.ToString(), $PSVersionTable.PSEdition)
Backup-ReleaseTransaction

try {
    if (-not (Test-Path -LiteralPath $VersionFile -PathType Leaf)) {
        throw "Ficheiro de versão não encontrado: $VersionFile"
    }

    $Version = [System.IO.File]::ReadAllText($VersionFile).Trim()
    if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
        throw "A versão deve seguir MAJOR.MINOR.PATCH. Valor: '$Version'"
    }
    $Tag = "v$Version"
    Write-ReleaseLog "Versão: $Version; tag: $Tag"

    $ExecutablePath = Join-Path $BuildDir 'OmniGhost.exe'
    if (-not (Test-Path -LiteralPath $ExecutablePath -PathType Leaf)) {
        throw "O executável final não foi encontrado: $ExecutablePath"
    }

    $VersionInfo = [System.Diagnostics.FileVersionInfo]::GetVersionInfo($ExecutablePath)
    $ExecutableVersion = [string]$VersionInfo.ProductVersion
    if ([string]::IsNullOrWhiteSpace($ExecutableVersion)) {
        $ExecutableVersion = [string]$VersionInfo.FileVersion
    }
    $ExecutableVersionMatch = [regex]::Match($ExecutableVersion, '^(?<version>\d+\.\d+\.\d+)(?:\.\d+)?(?:\s.*)?$')
    if (-not $ExecutableVersionMatch.Success -or $ExecutableVersionMatch.Groups['version'].Value -ne $Version) {
        throw "A versão interna do executável ('$ExecutableVersion') não corresponde exatamente a '$Version'."
    }
    Write-ReleaseLog "Executável validado: $ExecutablePath (versão $($ExecutableVersionMatch.Groups['version'].Value))"

    if ($PublishBuild -or $Commercial) {
        # Any remotely publishable package must originate from Publish|x64. Signed
        # commercial builds additionally require clean reproducible provenance.
        $BuildMetadataPath = Join-Path $ProjectDir '.cache\build-metadata.json'
        if (-not (Test-Path -LiteralPath $BuildMetadataPath -PathType Leaf)) {
            throw 'Metadados de build em falta. Recompila Publish|x64 antes do packaging.'
        }
        $BuildMetadata = Get-Content -LiteralPath $BuildMetadataPath -Raw | ConvertFrom-Json
        if ([string]$BuildMetadata.app_version -ne $Version) { throw 'Build metadata version does not match version.txt.' }
        if ([string]$BuildMetadata.configuration -cne 'Publish') { throw 'Publish package requires build metadata configuration=Publish.' }
        if ([string]$BuildMetadata.architecture -cne 'x64') { throw 'Publish package requires build metadata architecture=x64.' }
        if ([string]::IsNullOrWhiteSpace([string]$BuildMetadata.release_channel) -or [string]$BuildMetadata.release_channel -eq 'unknown') {
            throw 'Publish package requires a known release channel in build metadata.'
        }
        if ($Commercial) {
            if ([string]::IsNullOrWhiteSpace([string]$BuildMetadata.commit_id) -or [string]$BuildMetadata.commit_id -in @('unknown','untracked-source')) {
                throw 'Commercial package requires a concrete commit/source identity in build metadata.'
            }
            if ([string]::IsNullOrWhiteSpace([string]$BuildMetadata.source_provenance) -or [string]$BuildMetadata.source_provenance -eq 'local-untracked') {
                throw 'Commercial package requires known source provenance.'
            }
            if ($BuildMetadata.reproducible -ne $true -or $BuildMetadata.dirty -eq $true) {
                throw 'Commercial package requires a clean reproducible source build.'
            }
            if ([string]$BuildMetadata.toolchain.vc_tools_version -eq 'unknown' -or
                [string]$BuildMetadata.toolchain.compiler_version -eq 'unknown' -or
                [string]$BuildMetadata.toolchain.windows_sdk_version -eq 'unknown') {
                throw 'Commercial package requires captured MSVC/compiler/Windows SDK versions.'
            }
        }
        Write-ReleaseLog ("Build Publish validado: source={0}; commit={1}; MSVC={2}; compiler={3}; SDK={4}; arch={5}; channel={6}; reproducible={7}" -f
            $BuildMetadata.source_provenance, $BuildMetadata.commit_id, $BuildMetadata.toolchain.vc_tools_version,
            $BuildMetadata.toolchain.compiler_version, $BuildMetadata.toolchain.windows_sdk_version,
            $BuildMetadata.architecture, $BuildMetadata.release_channel, $BuildMetadata.reproducible)
    }

    $ReleaseWorkDir = Join-Path $ProjectDir '.cache\release-work'
    New-Item -ItemType Directory -Path $ReleaseWorkDir -Force | Out-Null
    $SbomPath = Join-Path $ReleaseWorkDir 'SBOM.cdx.json'
    Write-ReleaseLog 'A gerar o inventário CycloneDX dos binários desta build...'
    & (Join-Path $ProjectDir 'tools\Generate-Sbom.ps1') `
        -ProjectDir $ProjectDir `
        -RuntimeDir $BuildDir `
        -OutputPath $SbomPath
    & (Join-Path $ProjectDir 'tools\Audit-ReleaseDependencies.ps1') `
        -ProjectDir $ProjectDir `
        -RuntimeDir $BuildDir `
        -SbomPath $SbomPath
    Write-ReleaseLog 'SBOM e política de dependências validados.'

    $script:ReleaseOutputDir = Get-ReleaseOutputDirectory
    Write-ReleaseLog "Pasta de Transferências selecionada: $ReleaseOutputDir"

    if (-not (Remove-WithRetry -Path $ArtifactOutputDir)) {
        throw "Não foi possível limpar a pasta de artefactos: $ArtifactOutputDir"
    }
    $null = Remove-WithRetry -Path $ReleaseOutputDir
    if (Test-Path -LiteralPath $ReleaseOutputDir) {
        # Folder still locked (Explorer open) — write to a unique sibling instead of failing
        $script:ReleaseOutputDir = Join-Path (Get-DownloadsFolder) ("OmniGhost-Release_" + (Get-Date -Format "yyyyMMdd_HHmmss"))
        Write-ReleaseLog -Level WARN -Message "OmniGhost-Release bloqueada; a usar: $ReleaseOutputDir"
    }

    New-Item -ItemType Directory -Path $ArtifactOutputDir -Force | Out-Null
    New-Item -ItemType Directory -Path $ReleaseOutputDir -Force | Out-Null
    New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

    # Customer builds are self-contained. OmniGhost.exe verifies and repairs its
    # unavoidable native runtime under the configured volume-root data directory; no DLL, data,
    # resource or plaintext offset file is distributed beside the executable.
    $RequiredRuntimeFiles = @('OmniGhost.exe')
    foreach ($Name in $RequiredRuntimeFiles) {
        $Source = Join-Path $BuildDir $Name
        if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
            throw "Ficheiro runtime obrigatório em falta: $Name"
        }
        Copy-Item -LiteralPath $Source -Destination (Join-Path $StageDir $Name) -Force
    }

    $UnexpectedRuntime = @(Get-ChildItem -LiteralPath $BuildDir -Recurse -Force | Where-Object {
        -not $_.PSIsContainer -and $_.FullName -ne (Join-Path $BuildDir 'OmniGhost.exe')
    })
    if ($UnexpectedRuntime.Count -gt 0) {
        throw "A build comercial não é single-EXE: $($UnexpectedRuntime[0].FullName)"
    }

    foreach ($plaintextOffset in @('data\fortnite_offsets.json','data\warzone_offsets.json')) {
        if (Test-Path -LiteralPath (Join-Path $StageDir $plaintextOffset) -PathType Leaf) {
            throw "Plaintext offset snapshot leaked into customer package: $plaintextOffset"
        }
    }
    Write-ReleaseLog 'Snapshots Fortnite/Warzone confirmados como embedded-only no pacote.'

    Write-ReleaseLog 'Runtime comercial single-EXE validado; dependências estão embutidas.'

    $StagedExecutable = Join-Path $StageDir 'OmniGhost.exe'
    if (-not (Test-Path -LiteralPath $StagedExecutable -PathType Leaf)) {
        throw 'O OmniGhost.exe não ficou na raiz do pacote.'
    }

    $PublishConfigPath = Join-Path $ProjectDir 'release-publish.json'
    $PublishSettings = $null
    if (Test-Path -LiteralPath $PublishConfigPath -PathType Leaf) {
        $PublishSettings = Get-Content -LiteralPath $PublishConfigPath -Raw | ConvertFrom-Json
    }

    if ($Commercial) {
        if ($null -eq $PublishSettings) { throw 'release-publish.json é obrigatório para packaging Commercial.' }
        if ($PublishSettings.requireAuthenticode -ne $true -or $PublishSettings.requireManifestSignature -ne $true) {
            throw 'Packaging Commercial exige requireAuthenticode=true e requireManifestSignature=true.'
        }
        $ConfiguredChannel = if ($PublishSettings.releaseChannel) { [string]$PublishSettings.releaseChannel } else { 'stable' }
        if ([string]$BuildMetadata.release_channel -cne $ConfiguredChannel) {
            throw "Release channel do build ('$($BuildMetadata.release_channel)') não corresponde ao policy ('$ConfiguredChannel'). Recompila Publish|x64 com o canal correto."
        }
        $SigningThumbprint = ([string]$PublishSettings.signingCertificateThumbprint -replace '\s','').ToUpperInvariant()
        $ExpectedPublisher = [string]$PublishSettings.expectedPublisherSubject
        if ($SigningThumbprint -notmatch '^[0-9A-F]{40}$' -or [string]::IsNullOrWhiteSpace($ExpectedPublisher)) {
            throw 'Trust comercial não configurado. Executa tools\Configure-ReleaseTrust.ps1 no ambiente de assinatura.'
        }
        $TimestampUrl = if ($PublishSettings.timestampUrl) { [string]$PublishSettings.timestampUrl } else { 'http://timestamp.digicert.com' }
        Write-ReleaseLog 'A assinar a cópia staged de OmniGhost.exe com Authenticode...'
        & (Join-Path $ProjectDir 'tools\Sign-AuthenticodeFile.ps1') -Path $StagedExecutable -CertificateThumbprint $SigningThumbprint -TimestampUrl $TimestampUrl
        if ($LASTEXITCODE -ne 0) { throw 'Authenticode signing failed.' }
    }

    # Installation manifest: all customer runtime files are independently hashed.
    # It is line-oriented (SHA256<TAB>SIZE<TAB>RELATIVE_PATH) so the client can
    # verify it without another JSON parser. The manifest itself is excluded.
    $InstallManifestPath = Join-Path $ReleaseWorkDir 'install-manifest.sha256'
    $InstallManifestLines = New-Object System.Collections.Generic.List[string]
    Get-ChildItem -LiteralPath $StageDir -Recurse -Force -File |
        Where-Object { $_.FullName -ne $InstallManifestPath } |
        Sort-Object FullName |
        ForEach-Object {
            $relative = $_.FullName.Substring($StageDir.Length).TrimStart('\','/').Replace('\','/')
            if ($relative.Contains("`t") -or $relative.Contains("`r") -or $relative.Contains("`n")) {
                throw "Caminho runtime não suportado no install manifest: $relative"
            }
            $hash = Get-LowerSha256 -Path $_.FullName
            $InstallManifestLines.Add("$hash`t$($_.Length)`t$relative")
        }
    [IO.File]::WriteAllLines($InstallManifestPath, $InstallManifestLines, $Utf8NoBom)
    Write-ReleaseLog ("Install manifest criado: {0} ficheiros." -f $InstallManifestLines.Count)

    $InstallManifestSignaturePath = Join-Path $ReleaseWorkDir 'install-manifest.sha256.sig'
    if ($Commercial) {
        Write-ReleaseLog 'A assinar install-manifest.sha256 para permitir Verify installation autenticado...'
        & (Join-Path $ProjectDir 'tools\Sign-UpdateManifest.ps1') -InputPath $InstallManifestPath -OutputPath $InstallManifestSignaturePath -CertificateThumbprint $SigningThumbprint
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $InstallManifestSignaturePath -PathType Leaf)) {
            throw 'Detached install manifest signing failed.'
        }
    } else {
        Remove-Item -LiteralPath $InstallManifestSignaturePath -Force -ErrorAction SilentlyContinue
    }

    $ForbiddenFiles = Get-ChildItem -LiteralPath $StageDir -Recurse -Force -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.pdb', '.obj', '.tlog', '.idb', '.ilk', '.exp') }
    $ForbiddenDirectories = Get-ChildItem -LiteralPath $StageDir -Recurse -Force -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -ieq 'intermediate' }
    if ($ForbiddenFiles -or $ForbiddenDirectories) {
        throw 'O pacote temporário contém ficheiros de compilação proibidos.'
    }

    $ArtifactZip = Join-Path $ArtifactOutputDir $ZipName
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::CreateFromDirectory(
        $StageDir,
        $ArtifactZip,
        [System.IO.Compression.CompressionLevel]::Optimal,
        $false)

    $ZipItem = Get-Item -LiteralPath $ArtifactZip
    $Size = [int64]$ZipItem.Length
    if ($Size -le 0) { throw 'OmniGhost.zip foi criado com tamanho zero.' }
    $Sha256 = Get-LowerSha256 -Path $ArtifactZip
    Write-ReleaseLog "ZIP criado: $Size bytes; SHA-256: $Sha256"

    # Publish the signed inventory beside the ZIP. The client ZIP intentionally
    # contains only OmniGhost.exe; package SHA-256 + Authenticode authenticate it.
    $ArtifactInstallManifest = Join-Path $ArtifactOutputDir 'install-manifest.sha256'
    $ArtifactInstallManifestSignature = Join-Path $ArtifactOutputDir 'install-manifest.sha256.sig'
    $ArtifactSbom = Join-Path $ArtifactOutputDir 'SBOM.cdx.json'
    Copy-Item -LiteralPath $InstallManifestPath -Destination $ArtifactInstallManifest -Force
    Copy-Item -LiteralPath $SbomPath -Destination $ArtifactSbom -Force
    if (Test-Path -LiteralPath $InstallManifestSignaturePath -PathType Leaf) {
        Copy-Item -LiteralPath $InstallManifestSignaturePath -Destination $ArtifactInstallManifestSignature -Force
    }

    # Release policy comes from release-publish.json, never from a stale update.json.
    # This prevents an old mandatory flag/channel from silently leaking into a new package.
    $MinimumSupportedVersion = if ($null -ne $PublishSettings -and $PublishSettings.minimumSupportedVersion) { [string]$PublishSettings.minimumSupportedVersion } else { '1.0.0' }
    $Mandatory = if ($null -ne $PublishSettings -and $null -ne $PublishSettings.defaultMandatory) { [bool]$PublishSettings.defaultMandatory } else { $false }
    $MandatoryReason = if ($null -ne $PublishSettings -and $PublishSettings.mandatoryReason) { [string]$PublishSettings.mandatoryReason } else { '' }
    $Channel = if ($null -ne $PublishSettings -and $PublishSettings.releaseChannel) { [string]$PublishSettings.releaseChannel } else { 'stable' }
    $AppId = if ($null -ne $PublishSettings -and $PublishSettings.appId) { [string]$PublishSettings.appId } else { 'com.omnighost.launcher' }
    if ($Channel -notin @('stable','beta','development')) { throw "releaseChannel inválido em release-publish.json: '$Channel'." }
    if ([string]::IsNullOrWhiteSpace($AppId)) { throw 'appId não pode estar vazio em release-publish.json.' }
    if ($MinimumSupportedVersion -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') {
        throw "minimumSupportedVersion inválida em release-publish.json: '$MinimumSupportedVersion'."
    }
    if ($Mandatory -and [string]::IsNullOrWhiteSpace($MandatoryReason)) {
        $MandatoryReason = 'Esta versão é necessária para manter compatibilidade e suporte.'
    }
    if ($MandatoryReason.Length -gt 512) { throw 'mandatoryReason excede 512 caracteres.' }

    $BaseUrl = "https://github.com/$Repository/releases"
    $Manifest = [ordered]@{
        schemaVersion = 1
        appId = $AppId
        channel = $Channel
        version = $Version
        minimumSupportedVersion = $MinimumSupportedVersion
        mandatory = $Mandatory
        mandatoryReason = $MandatoryReason
        publishedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        releaseNotesUrl = "$BaseUrl/tag/$Tag"
        packages = @(
            [ordered]@{
                platform = 'windows'
                architecture = 'x64'
                fileName = $ZipName
                url = "$BaseUrl/download/$Tag/$ZipName"
                size = $Size
                sha256 = $Sha256
            }
        )
    }

    $ManifestJson = ($Manifest | ConvertTo-Json -Depth 8) + [Environment]::NewLine
    $null = $ManifestJson | ConvertFrom-Json
    New-Item -ItemType Directory -Path (Split-Path -Parent $ManifestPath) -Force | Out-Null
    [System.IO.File]::WriteAllText($ManifestPath, $ManifestJson, $Utf8NoBom)
    $ArtifactManifest = Join-Path $ArtifactOutputDir 'update.json'
    [System.IO.File]::WriteAllText($ArtifactManifest, $ManifestJson, $Utf8NoBom)

    $ArtifactManifestSignature = Join-Path $ArtifactOutputDir 'update.json.sig'
    if ($Commercial) {
        Write-ReleaseLog 'A assinar update.json com a chave privada do certificado de Release...'
        & (Join-Path $ProjectDir 'tools\Sign-UpdateManifest.ps1') -InputPath $ArtifactManifest -OutputPath $ArtifactManifestSignature -CertificateThumbprint $SigningThumbprint
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $ArtifactManifestSignature -PathType Leaf)) {
            throw 'Detached manifest signing failed.'
        }
    } else {
        Remove-Item -LiteralPath $ArtifactManifestSignature -Force -ErrorAction SilentlyContinue
        if ($PublishBuild) {
            Write-ReleaseLog -Message 'Publish sem certificado: update.json.sig nao foi criado; HTTPS, tamanho e SHA-256 continuam obrigatorios.'
        } else {
            Write-ReleaseLog -Level WARN -Message 'Development package: update.json.sig nao foi criado.'
        }
    }

    $ManifestCheck = Get-Content -LiteralPath $ArtifactManifest -Raw | ConvertFrom-Json
    if ([string]$ManifestCheck.version -ne $Version -or
        [string]$ManifestCheck.packages[0].fileName -ne $ZipName -or
        [int64]$ManifestCheck.packages[0].size -ne $Size -or
        [string]$ManifestCheck.packages[0].sha256 -ne $Sha256 -or
        [string]$ManifestCheck.packages[0].url -notlike "*/$Tag/$ZipName") {
        throw 'A validação final de update.json falhou.'
    }
    Write-ReleaseLog 'update.json gerado e validado.'

    if (-not $SkipChangelog) {
        $Generator = Join-Path $ProjectDir 'tools\Generate-Changelog.ps1'
        if (-not (Test-Path -LiteralPath $Generator -PathType Leaf)) {
            throw "Gerador de changelog em falta: $Generator"
        }
        Write-ReleaseLog 'A gerar changelog.json e release-notes.md...'
        try {
            $generatorArguments = @{
                ProjectDir = $ProjectDir
                Version = $Version
                OutputDir = $ArtifactOutputDir
            }
            if (-not [string]::IsNullOrWhiteSpace($SemanticAnalysisPath) -and
                (Test-Path -LiteralPath $SemanticAnalysisPath -PathType Leaf)) {
                $generatorArguments.SemanticAnalysisPath = [IO.Path]::GetFullPath($SemanticAnalysisPath)
            }
            $null = & $Generator @generatorArguments
        }
        catch {
            throw "A geração do changelog falhou: $($_.Exception.Message)"
        }
    }

    $ChangelogSource = Join-Path $ArtifactOutputDir 'changelog.json'
    $NotesSource = Join-Path $ArtifactOutputDir 'release-notes.md'
    foreach ($RequiredFile in @($ChangelogSource, $NotesSource)) {
        if (-not (Test-Path -LiteralPath $RequiredFile -PathType Leaf)) {
            throw "Artefacto obrigatório em falta: $RequiredFile"
        }
        if ((Get-Item -LiteralPath $RequiredFile).Length -le 0) {
            throw "Artefacto obrigatório vazio: $RequiredFile"
        }
    }

    $ChangelogCheck = Get-Content -LiteralPath $ChangelogSource -Raw | ConvertFrom-Json
    if (-not @($ChangelogCheck.releases | Where-Object { [string]$_.version -eq $Version -and [string]$_.tag -eq $Tag })) {
        throw "changelog.json não contém a versão/tag $Version/$Tag."
    }
    Write-ReleaseLog 'Changelog e notas validados.'

    $ReleaseZip = Join-Path $ReleaseOutputDir $ZipName
    $ReleaseManifest = Join-Path $ReleaseOutputDir 'update.json'
    $ReleaseManifestSignature = Join-Path $ReleaseOutputDir 'update.json.sig'
    $ReleaseInstallManifest = Join-Path $ReleaseOutputDir 'install-manifest.sha256'
    $ReleaseInstallManifestSignature = Join-Path $ReleaseOutputDir 'install-manifest.sha256.sig'
    $ReleaseChangelog = Join-Path $ReleaseOutputDir 'changelog.json'
    $ReleaseNotes = Join-Path $ReleaseOutputDir 'release-notes.md'
    $ReleaseSbom = Join-Path $ReleaseOutputDir 'SBOM.cdx.json'

    Copy-Item -LiteralPath $ArtifactZip -Destination $ReleaseZip -Force
    Copy-Item -LiteralPath $ArtifactManifest -Destination $ReleaseManifest -Force
    if (Test-Path -LiteralPath $ArtifactManifestSignature -PathType Leaf) {
        Copy-Item -LiteralPath $ArtifactManifestSignature -Destination $ReleaseManifestSignature -Force
    }
    Copy-Item -LiteralPath $ArtifactInstallManifest -Destination $ReleaseInstallManifest -Force
    if (Test-Path -LiteralPath $ArtifactInstallManifestSignature -PathType Leaf) {
        Copy-Item -LiteralPath $ArtifactInstallManifestSignature -Destination $ReleaseInstallManifestSignature -Force
    }
    Copy-Item -LiteralPath $ChangelogSource -Destination $ReleaseChangelog -Force
    Copy-Item -LiteralPath $NotesSource -Destination $ReleaseNotes -Force
    Copy-Item -LiteralPath $ArtifactSbom -Destination $ReleaseSbom -Force

    Assert-CopyMatches -Source $ArtifactZip -Destination $ReleaseZip
    Assert-CopyMatches -Source $ArtifactManifest -Destination $ReleaseManifest
    if (Test-Path -LiteralPath $ArtifactManifestSignature -PathType Leaf) {
        Assert-CopyMatches -Source $ArtifactManifestSignature -Destination $ReleaseManifestSignature
    }
    Assert-CopyMatches -Source $ArtifactInstallManifest -Destination $ReleaseInstallManifest
    if (Test-Path -LiteralPath $ArtifactInstallManifestSignature -PathType Leaf) {
        Assert-CopyMatches -Source $ArtifactInstallManifestSignature -Destination $ReleaseInstallManifestSignature
    }
    Assert-CopyMatches -Source $ChangelogSource -Destination $ReleaseChangelog
    Assert-CopyMatches -Source $NotesSource -Destination $ReleaseNotes
    Assert-CopyMatches -Source $ArtifactSbom -Destination $ReleaseSbom
    Write-ReleaseLog "Artefactos copiados e validados em: $ReleaseOutputDir"

    if ($Commercial) {
        Write-ReleaseLog 'A executar validação comercial final do pacote...'
        & (Join-Path $ProjectDir 'tools\Validate-CommercialRelease.ps1') -ProjectDir $ProjectDir -ReleaseDirectory $ReleaseOutputDir -Version $Version
        if ($LASTEXITCODE -ne 0) { throw 'Commercial release validation failed.' }
        Write-ReleaseLog 'Pacote comercial validado: Authenticode + publisher + manifest signature + SHA-256.'
    }

    Copy-Item -LiteralPath $ArtifactZip -Destination $DownloadsZipPath -Force
    Assert-CopyMatches -Source $ArtifactZip -Destination $DownloadsZipPath
    Write-ReleaseLog "ZIP final copiado para as Transferências: $DownloadsZipPath"

    # Version is read-only during Package. Version changes are an explicit developer action.

    if ($PublishBuild) {
        Write-ReleaseLog 'Fase Package concluida. O orquestrador Publish pode agora executar a operacao remota.'
    } else {
        Write-ReleaseLog 'Fase Package concluida localmente. Nenhuma operacao remota foi executada.'
    }
    Complete-ReleaseTransaction

    Remove-Item -LiteralPath (Join-Path $BuildDir 'OmniGhost.pdb') -Force -ErrorAction SilentlyContinue

    Write-ReleaseLog 'Artefactos locais preservados. Packaging nunca apaga o canonical release/CI output.'

    Write-ReleaseLog 'Pacote de Release criado com sucesso.'
    Write-ReleaseLog "Versão: $Version"
    Write-ReleaseLog "Tag: $Tag"
    Write-ReleaseLog "Tamanho: $Size bytes"
    Write-ReleaseLog "SHA-256: $Sha256"
    if (Test-Path -LiteralPath $DownloadsZipPath) {
        Write-ReleaseLog "ZIP nas Transferências: $DownloadsZipPath"
    }
    else {
        Write-ReleaseLog 'O ZIP temporário das Transferências foi removido depois da validação do upload.'
    }
    if (Test-Path -LiteralPath $ReleaseOutputDir) {
        Write-ReleaseLog "Pasta local: $ReleaseOutputDir"
    }
    else {
        Write-ReleaseLog 'A pasta local foi removida depois da validação do upload.'
    }
}
catch {
    $Exception = $_.Exception
    $Details = "Falha: $($Exception.Message)"
    if ($_.InvocationInfo -and $_.InvocationInfo.PositionMessage) {
        $Details += [Environment]::NewLine + $_.InvocationInfo.PositionMessage
    }
    if ($_.ScriptStackTrace) {
        $Details += [Environment]::NewLine + 'Stack:' + [Environment]::NewLine + $_.ScriptStackTrace
    }
    Write-ReleaseLog -Level ERROR -Message $Details
    try {
        Restore-ReleaseTransaction
    }
    catch {
        Write-ReleaseLog -Level WARN -Message "Não foi possível restaurar todos os artefactos anteriores: $($_.Exception.Message)"
    }
    # Packaging never mutates version/source, therefore it must never roll source files back.
    throw
}
finally {
    Remove-Item -LiteralPath $StageDir -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $TransactionBackupDir -Recurse -Force -ErrorAction SilentlyContinue
    # A fase Package preserva sempre os artefactos locais para validação/publicação posterior.
}

return
