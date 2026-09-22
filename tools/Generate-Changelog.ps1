[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$OutputDir,

    [string]$HistoryPath,

    [string]$SemanticAnalysisPath,

    [switch]$SkipRootChangelog
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectDir 'artifacts\release'
}
$OutputDir = [System.IO.Path]::GetFullPath((Join-Path $OutputDir '.'))
$OverridePath = Join-Path $ProjectDir 'config\release\release-notes.override.json'
if ([string]::IsNullOrWhiteSpace($HistoryPath)) {
    $HistoryPath = Join-Path $ProjectDir 'artifacts\changelog-history.json'
}
else {
    $HistoryPath = [System.IO.Path]::GetFullPath($HistoryPath)
}
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
$Tag = "v$Version"
$PublishedAt = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$Repository = 'rodrigomatossilva07-rgb/OmniGhost-Updates'
$ReleaseAuthor = 'rodrigomatossilva07-rgb'
$ReleaseUrl = "https://github.com/$Repository/releases/tag/$Tag"
$Commit = ''
$PreviousTag = $null
$ChangedPathSet = @{}

$Utf8Strict = New-Object System.Text.UTF8Encoding -ArgumentList @($false, $true)
$Windows1252 = [System.Text.Encoding]::GetEncoding(1252)
$MojibakePattern = '(?:\u00C3[\u00A0-\u00BF\u0192\u201A]|\u00C2[\u00A0\u00AB\u00BB]|\u00E2\u20AC(?:[\u201C\u201D\u00A2])?|\u00F0\u0178|\uFFFD)'

function Read-TextUtf8 {
    param([Parameter(Mandatory = $true)][string]$Path)

    try {
        return [System.IO.File]::ReadAllText($Path, $Utf8Strict)
    }
    catch {
        throw "Não foi possível ler '$Path' como UTF-8 válido: $($_.Exception.Message)"
    }
}

function Read-JsonUtf8 {
    param([Parameter(Mandatory = $true)][string]$Path)

    $Content = Read-TextUtf8 -Path $Path
    try {
        return $Content | ConvertFrom-Json
    }
    catch {
        throw "JSON inválido em '$Path': $($_.Exception.Message)"
    }
}

function Get-MojibakeScore {
    param([string]$Text)

    if ([string]::IsNullOrEmpty($Text)) {
        return 0
    }

    return [regex]::Matches($Text, $MojibakePattern).Count
}

function Repair-MojibakeText {
    param([string]$Text)

    if ([string]::IsNullOrEmpty($Text)) { return $Text }

    $Current = $Text
    for ($Pass = 0; $Pass -lt 4; ++$Pass) {
        $BeforeScore = Get-MojibakeScore -Text $Current
        if ($BeforeScore -eq 0) { break }

        try {
            $Bytes = $Windows1252.GetBytes($Current)
            $Candidate = $Utf8Strict.GetString($Bytes)
        }
        catch {
            break
        }

        $AfterScore = Get-MojibakeScore -Text $Candidate
        if ($Candidate -eq $Current -or
            $Candidate.Contains([string][char]0xFFFD) -or
            $AfterScore -ge $BeforeScore) {
            break
        }

        $Current = $Candidate
    }

    return $Current
}

function Repair-ObjectText {
    param($Value)

    if ($null -eq $Value) { return $Value }
    if ($Value -is [string]) {
        return Repair-MojibakeText -Text ([string]$Value)
    }

    if ($Value -is [System.Collections.IDictionary]) {
        foreach ($Key in @($Value.Keys)) {
            $Value[$Key] = Repair-ObjectText -Value $Value[$Key]
        }
        return $Value
    }

    if ($Value -is [System.Collections.IList]) {
        for ($Index = 0; $Index -lt $Value.Count; ++$Index) {
            $Value[$Index] = Repair-ObjectText -Value $Value[$Index]
        }
        return $Value
    }

    foreach ($Property in @($Value.PSObject.Properties)) {
        if (-not $Property.IsSettable) { continue }
        $Property.Value = Repair-ObjectText -Value $Property.Value
    }
    return $Value
}


function Write-ChangelogLog {
    param([string]$Message)
    Write-Host "[OmniGhost Changelog] $Message"
}

function Get-PropertyValue {
    param(
        $Object,
        [string]$Name,
        $Default = $null
    )

    if ($null -eq $Object) { return $Default }
    if ($Object.PSObject.Properties.Name -contains $Name) {
        return $Object.$Name
    }
    return $Default
}

function ConvertTo-NormalizedRelease {
    param(
        [Parameter(Mandatory = $true)]$Release,
        [Parameter(Mandatory = $true)][string]$RepositoryName
    )

    $ReleaseVersion = [string](Get-PropertyValue -Object $Release -Name 'version' -Default '')
    $ReleaseTag = [string](Get-PropertyValue -Object $Release -Name 'tag' -Default '')
    $ReleaseId = [string](Get-PropertyValue -Object $Release -Name 'id' -Default '')
    $Published = [string](Get-PropertyValue -Object $Release -Name 'publishedAt' -Default '')
    $ReleaseTitle = [string](Get-PropertyValue -Object $Release -Name 'title' -Default '')
    $ReleaseSummary = [string](Get-PropertyValue -Object $Release -Name 'summary' -Default '')

    if ([string]::IsNullOrWhiteSpace($ReleaseVersion) -or
        [string]::IsNullOrWhiteSpace($ReleaseTag) -or
        [string]::IsNullOrWhiteSpace($ReleaseId) -or
        [string]::IsNullOrWhiteSpace($Published) -or
        [string]::IsNullOrWhiteSpace($ReleaseTitle)) {
        return $null
    }

    $Owner = ($RepositoryName -split '/', 2)[0]
    $Author = [string](Get-PropertyValue -Object $Release -Name 'author' -Default $Owner)
    if ([string]::IsNullOrWhiteSpace($Author)) { $Author = $Owner }

    $ReleaseCommit = [string](Get-PropertyValue -Object $Release -Name 'commit' -Default '')
    $PublicUrl = [string](Get-PropertyValue -Object $Release -Name 'releaseUrl' -Default '')
    if ([string]::IsNullOrWhiteSpace($PublicUrl)) {
        $PublicUrl = "https://github.com/$RepositoryName/releases/tag/$ReleaseTag"
    }

    $NormalizedModules = @()
    foreach ($Module in @(Get-PropertyValue -Object $Release -Name 'modules' -Default @())) {
        if ($null -eq $Module) { continue }

        $ModuleId = [string](Get-PropertyValue -Object $Module -Name 'id' -Default '')
        $ModuleName = [string](Get-PropertyValue -Object $Module -Name 'name' -Default '')
        $ModuleVersion = Get-PropertyValue -Object $Module -Name 'version' -Default $null
        if ([string]::IsNullOrWhiteSpace($ModuleId) -or
            [string]::IsNullOrWhiteSpace($ModuleName)) {
            continue
        }

        $NormalizedChanges = @()
        foreach ($Change in @(Get-PropertyValue -Object $Module -Name 'changes' -Default @())) {
            if ($null -eq $Change) { continue }
            $Type = [string](Get-PropertyValue -Object $Change -Name 'type' -Default '')
            $Text = Normalize-Text ([string](Get-PropertyValue -Object $Change -Name 'text' -Default ''))
            if ([string]::IsNullOrWhiteSpace($Type) -or
                [string]::IsNullOrWhiteSpace($Text)) {
                continue
            }
            $NormalizedChanges += [pscustomobject][ordered]@{
                type = $Type
                text = $Text
                evidence = @((Get-PropertyValue -Object $Change -Name 'evidence' -Default @()))
            }
        }

        if ($NormalizedChanges.Count -eq 0) { continue }
        $NormalizedModules += [pscustomobject][ordered]@{
            id = $ModuleId
            name = $ModuleName
            version = $ModuleVersion
            changes = @($NormalizedChanges)
        }
    }

    if ($NormalizedModules.Count -eq 0) { return $null }

    return [pscustomobject][ordered]@{
        id = $ReleaseId
        version = $ReleaseVersion
        tag = $ReleaseTag
        publishedAt = $Published
        title = $ReleaseTitle
        summary = $ReleaseSummary
        author = $Author
        commit = $ReleaseCommit
        releaseUrl = $PublicUrl
        modules = @($NormalizedModules)
    }
}

function Write-TextAtomic {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )

    $Directory = Split-Path -Parent $Path
    if (-not $Directory) { throw "Diretório inválido para escrita: $Path" }
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null

    $TemporaryPath = Join-Path $Directory ((Split-Path -Leaf $Path) + '.tmp-' + [guid]::NewGuid().ToString('N'))
    try {
        [System.IO.File]::WriteAllText($TemporaryPath, $Text, $Utf8NoBom)
        $null = [System.IO.File]::ReadAllText($TemporaryPath)
        if (Test-Path -LiteralPath $Path) {
            Remove-Item -LiteralPath $Path -Force
        }
        Move-Item -LiteralPath $TemporaryPath -Destination $Path -Force
    }
    finally {
        Remove-Item -LiteralPath $TemporaryPath -Force -ErrorAction SilentlyContinue
    }
}

function Normalize-Text {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) { return '' }
    $Normalized = [regex]::Replace($Text.Trim(), '\s+', ' ')
    if ($Normalized.Length -gt 0) {
        $Normalized = $Normalized.Substring(0, 1).ToUpperInvariant() + $Normalized.Substring(1)
    }
    if ($Normalized.Length -gt 0) {
        $LastCharacter = $Normalized.Substring($Normalized.Length - 1, 1)
        if (@('.', '!', '?') -notcontains $LastCharacter) {
            $Normalized += '.'
        }
    }
    return $Normalized
}

function Get-ModuleInfo {
    param([string]$Scope)

    $NormalizedScope = if ($Scope) { $Scope.ToLowerInvariant() } else { '' }
    switch -Regex ($NormalizedScope) {
        '^(launcher|core|app|ui|updater|platform)$' { return @{ id = 'core'; name = 'OMNIGHOST Launcher' } }
        '^(fivem|gta|gtav)$' { return @{ id = 'fivem'; name = 'FiveM' } }
        '^(cs2|counter-?strike)$' { return @{ id = 'cs2'; name = 'Counter-Strike 2' } }
        '^(rust)$' { return @{ id = 'rust'; name = 'Rust' } }
        '^(warzone|cod|mw)$' { return @{ id = 'warzone'; name = 'Warzone' } }
        '^(valorant|val)$' { return @{ id = 'valorant'; name = 'Valorant' } }
        '^(apex)$' { return @{ id = 'apex'; name = 'Apex Legends' } }
        default { return @{ id = 'other'; name = 'Outras alterações' } }
    }
}

function Get-ChangeType {
    param(
        [string]$CommitType,
        [bool]$Breaking
    )

    if ($Breaking) { return 'breaking' }
    switch ($CommitType.ToLowerInvariant()) {
        'feat' { return 'added' }
        'fix' { return 'fixed' }
        'perf' { return 'performance' }
        'security' { return 'security' }
        'compat' { return 'compatibility' }
        'remove' { return 'removed' }
        'refactor' { return 'improved' }
        'improve' { return 'improved' }
        default { return $null }
    }
}

function Get-ChangeTypeLabel {
    param([string]$Type)

    switch ($Type) {
        'added' { return 'Adicionado' }
        'improved' { return 'Melhorado' }
        'fixed' { return 'Corrigido' }
        'performance' { return 'Desempenho' }
        'compatibility' { return 'Compatibilidade' }
        'security' { return 'Segurança' }
        'removed' { return 'Removido' }
        'breaking' { return 'Alteração importante' }
        'maintenance' { return 'Manutenção' }
        default { return $Type }
    }
}


function Get-ChangeTypeIcon {
    param([string]$Type)

    switch ($Type) {
        'added' { return ':sparkles:' }
        'improved' { return ':rocket:' }
        'fixed' { return ':wrench:' }
        'performance' { return ':zap:' }
        'compatibility' { return ':arrows_counterclockwise:' }
        'security' { return ':lock:' }
        'removed' { return ':wastebasket:' }
        'breaking' { return ':warning:' }
        'maintenance' { return ':gear:' }
        default { return ':small_blue_diamond:' }
    }
}

if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão inválida para o changelog: '$Version'."
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null

$PublishConfigPath = Join-Path $ProjectDir 'config\release\release-publish.json'
if (Test-Path -LiteralPath $PublishConfigPath -PathType Leaf) {
    $PublishConfig = Read-JsonUtf8 -Path $PublishConfigPath
    $ConfiguredRepository = [string](Get-PropertyValue -Object $PublishConfig -Name 'repository' -Default '')
    if ($ConfiguredRepository -match '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
        $Repository = $ConfiguredRepository
        $ReleaseAuthor = ($Repository -split '/', 2)[0]
        $ReleaseUrl = "https://github.com/$Repository/releases/tag/$Tag"
    }
}

$Title = "OmniGhost $Version"
$Summary = ''
$HideCommits = @()
$ManualChanges = @()

if (Test-Path -LiteralPath $OverridePath -PathType Leaf) {
    try {
        $Override = Read-JsonUtf8 -Path $OverridePath
        $OverrideVersion = [string](Get-PropertyValue -Object $Override -Name 'version' -Default 'auto')
        if ($OverrideVersion -and $OverrideVersion -ne 'auto' -and $OverrideVersion -ne $Version) {
            throw "A versão '$OverrideVersion' do override não corresponde à versão '$Version'."
        }

        $OverrideTitle = [string](Get-PropertyValue -Object $Override -Name 'title' -Default '')
        if ($OverrideTitle) {
            $Title = $OverrideTitle
            $EscapedVersion = [regex]::Escape($Version)
            if ($Title -notmatch $EscapedVersion) {
                $Title = "$Title $Version"
            }
        }

        $OverrideSummary = [string](Get-PropertyValue -Object $Override -Name 'summary' -Default '')
        if ($OverrideSummary) { $Summary = $OverrideSummary }

        $OverridePublishedAt = [string](Get-PropertyValue -Object $Override -Name 'publishedAt' -Default '')
        if ($OverridePublishedAt) { $PublishedAt = $OverridePublishedAt }

        $OverrideHide = Get-PropertyValue -Object $Override -Name 'hideCommits' -Default @()
        foreach ($Item in @($OverrideHide)) {
            if ($null -ne $Item -and -not [string]::IsNullOrWhiteSpace([string]$Item)) {
                $HideCommits += [string]$Item
            }
        }

        $AllowedTypes = @('added', 'improved', 'fixed', 'performance', 'compatibility', 'security', 'removed', 'breaking', 'maintenance')
        $OverrideChanges = Get-PropertyValue -Object $Override -Name 'changes' -Default @()
        foreach ($Change in @($OverrideChanges)) {
            $ChangeType = [string](Get-PropertyValue -Object $Change -Name 'type' -Default '')
            $ChangeText = Normalize-Text ([string](Get-PropertyValue -Object $Change -Name 'text' -Default ''))
            $ChangeModule = [string](Get-PropertyValue -Object $Change -Name 'module' -Default 'core')
            $AllowRepeat = [bool](Get-PropertyValue -Object $Change -Name 'allowRepeat' -Default $false)

            if ($AllowedTypes -notcontains $ChangeType) {
                throw "Categoria manual inválida: '$ChangeType'."
            }
            if (-not $ChangeText) {
                throw 'Uma alteração manual possui texto vazio.'
            }

            $ManualChanges += [pscustomobject]@{
                module = $ChangeModule
                type = $ChangeType
                text = $ChangeText
                allowRepeat = $AllowRepeat
            }
        }

        Write-ChangelogLog "Override carregado: $OverridePath"
    }
    catch {
        throw "Falha ao ler config\release\release-notes.override.json: $($_.Exception.Message)"
    }
}

$SemanticModeRequested = -not [string]::IsNullOrWhiteSpace($SemanticAnalysisPath)
if ($SemanticModeRequested) {
    # In semantic CI mode, an old/manual override must not contaminate the
    # evidence-backed result or the Conventional Commits fallback.
    $ManualChanges = @()
    $Summary = ''
}

$GitChanges = @()
$GitCommand = Get-Command git -ErrorAction SilentlyContinue
if ($GitCommand) {
    $OriginalLocation = Get-Location
    try {
        Set-Location -LiteralPath $ProjectDir
        $InsideWorkTree = & $GitCommand.Source rev-parse --is-inside-work-tree 2>$null
        if ($LASTEXITCODE -eq 0 -and [string]$InsideWorkTree -eq 'true') {
            $ResolvedCommit = & $GitCommand.Source rev-parse --short=7 HEAD 2>$null
            if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace([string]$ResolvedCommit)) {
                $Commit = ([string]$ResolvedCommit).Trim()
            }

            $PreviousTag = $null
            $Tags = & $GitCommand.Source tag --list 'v*' --sort=-v:refname 2>$null
            foreach ($CandidateTag in @($Tags)) {
                $Candidate = ([string]$CandidateTag).Trim()
                if ($Candidate -and $Candidate -ne $Tag) {
                    $PreviousTag = $Candidate
                    break
                }
            }

            $Range = if ($PreviousTag) { "$PreviousTag..HEAD" } else { 'HEAD' }
            Write-ChangelogLog "Intervalo de commits: $Range"
            $CommitMessages = & $GitCommand.Source log $Range --pretty=format:'%H%x09%s' --no-merges 2>$null
            if ($LASTEXITCODE -eq 0) {
                foreach ($RawMessage in @($CommitMessages)) {
                    $CommitParts = ([string]$RawMessage) -split "`t", 2
                    if ($CommitParts.Count -ne 2) { continue }
                    $CommitHash = $CommitParts[0]
                    $Message = $CommitParts[1]
                    if ([string]::IsNullOrWhiteSpace($Message)) { continue }
                    if ($Message -match '^(chore|build|ci|test|style|docs)(\(.+\))?(!)?:') { continue }
                    if ($Message -match '(?i)bump version|update version|release:\s|chore\(release\)') { continue }

                    $Hidden = $false
                    foreach ($HiddenPattern in $HideCommits) {
                        if ($Message -like "*$HiddenPattern*") {
                            $Hidden = $true
                            break
                        }
                    }
                    if ($Hidden) { continue }

                    $CommitMatch = [regex]::Match($Message, '^(?<type>\w+)(?:\((?<scope>[^)]+)\))?(?<breaking>!)?:\s*(?<text>.+)$')
                    if (-not $CommitMatch.Success) { continue }

                    $IsBreaking = $CommitMatch.Groups['breaking'].Success -or ($Message -match 'BREAKING CHANGE')
                    $MappedType = Get-ChangeType -CommitType $CommitMatch.Groups['type'].Value -Breaking $IsBreaking
                    if (-not $MappedType) { continue }

                    $Module = Get-ModuleInfo -Scope $CommitMatch.Groups['scope'].Value
                    $Text = Normalize-Text $CommitMatch.Groups['text'].Value
                    if (-not $Text) { continue }

                    $Evidence = @(& $GitCommand.Source diff-tree --no-commit-id --name-only -r $CommitHash 2>$null |
                        ForEach-Object { ([string]$_ -replace '\\', '/').Trim() } |
                        Where-Object {
                            $_ -and $_ -notmatch '(?i)(^|/)(bin|obj|build|artifacts|\.cache|\.vs|x64|logs?|temp|tmp)(/|$)' -and
                            $_ -notmatch '(?i)\.(exe|dll|lib|pdb|obj|zip|png|jpe?g|gif|ico|log|tmp)$' -and
                            $_ -notmatch '(?i)(^|/)(CHANGELOG\.md|release-notes\.md|changelog\.json|release-meta\.json|update\.json|version\.txt)$'
                        } | Select-Object -Unique)
                    if ($Evidence.Count -eq 0) { continue }

                    $GitChanges += [pscustomobject]@{
                        module = [string]$Module.id
                        moduleName = [string]$Module.name
                        type = $MappedType
                        text = $Text
                        evidence = @($Evidence)
                        allowRepeat = $false
                    }
                }
            }

            # The path list is evidence validation only. Filenames never create
            # release-note claims; semantic AI or Conventional Commits do that.
            Write-ChangelogLog "A recolher evidence do git diff $Range --name-status..."
            $NameStatus = & $GitCommand.Source diff --name-status $Range 2>$null
            if ($LASTEXITCODE -eq 0) {
                $FileCount = 0
                foreach ($Line in @($NameStatus)) {
                    $Raw = [string]$Line
                    if ([string]::IsNullOrWhiteSpace($Raw)) { continue }
                    $Parts = $Raw -split "`t"
                    if ($Parts.Count -lt 2) { continue }
                    $Status = $Parts[0].Substring(0, 1)
                    $FilePath = ([string]$Parts[$Parts.Count - 1] -replace '\\', '/').Trim()
                    $FileCount++
                    if ($FilePath) { $ChangedPathSet[$FilePath.ToLowerInvariant()] = $true }
                }
                Write-ChangelogLog "Ficheiros no diff disponíveis para evidence: $FileCount"
            }
            else {
                Write-ChangelogLog 'git diff indisponível para este intervalo.'
            }
        }
        else {
            Write-ChangelogLog 'A pasta atual não é um repositório Git; serão usadas apenas notas manuais.'
        }
    }
    catch {
        Write-ChangelogLog "Git indisponível para o changelog: $($_.Exception.Message)"
    }
    finally {
        Set-Location -LiteralPath $OriginalLocation.Path
    }
}
else {
    Write-ChangelogLog 'Git não foi encontrado; serão usadas apenas notas manuais.'
}

$SemanticAccepted = $false
if ([string]::IsNullOrWhiteSpace($SemanticAnalysisPath)) {
    $CandidateSemanticPath = Join-Path $OutputDir 'semantic-analysis.json'
    if (Test-Path -LiteralPath $CandidateSemanticPath -PathType Leaf) {
        $SemanticAnalysisPath = $CandidateSemanticPath
    }
}
if (-not [string]::IsNullOrWhiteSpace($SemanticAnalysisPath) -and
    (Test-Path -LiteralPath $SemanticAnalysisPath -PathType Leaf)) {
    try {
        $Semantic = Read-JsonUtf8 -Path $SemanticAnalysisPath
        $AllowedSemanticTypes = @('added','improved','fixed','performance','compatibility','security','removed','breaking','maintenance')
        $AllowedModuleIds = @('core','fivem','cs2','rust','warzone','valorant','apex','other')
        $SemanticChanges = @()
        foreach ($SemanticModule in @($Semantic.modules)) {
            $ModuleId = [string]$SemanticModule.id
            if ($AllowedModuleIds -notcontains $ModuleId) { throw "Módulo semântico inválido: '$ModuleId'." }
            $CanonicalModule = Get-ModuleInfo -Scope $ModuleId
            foreach ($SemanticChange in @($SemanticModule.changes)) {
                $Type = [string]$SemanticChange.type
                $Text = Normalize-Text ([string]$SemanticChange.text)
                $Evidence = @($SemanticChange.evidence | ForEach-Object { ([string]$_ -replace '\\','/').Trim() } | Select-Object -Unique)
                if ($AllowedSemanticTypes -notcontains $Type) { throw "Tipo semântico inválido: '$Type'." }
                if (-not $Text -or $Evidence.Count -eq 0) { throw 'Alteração semântica sem texto/evidence.' }
                foreach ($EvidencePath in $Evidence) {
                    if (-not $ChangedPathSet.ContainsKey($EvidencePath.ToLowerInvariant())) {
                        throw "Evidence não pertence ao diff: '$EvidencePath'."
                    }
                }
                $SemanticChanges += [pscustomobject]@{
                    module = [string]$CanonicalModule.id
                    moduleName = [string]$CanonicalModule.name
                    type = $Type
                    text = $Text
                    evidence = @($Evidence)
                    allowRepeat = $false
                }
            }
        }
        if ($SemanticChanges.Count -gt 0) {
            $GitChanges = @($SemanticChanges)
            $ManualChanges = @() # validated semantic diff is authoritative
            $SemanticSummary = Normalize-Text ([string]$Semantic.summary)
            if ($SemanticSummary) { $Summary = $SemanticSummary }
            $SemanticAccepted = $true
            Write-ChangelogLog "Análise semântica aceite: $($SemanticChanges.Count) alteração(ões) com evidence."
        }
    }
    catch {
        Write-ChangelogLog "Análise semântica rejeitada; fallback Conventional Commits: $($_.Exception.Message)"
    }
}

$ExistingKeys = @{}
$PreviousReleases = @()
if (Test-Path -LiteralPath $HistoryPath -PathType Leaf) {
    try {
        $ExistingHistory = Repair-ObjectText -Value (Read-JsonUtf8 -Path $HistoryPath)
        $RawPreviousReleases = @(Get-PropertyValue -Object $ExistingHistory -Name 'releases' -Default @())
        $PreviousReleases = @()
        foreach ($RawRelease in $RawPreviousReleases) {
            $NormalizedRelease = ConvertTo-NormalizedRelease -Release $RawRelease -RepositoryName $Repository
            if ($null -ne $NormalizedRelease) {
                $PreviousReleases += $NormalizedRelease
            }
            else {
                Write-ChangelogLog 'Uma entrada antiga inválida foi removida durante a normalização do histórico.'
            }
        }
        foreach ($ExistingRelease in $PreviousReleases) {
            $ExistingReleaseVersion = [string](Get-PropertyValue -Object $ExistingRelease -Name 'version' -Default '')
            if ($ExistingReleaseVersion -eq $Version) {
                continue
            }

            foreach ($ExistingModule in @(Get-PropertyValue -Object $ExistingRelease -Name 'modules' -Default @())) {
                $ExistingModuleId = [string](Get-PropertyValue -Object $ExistingModule -Name 'id' -Default 'other')
                foreach ($ExistingChange in @(Get-PropertyValue -Object $ExistingModule -Name 'changes' -Default @())) {
                    $ExistingType = [string](Get-PropertyValue -Object $ExistingChange -Name 'type' -Default '')
                    $ExistingText = [string](Get-PropertyValue -Object $ExistingChange -Name 'text' -Default '')
                    if ($ExistingType -and $ExistingText) {
                        $ExistingKey = ("$ExistingModuleId|$ExistingType|$ExistingText").ToLowerInvariant()
                        $ExistingKeys[$ExistingKey] = $true
                    }
                }
            }
        }
    }
    catch {
        Write-ChangelogLog 'O histórico anterior não pôde ser lido; será criado um histórico novo.'
        $PreviousReleases = @()
        $ExistingKeys = @{}
    }
}

$AllChanges = @()
$CurrentKeys = @{}
$SkippedManualChangeCount = 0

foreach ($ManualChange in $ManualChanges) {
    $Module = Get-ModuleInfo -Scope $ManualChange.module
    $Key = ("$($Module.id)|$($ManualChange.type)|$($ManualChange.text)").ToLowerInvariant()
    if ($ExistingKeys.ContainsKey($Key) -and -not [bool]$ManualChange.allowRepeat) {
        $SkippedManualChangeCount++
        Write-ChangelogLog "Nota manual já existente no histórico; ignorada nesta versão: $($ManualChange.text)"
        continue
    }
    if (-not $CurrentKeys.ContainsKey($Key)) {
        $CurrentKeys[$Key] = $true
        $AllChanges += [pscustomobject]@{
            module = [string]$Module.id
            moduleName = [string]$Module.name
            type = [string]$ManualChange.type
            text = [string]$ManualChange.text
            evidence = @()
        }
    }
}

foreach ($GitChange in $GitChanges) {
    $Key = ("$($GitChange.module)|$($GitChange.type)|$($GitChange.text)").ToLowerInvariant()
    if ($ExistingKeys.ContainsKey($Key)) { continue }
    if (-not $CurrentKeys.ContainsKey($Key)) {
        $CurrentKeys[$Key] = $true
        $AllChanges += $GitChange
    }
}

if ($AllChanges.Count -eq 0) {
    # A ausência de commits Git ou a reutilização acidental de um override antigo
    # não deve interromper uma Build Release válida. Em vez disso, é criada uma
    # entrada de manutenção verdadeira sobre os artefactos gerados nesta versão.
    $FallbackText = Normalize-Text (
        "Maintenance release with no user-facing changes identified")

    $AllChanges += [pscustomobject]@{
        module = 'core'
        moduleName = 'OMNIGHOST Launcher'
        type = 'maintenance'
        text = $FallbackText
        evidence = @()
    }

    $Summary = "Maintenance release with no user-facing changes identified."

    if ($SkippedManualChangeCount -gt 0) {
        Write-ChangelogLog (
            "$SkippedManualChangeCount nota(s) manual(is) já existiam no histórico. " +
            'Foi criada uma entrada de manutenção para impedir um changelog vazio.')
    }
    else {
        Write-ChangelogLog (
            'Não foram encontrados commits ou notas manuais utilizáveis. ' +
            'Foi criada uma entrada de manutenção para a Release atual.')
    }
}

if (-not $Summary) {
    $Summary = "Atualização $Version do OmniGhost."
}

$ModulesById = @{}
foreach ($Change in $AllChanges) {
    $ModuleId = [string]$Change.module
    if (-not $ModulesById.ContainsKey($ModuleId)) {
        $ModulesById[$ModuleId] = [ordered]@{
            id = $ModuleId
            name = [string]$Change.moduleName
            version = if ($ModuleId -eq 'core') { $Version } else { $null }
            changes = @()
        }
    }
    $ModulesById[$ModuleId].changes += [ordered]@{
        type = [string]$Change.type
        text = [string]$Change.text
        evidence = @($Change.evidence)
    }
}

$Modules = @()
foreach ($ModuleId in @('core', 'fivem', 'cs2', 'rust', 'warzone', 'valorant', 'apex', 'other')) {
    if ($ModulesById.ContainsKey($ModuleId)) {
        $Modules += $ModulesById[$ModuleId]
    }
}

$NormalizedModules = @()
foreach ($Module in @($Modules)) {
    $NormalizedModuleChanges = @()
    foreach ($Change in @($Module.changes)) {
        $NormalizedModuleChanges += [pscustomobject][ordered]@{
            type = [string]$Change.type
            text = [string]$Change.text
            evidence = @($Change.evidence)
        }
    }
    $NormalizedModules += [pscustomobject][ordered]@{
        id = [string]$Module.id
        name = [string]$Module.name
        version = $Module.version
        changes = @($NormalizedModuleChanges)
    }
}

$ReleaseEntry = [pscustomobject][ordered]@{
    id = "launcher-$Version"
    version = $Version
    tag = $Tag
    publishedAt = $PublishedAt
    title = $Title
    summary = $Summary
    author = $ReleaseAuthor
    commit = $Commit
    releaseUrl = $ReleaseUrl
    modules = @($NormalizedModules)
}

$FilteredPreviousReleases = @()
foreach ($PreviousRelease in $PreviousReleases) {
    if ([string](Get-PropertyValue -Object $PreviousRelease -Name 'version' -Default '') -ne $Version) {
        $FilteredPreviousReleases += (ConvertTo-NormalizedRelease -Release $PreviousRelease -RepositoryName $Repository)
    }
}

$History = [ordered]@{
    schemaVersion = 1
    generatedAt = $PublishedAt
    channel = 'stable'
    releases = @($ReleaseEntry) + @($FilteredPreviousReleases)
}

$HistoryJson = ($History | ConvertTo-Json -Depth 16) + [Environment]::NewLine
$null = $HistoryJson | ConvertFrom-Json
Write-TextAtomic -Path $HistoryPath -Text $HistoryJson

$ChangelogPath = Join-Path $OutputDir 'changelog.json'
Write-TextAtomic -Path $ChangelogPath -Text $HistoryJson

$Markdown = New-Object System.Text.StringBuilder
[void]$Markdown.AppendLine("# $Title")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("> $Summary")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("| Versão | Canal | Tag |")
[void]$Markdown.AppendLine("|:--|:--|:--|")
[void]$Markdown.AppendLine("| ``$Version`` | ``stable`` | ``$Tag`` |")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("---")
[void]$Markdown.AppendLine()

foreach ($Module in $Modules) {
    [void]$Markdown.AppendLine("## $($Module.name)")
    [void]$Markdown.AppendLine()

    foreach ($Type in @('breaking', 'added', 'improved', 'performance', 'compatibility', 'fixed', 'security', 'removed', 'maintenance')) {
        $ChangesForType = @($Module.changes | Where-Object { [string]$_.type -eq $Type })
        if ($ChangesForType.Count -eq 0) { continue }

        $Icon = Get-ChangeTypeIcon -Type $Type
        $Label = Get-ChangeTypeLabel -Type $Type
        [void]$Markdown.AppendLine("### $Icon $Label")
        [void]$Markdown.AppendLine()
        foreach ($Change in $ChangesForType) {
            [void]$Markdown.AppendLine("- $($Change.text)")
        }
        [void]$Markdown.AppendLine()
    }
}

[void]$Markdown.AppendLine("---")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("_Histórico completo disponível na página **Atualizações** do OmniGhost._")
[void]$Markdown.AppendLine()

$NotesPath = Join-Path $OutputDir 'release-notes.md'
Write-TextAtomic -Path $NotesPath -Text $Markdown.ToString()

$CombinedContent = ((Read-TextUtf8 -Path $ChangelogPath) + (Read-TextUtf8 -Path $NotesPath)).ToUpperInvariant()
foreach ($ForbiddenText in @(
    'EXEMPLO',
    'DADOS DE EXEMPLO',
    'DADOS NÃO OFICIAIS',
    'CONTEÚDO DE DEMONSTRAÇÃO',
    'DESCREVE AQUI',
    'SUBSTITUI ESTE TEXTO'
)) {
    if ($CombinedContent.Contains($ForbiddenText)) {
        throw "Dados de demonstração detetados nos artefactos: '$ForbiddenText'."
    }
}


if ((Get-MojibakeScore -Text $CombinedContent) -gt 0) {
    throw 'Foram detetados caracteres corrompidos (mojibake) no changelog ou nas notas da Release.'
}

$Validation = Read-JsonUtf8 -Path $ChangelogPath
foreach ($ValidatedRelease in @($Validation.releases)) {
    if ($ValidatedRelease.modules -isnot [System.Array]) {
        throw "A Release '$($ValidatedRelease.version)' não contém modules como array JSON."
    }
    foreach ($ValidatedModule in @($ValidatedRelease.modules)) {
        if ($ValidatedModule.changes -isnot [System.Array]) {
            throw "O módulo '$($ValidatedModule.id)' da Release '$($ValidatedRelease.version)' não contém changes como array JSON."
        }
    }
}
if (-not @($Validation.releases | Where-Object { [string]$_.version -eq $Version -and [string]$_.tag -eq $Tag })) {
    throw "A validação final do changelog falhou para $Version/$Tag."
}
if (-not (Test-Path -LiteralPath $NotesPath -PathType Leaf) -or (Get-Item -LiteralPath $NotesPath).Length -le 0) {
    throw 'release-notes.md não foi criado corretamente.'
}

$ReleasedLocal = (Get-Date).ToString('dd/MM/yyyy')
if (-not $SkipRootChangelog) {
    # Project-root CHANGELOG.md (prepend current release; keep prior history)
    $RootChangelog = Join-Path $ProjectDir 'CHANGELOG.md'
    $RootBuilder = New-Object System.Text.StringBuilder
[void]$RootBuilder.AppendLine("## OmniGhost $Version")
[void]$RootBuilder.AppendLine()
[void]$RootBuilder.AppendLine("Released: $ReleasedLocal")
[void]$RootBuilder.AppendLine("Channel: stable")
[void]$RootBuilder.AppendLine("Tag: $Tag")
[void]$RootBuilder.AppendLine()
foreach ($Module in $Modules) {
    [void]$RootBuilder.AppendLine("### $($Module.name)")
    [void]$RootBuilder.AppendLine()
    foreach ($Type in @('breaking', 'added', 'improved', 'performance', 'compatibility', 'fixed', 'security', 'removed', 'maintenance')) {
        $ChangesForType = @($Module.changes | Where-Object { [string]$_.type -eq $Type })
        if ($ChangesForType.Count -eq 0) { continue }
        $Icon = Get-ChangeTypeIcon -Type $Type
        $Label = Get-ChangeTypeLabel -Type $Type
        [void]$RootBuilder.AppendLine("#### $Icon $Label")
        [void]$RootBuilder.AppendLine()
        foreach ($Change in $ChangesForType) {
            [void]$RootBuilder.AppendLine("- $($Change.text)")
        }
        [void]$RootBuilder.AppendLine()
    }
}
[void]$RootBuilder.AppendLine('---')
[void]$RootBuilder.AppendLine()
if (Test-Path -LiteralPath $RootChangelog -PathType Leaf) {
    $Existing = Read-TextUtf8 -Path $RootChangelog
    # Drop previous heading for same version if regenerating
    $Existing = [regex]::Replace($Existing, "(?ms)^## OmniGhost $([regex]::Escape($Version)).*?(?=^## OmniGhost |\z)", '')
    [void]$RootBuilder.Append($Existing.TrimStart())
}
    Write-TextAtomic -Path $RootChangelog -Text $RootBuilder.ToString()
}

# Internal metadata for Discord / audit (no secrets)
$AffectedProducts = @($Modules | ForEach-Object { [string]$_.name } | Select-Object -Unique)
$Meta = [ordered]@{
    previousTag = if ($PreviousTag) { $PreviousTag } else { '' }
    currentVersion = $Version
    currentTag = $Tag
    currentCommit = $Commit
    releasedAt = $PublishedAt
    releasedLocal = $ReleasedLocal
    affectedProducts = @($AffectedProducts)
    changeCount = $AllChanges.Count
    moduleCount = $Modules.Count
    semanticAnalysisUsed = $SemanticAccepted
}
$MetaPath = Join-Path $OutputDir 'release-meta.json'
Write-TextAtomic -Path $MetaPath -Text (($Meta | ConvertTo-Json -Depth 6) + [Environment]::NewLine)

$GeneratedNames = if ($SkipRootChangelog) { 'changelog.json e release-notes.md' } else { 'changelog.json, release-notes.md e CHANGELOG.md' }
Write-ChangelogLog "$GeneratedNames gerados para $Version"
Write-ChangelogLog "Alterações: $($AllChanges.Count); módulos: $($Modules.Count); produtos: $($AffectedProducts -join ', ')"
return
