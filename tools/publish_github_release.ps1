[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [Parameter(Mandatory = $true)]
    [string]$ReleaseDirectory,

    [Parameter(Mandatory = $false)]
    [switch]$InternalConfirmed,

    [Parameter(Mandatory = $false)]
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'Hashing.ps1')

if (-not $InternalConfirmed) { throw 'Direct publication is blocked. Use tools\Publish-Release.ps1 with the explicit -Confirm phrase.' }

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$ReleaseDirectory = [System.IO.Path]::GetFullPath((Join-Path $ReleaseDirectory '.'))
$ConfigPath = Join-Path $ProjectDir 'release-publish.json'
$Tag = "v$Version"

function Write-Gh {
    param([string]$Message)
    Write-Host "[OmniGhost GitHub] $Message"
}

function Find-Gh {
    $Command = Get-Command gh.exe -ErrorAction SilentlyContinue
    if (-not $Command) {
        $Command = Get-Command gh -ErrorAction SilentlyContinue
    }
    if ($Command) { return $Command.Source }

    $Candidates = @()
    if (-not [string]::IsNullOrWhiteSpace($env:ProgramFiles)) {
        $Candidates += Join-Path $env:ProgramFiles 'GitHub CLI\gh.exe'
    }
    $ProgramFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($ProgramFilesX86)) {
        $Candidates += Join-Path $ProgramFilesX86 'GitHub CLI\gh.exe'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $Candidates += Join-Path $env:LOCALAPPDATA 'Programs\GitHub CLI\gh.exe'
        $Candidates += Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links\gh.exe'
    }
    if (-not [string]::IsNullOrWhiteSpace($env:ChocolateyInstall)) {
        $Candidates += Join-Path $env:ChocolateyInstall 'bin\gh.exe'
    }
    if (-not [string]::IsNullOrWhiteSpace($HOME)) {
        $Candidates += Join-Path $HOME 'scoop\apps\gh\current\bin\gh.exe'
    }

    foreach ($Candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace([string]$Candidate) -and
            (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($Candidate)
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) {
        $WinGetPackages = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
        if (Test-Path -LiteralPath $WinGetPackages -PathType Container) {
            $Package = Get-ChildItem -LiteralPath $WinGetPackages -Directory -Filter 'GitHub.cli_*' -ErrorAction SilentlyContinue |
                Sort-Object LastWriteTimeUtc -Descending |
                Select-Object -First 1
            if ($Package) {
                $Found = Get-ChildItem -LiteralPath $Package.FullName -Recurse -File -Filter 'gh.exe' -ErrorAction SilentlyContinue |
                    Select-Object -First 1
                if ($Found) { return $Found.FullName }
            }
        }
    }

    return $null
}

function Get-RequiredAssets {
    param([bool]$RequireSignatures)
    $assets = @(
        'OmniGhost.zip',
        'update.json',
        'install-manifest.sha256',
        'SBOM.cdx.json',
        'changelog.json',
        'release-notes.md'
    )
    if ($RequireSignatures) {
        $assets += 'update.json.sig'
        $assets += 'install-manifest.sha256.sig'
    }
    return $assets
}

function Get-LocalAssetInfo {
    param([Parameter(Mandatory = $true)][string[]]$Names)
    $Result = @{}
    foreach ($Name in $Names) {
        $Path = Join-Path $ReleaseDirectory $Name
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
            throw "Asset em falta para publicação: $Path"
        }
        $Item = Get-Item -LiteralPath $Path
        if ($Item.Length -le 0) { throw "Asset com tamanho zero: $Path" }
        $Result[$Name] = [pscustomobject]@{
            Path = $Path
            Size = [int64]$Item.Length
            Sha256 = Get-OmniGhostSha256 -LiteralPath $Path
        }
    }
    return $Result
}

function Invoke-GhCapture {
    param(
        [Parameter(Mandatory = $true)][string]$Gh,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $PreviousErrorActionPreference = $ErrorActionPreference
    $Captured = @()
    $ExitCode = 1

    try {
        # gh writes expected lookup failures (for example "release not found")
        # to stderr. With the script-wide ErrorActionPreference = Stop, Windows
        # PowerShell can turn that expected stderr into a terminating error.
        # Capture it with Continue and decide from the real process exit code.
        $ErrorActionPreference = 'Continue'
        $Captured = @(& $Gh @Arguments 2>&1)
        $ExitCode = [int]$LASTEXITCODE
    }
    catch {
        $Captured += $_.Exception.Message
        if (Test-Path variable:LASTEXITCODE) {
            $ExitCode = [int]$LASTEXITCODE
        }
        if ($ExitCode -eq 0) { $ExitCode = 1 }
    }
    finally {
        $ErrorActionPreference = $PreviousErrorActionPreference
    }

    $Text = ($Captured | ForEach-Object { [string]$_ }) -join [Environment]::NewLine
    return [pscustomobject]@{
        ExitCode = $ExitCode
        Output = $Text.Trim()
    }
}

function Get-ReleaseView {
    param(
        [Parameter(Mandatory = $true)][string]$Gh,
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$ReleaseTag
    )

    $Result = Invoke-GhCapture -Gh $Gh -Arguments @(
        'release', 'view', $ReleaseTag,
        '--repo', $Repository,
        '--json', 'tagName,name,isDraft,isPrerelease,url,assets'
    )

    if ($Result.ExitCode -ne 0) {
        if ($Result.Output -match '(?i)release not found|HTTP 404|not found') {
            return $null
        }
        throw "Não foi possível verificar se a Release $ReleaseTag existe: $($Result.Output)"
    }
    if ([string]::IsNullOrWhiteSpace($Result.Output)) {
        throw "A consulta da Release $ReleaseTag terminou sem devolver dados."
    }

    return $Result.Output | ConvertFrom-Json
}


function Test-PublicUpdateManifest {
    param(
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$ExpectedVersion
    )
    $PublicUrl = "https://github.com/$Repository/releases/latest/download/update.json"
    Write-Gh "A verificar URL pública do manifesto: $PublicUrl"

    # Prefer WebClient.DownloadString — always returns a .NET string (UTF-8),
    # avoiding PS 5.1 Invoke-WebRequest returning [byte[]] / Object[] that
    # stringifies as "123 10 32 ..." under ConvertFrom-Json.
    $body = $null
    try {
        $wc = New-Object System.Net.WebClient
        $wc.Headers.Add('User-Agent', 'OmniGhost-Publish/1.0')
        $wc.Encoding = [System.Text.Encoding]::UTF8
        $body = $wc.DownloadString($PublicUrl)
        $wc.Dispose()
    } catch {
        # Fallback: Invoke-WebRequest with explicit UTF-8 decode
        try {
            $resp = Invoke-WebRequest -Uri $PublicUrl -Method Get -UseBasicParsing -TimeoutSec 45
            if ([int]$resp.StatusCode -ne 200) {
                throw ("HTTP {0}" -f $resp.StatusCode)
            }
            $raw = $resp.Content
            if ($raw -is [string]) {
                $body = $raw
            } elseif ($raw -is [byte[]]) {
                $body = [System.Text.Encoding]::UTF8.GetString($raw)
            } elseif ($raw -is [System.Collections.IEnumerable]) {
                $list = New-Object System.Collections.Generic.List[byte]
                foreach ($item in $raw) {
                    $list.Add([byte]$item)
                }
                $body = [System.Text.Encoding]::UTF8.GetString($list.ToArray())
            } else {
                $body = [string]$raw
            }
        } catch {
            throw ("Publish falhou na verificação pública do update.json.`nURL: {0}`nDetalhe: {1}" -f $PublicUrl, $_.Exception.Message)
        }
    }

    if ([string]::IsNullOrWhiteSpace($body)) {
        throw ("update.json público está vazio: {0}" -f $PublicUrl)
    }
    if ($body.Length -gt 0 -and [int][char]$body[0] -eq 0xFEFF) {
        $body = $body.Substring(1)
    }
    # Guard: never pass a "byte dump" string to ConvertFrom-Json
    if ($body -match '^\s*\d{1,3}(\s+\d{1,3}){8,}') {
        throw ("Resposta pública não parece texto JSON (possível descodificação errada). URL: {0} len={1}" -f $PublicUrl, $body.Length)
    }

    try {
        $json = $body | ConvertFrom-Json
    } catch {
        $preview = ($body -replace '[\r\n]+', ' ').Trim()
        if ($preview.Length -gt 100) { $preview = $preview.Substring(0, 100) + '...' }
        throw ("update.json público não é JSON válido ({0}). Preview: {1}" -f $_.Exception.Message, $preview)
    }

    $verStr = $null
    $props = @()
    if ($null -ne $json -and $null -ne $json.PSObject) {
        $props = @($json.PSObject.Properties | ForEach-Object { $_.Name })
    }
    if ($props -contains 'version') {
        $rawVer = $json.version
        if ($rawVer -is [string]) {
            $verStr = $rawVer
        } elseif ($null -ne $rawVer -and $null -ne $rawVer.PSObject -and
                  (@($rawVer.PSObject.Properties | ForEach-Object { $_.Name }) -contains 'original')) {
            $verStr = [string]$rawVer.original
        } else {
            $verStr = [string]$rawVer
        }
    }
    if ([string]::IsNullOrWhiteSpace($verStr)) {
        throw ("update.json público sem version legível. Props=[{0}] len={1}" -f ($props -join ','), $body.Length)
    }
    if (-not [string]::IsNullOrWhiteSpace($ExpectedVersion) -and $verStr -ne $ExpectedVersion) {
        Write-Warning ("Manifesto público version={0} difere de ExpectedVersion={1}" -f $verStr, $ExpectedVersion)
    }

    Write-Gh ("Manifesto público OK: version={0} bytes={1}" -f $verStr, $body.Length)
    return $PublicUrl
}

function Assert-UpdatesRepositoryPublic {
    param(
        [Parameter(Mandatory = $true)][string]$Gh,
        [Parameter(Mandatory = $true)][string]$Repository
    )
    $Result = Invoke-GhCapture -Gh $Gh -Arguments @(
        'api', "repos/$Repository", '--jq', '.private'
    )
    if ($Result.ExitCode -ne 0) {
        Write-Warning ("Não foi possível consultar visibilidade de {0}: {1}" -f $Repository, $Result.Output)
        return
    }
    $flag = ([string]$Result.Output).Trim().ToLowerInvariant()
    if ($flag -eq 'true') {
        throw @"
O repositório $Repository está PRIVADO.
O launcher OmniGhost descarrega update.json sem autenticação GitHub; com repo privado o cliente recebe sempre HTTP 404.
Torna o repositório OmniGhost-Updates público (Settings → Danger zone → Change visibility → Public) e volta a executar Publish|x64.
"@
    }
    Write-Gh "Repositório $Repository está público (verificação OK)."
}

function Set-AndConfirmLatestRelease {
    param(
        [Parameter(Mandatory = $true)][string]$Gh,
        [Parameter(Mandatory = $true)][string]$Repository,
        [Parameter(Mandatory = $true)][string]$ReleaseTag
    )

    $EditResult = Invoke-GhCapture -Gh $Gh -Arguments @(
        'release', 'edit', $ReleaseTag,
        '--repo', $Repository,
        '--latest'
    )
    if ($EditResult.ExitCode -ne 0) {
        throw "Não foi possível marcar ${ReleaseTag} como Latest: $($EditResult.Output)"
    }

    $LastObservedTag = ''
    for ($Attempt = 1; $Attempt -le 8; $Attempt++) {
        $LatestResult = Invoke-GhCapture -Gh $Gh -Arguments @(
            'api', "repos/$Repository/releases/latest",
            '--jq', '.tag_name'
        )

        if ($LatestResult.ExitCode -eq 0) {
            $LastObservedTag = ([string]$LatestResult.Output).Trim()
            if ($LastObservedTag -eq $ReleaseTag) {
                Write-Gh "Latest confirmado pela API: $ReleaseTag"
                return
            }
        }
        else {
            $LastObservedTag = $LatestResult.Output
        }

        Start-Sleep -Milliseconds (400 * $Attempt)
    }

    throw "A Release foi publicada, mas a API Latest devolveu '$LastObservedTag' em vez de '$ReleaseTag'."
}

function Confirm-RemoteAssets {
    param(
        [Parameter(Mandatory = $true)]$View,
        [Parameter(Mandatory = $true)][string[]]$Required,
        [Parameter(Mandatory = $true)]$LocalInfo,
        [Parameter(Mandatory = $true)][bool]$ExpectedDraft
    )

    if ([string]$View.tagName -ne $Tag) {
        throw "tagName remoto '$($View.tagName)' difere de '$Tag'."
    }
    if ([string]$View.name -ne "OmniGhost $Version") {
        throw "Título remoto '$($View.name)' difere de 'OmniGhost $Version'."
    }
    if ([bool]$View.isPrerelease) {
        throw 'A Release foi criada incorretamente como prerelease.'
    }
    if ([bool]$View.isDraft -ne $ExpectedDraft) {
        throw "O estado draft remoto não corresponde à configuração."
    }

    $RemoteByName = @{}
    foreach ($Asset in @($View.assets)) {
        $RemoteByName[[string]$Asset.name] = $Asset
    }
    foreach ($Name in $Required) {
        if (-not $RemoteByName.ContainsKey($Name)) {
            throw "Asset remoto em falta: $Name"
        }
        $Remote = $RemoteByName[$Name]
        if ($null -ne $Remote.size -and [int64]$Remote.size -le 0) {
            throw "Asset remoto com tamanho zero: $Name"
        }
        if ($null -ne $Remote.size -and [int64]$Remote.size -ne [int64]$LocalInfo[$Name].Size) {
            throw "O tamanho remoto de '$Name' difere do ficheiro local."
        }
        if ($Remote.PSObject.Properties.Name -contains 'digest' -and $Remote.digest) {
            $ExpectedDigest = 'sha256:' + [string]$LocalInfo[$Name].Sha256
            if ([string]$Remote.digest -ne $ExpectedDigest) {
                throw "O digest remoto de '$Name' difere do SHA-256 local."
            }
        }
    }
}

if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão inválida para publicação: '$Version'."
}
if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    Write-Gh 'release-publish.json ausente — publicação ignorada.'
    return
}

$Config = Get-Content -LiteralPath $ConfigPath -Raw | ConvertFrom-Json
if (-not $Config.enabled) {
    Write-Gh 'Publicação automática desativada.'
    return
}
$Repository = if ($Config.repository) {
    [string]$Config.repository
} else {
    'rodrigomatossilva07-rgb/OmniGhost-Updates'
}
$Branch = if ($Config.targetBranch) { [string]$Config.targetBranch } else { 'main' }
$Draft = if ($null -ne $Config.draft) { [bool]$Config.draft } else { $true }
$MakeLatest = if ($null -ne $Config.makeLatest) { [bool]$Config.makeLatest } else { $true }
# Canonical local/CI artefacts are always preserved after upload.
# Remote publication never owns local cleanup.
Write-Gh 'Publicação explícita autorizada.'
Write-Gh "Repositório: $Repository"
Write-Gh "Tag: $Tag"
Write-Gh "Draft: $Draft"

$RequireSignatures = $Config.requireManifestSignature -eq $true
$Required = Get-RequiredAssets -RequireSignatures $RequireSignatures
$LocalInfo = Get-LocalAssetInfo -Names $Required
$AssetPaths = @($Required | ForEach-Object { $LocalInfo[$_].Path })

$UpdateManifest = Get-Content -LiteralPath $LocalInfo['update.json'].Path -Raw | ConvertFrom-Json
if ([string]$UpdateManifest.version -ne $Version -or
    [string]$UpdateManifest.packages[0].fileName -ne 'OmniGhost.zip' -or
    [int64]$UpdateManifest.packages[0].size -ne [int64]$LocalInfo['OmniGhost.zip'].Size -or
    [string]$UpdateManifest.packages[0].sha256 -ne [string]$LocalInfo['OmniGhost.zip'].Sha256 -or
    [string]$UpdateManifest.packages[0].url -notlike "*/$Tag/OmniGhost.zip") {
    throw 'update.json não corresponde ao OmniGhost.zip ou à tag calculada.'
}
$Changelog = Get-Content -LiteralPath $LocalInfo['changelog.json'].Path -Raw | ConvertFrom-Json
if (-not ($Changelog.releases | Where-Object { $_.version -eq $Version -and $_.tag -eq $Tag })) {
    throw "changelog.json não contém a versão/tag $Version/$Tag."
}

$Gh = Find-Gh
if (-not $Gh) {
    throw "GitHub CLI (gh) não encontrada. Instala com: winget install --id GitHub.cli -e ; depois fecha e abre o Visual Studio e executa: gh auth login"
}
Write-Gh "GitHub CLI encontrada: $Gh"

$AuthResult = Invoke-GhCapture -Gh $Gh -Arguments @('auth', 'status')
if ($AuthResult.ExitCode -ne 0) {
    throw "GitHub CLI sem autenticação válida. Executa 'gh auth login'. Detalhes: $($AuthResult.Output)"
}
Write-Gh 'Autenticação válida.'
Assert-UpdatesRepositoryPublic -Gh $Gh -Repository $Repository

if ($ValidateOnly) {
    Write-Gh 'Preflight concluído: configuração, autenticação e assets locais estão válidos.'
    return
}

$Existing = Get-ReleaseView -Gh $Gh -Repository $Repository -ReleaseTag $Tag
$CreatedRelease = $false
$View = $null

if ($Existing) {
    Write-Gh "A Release $Tag já existe; a atualizar os assets assinados com --clobber."
    $UploadArguments = @('release', 'upload', $Tag) + $AssetPaths + @(
        '--repo', $Repository, '--clobber')
    $UploadResult = Invoke-GhCapture -Gh $Gh -Arguments $UploadArguments
    if ($UploadResult.ExitCode -ne 0) {
        throw "Não foi possível atualizar os assets da Release ${Tag}: $($UploadResult.Output)"
    }

    $EditArguments = @(
        'release', 'edit', $Tag,
        '--repo', $Repository,
        '--title', "OmniGhost $Version",
        '--notes-file', $LocalInfo['release-notes.md'].Path
    )
    if ($Draft) {
        $EditArguments += '--draft'
    }
    else {
        $EditArguments += '--draft=false'
        if ($MakeLatest) { $EditArguments += '--latest' }
    }
    $EditResult = Invoke-GhCapture -Gh $Gh -Arguments $EditArguments
    if ($EditResult.ExitCode -ne 0) {
        throw "Assets atualizados, mas não foi possível atualizar os metadados da Release ${Tag}: $($EditResult.Output)"
    }

    $View = Get-ReleaseView -Gh $Gh -Repository $Repository -ReleaseTag $Tag
    if (-not $View) { throw "A Release $Tag deixou de estar acessível depois da atualização." }
    Confirm-RemoteAssets -View $View -Required $Required -LocalInfo $LocalInfo -ExpectedDraft $Draft
    if (-not $Draft -and $MakeLatest) {
        Set-AndConfirmLatestRelease -Gh $Gh -Repository $Repository -ReleaseTag $Tag
    }
    Write-Gh 'Release existente atualizada e validada.'
}
else {
    # gh release create também suporta uma tag Git já existente. Não recusamos
    # esse caso: apenas criamos a Release apontada à tag/branch configurada.
    $Arguments = @('release', 'create', $Tag) + $AssetPaths + @(
        '--repo', $Repository,
        '--target', $Branch,
        '--title', "OmniGhost $Version",
        '--notes-file', $LocalInfo['release-notes.md'].Path,
        '--draft'
    )
    Write-Gh 'A criar Release draft para validação dos assets...'

    try {
        $CreateResult = Invoke-GhCapture -Gh $Gh -Arguments $Arguments
        if ($CreateResult.ExitCode -ne 0) {
            throw "gh release create falhou com o código $($CreateResult.ExitCode): $($CreateResult.Output)"
        }
        $CreatedRelease = $true
        if (-not [string]::IsNullOrWhiteSpace($CreateResult.Output)) {
            Write-Gh $CreateResult.Output
        }

        $View = Get-ReleaseView -Gh $Gh -Repository $Repository -ReleaseTag $Tag
        if (-not $View) { throw "Não foi possível validar a Release remota $Tag." }
        Confirm-RemoteAssets -View $View -Required $Required -LocalInfo $LocalInfo -ExpectedDraft $true
        Write-Gh 'Todos os assets da draft foram validados.'

        if (-not $Draft) {
            Write-Gh 'A publicar a Release validada...'
            $EditArguments = @('release', 'edit', $Tag, '--repo', $Repository, '--draft=false')
            if ($MakeLatest) { $EditArguments += '--latest' }
            $EditResult = Invoke-GhCapture -Gh $Gh -Arguments $EditArguments
            if ($EditResult.ExitCode -ne 0) {
                throw "Não foi possível publicar a Release validada: $($EditResult.Output)"
            }

            $View = Get-ReleaseView -Gh $Gh -Repository $Repository -ReleaseTag $Tag
            if (-not $View) { throw "A Release publicada $Tag deixou de estar acessível." }
            Confirm-RemoteAssets -View $View -Required $Required -LocalInfo $LocalInfo -ExpectedDraft $false
            if ($MakeLatest) {
                Write-Gh 'A confirmar explicitamente o marcador Latest...'
                Set-AndConfirmLatestRelease -Gh $Gh -Repository $Repository -ReleaseTag $Tag
            }
        }
    }
    catch {
        $OriginalError = $_
        if ($CreatedRelease) {
            Write-Warning "A remover a Release incompleta $Tag..."
            $CleanupResult = Invoke-GhCapture -Gh $Gh -Arguments @(
                'release', 'delete', $Tag, '--repo', $Repository, '--yes', '--cleanup-tag')
            if ($CleanupResult.ExitCode -ne 0) {
                Write-Warning "Não foi possível remover automaticamente a Release incompleta: $($CleanupResult.Output)"
            }
        }
        throw $OriginalError
    }
}

Write-Gh 'Assets enviados.'
Write-Gh 'Release validada com sucesso.'
if (-not $Draft) {
    $publicManifestUrl = Test-PublicUpdateManifest -Repository $Repository -ExpectedVersion $Version
    Write-Gh "URL pública confirmada: $publicManifestUrl"
}
if ($Draft) {
    Write-Gh 'Release em modo draft — revê e publica manualmente no GitHub.'
}
else {
    Write-Gh 'Release publicada e marcada como Latest.'
}
Write-Gh "URL: $($View.url)"

Write-Gh "Artefactos locais preservados em: $ReleaseDirectory"

# Discord #updates — only after a successful non-draft publish; webhook from env/secret only
if (-not $Draft) {
    $NotifyScript = Join-Path $PSScriptRoot 'Notify-DiscordRelease.ps1'
    if (Test-Path -LiteralPath $NotifyScript -PathType Leaf) {
        try {
            & $NotifyScript -ProjectDir $ProjectDir -Version $Version -ReleaseDir $ReleaseDirectory
        }
        catch {
            $Safe = [string]$_.Exception.Message -replace 'https://discord(?:app)?\.com/api/webhooks/\S+', '[webhook redacted]'
            Write-Warning "Discord notification failed (release still published): $Safe"
        }
    }
    else {
        Write-Gh 'Notify-DiscordRelease.ps1 not found — Discord step skipped.'
    }
}
else {
    Write-Gh 'Draft release — Discord notification skipped.'
}

return
