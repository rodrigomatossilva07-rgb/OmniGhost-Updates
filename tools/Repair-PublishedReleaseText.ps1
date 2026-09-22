[CmdletBinding()]
param(
    [string]$ProjectDir,
    [string]$Version,
    [string]$Repository,
    [switch]$Apply
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ([string]::IsNullOrWhiteSpace($ProjectDir)) {
    if ([string]::IsNullOrWhiteSpace($PSScriptRoot)) {
        throw 'Não foi possível determinar a pasta do projeto.'
    }
    $ProjectDir = Split-Path -Parent $PSScriptRoot
}

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$Utf8NoBom = New-Object System.Text.UTF8Encoding -ArgumentList $false
$Utf8Strict = New-Object System.Text.UTF8Encoding -ArgumentList @($false, $true)
$MojibakePattern = '(?:\u00C3[\u00A0-\u00BF\u0192\u201A]|\u00C2[\u00A0\u00AB\u00BB]|\u00E2\u20AC(?:[\u201C\u201D\u00A2])?|\u00F0\u0178|\uFFFD)'

function Write-RepairLog {
    param([string]$Message)
    Write-Host "[OmniGhost Release Repair] $Message"
}

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

    try {
        return (Read-TextUtf8 -Path $Path) | ConvertFrom-Json
    }
    catch {
        throw "JSON inválido em '$Path': $($_.Exception.Message)"
    }
}

function Write-TextUtf8 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Text
    )

    $Directory = Split-Path -Parent $Path
    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    [System.IO.File]::WriteAllText($Path, $Text, $Utf8NoBom)
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
        default { return ':small_blue_diamond:' }
    }
}

function Find-Gh {
    $Command = Get-Command gh.exe -ErrorAction SilentlyContinue
    if (-not $Command) {
        $Command = Get-Command gh -ErrorAction SilentlyContinue
    }
    if ($Command) {
        return $Command.Source
    }

    $Candidates = @()

    $ProgramFiles = [Environment]::GetEnvironmentVariable('ProgramFiles')
    if (-not [string]::IsNullOrWhiteSpace($ProgramFiles)) {
        $Candidates += Join-Path $ProgramFiles 'GitHub CLI\gh.exe'
    }

    $ProgramFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    if (-not [string]::IsNullOrWhiteSpace($ProgramFilesX86)) {
        $Candidates += Join-Path $ProgramFilesX86 'GitHub CLI\gh.exe'
    }

    $LocalAppData = [Environment]::GetEnvironmentVariable('LOCALAPPDATA')
    if (-not [string]::IsNullOrWhiteSpace($LocalAppData)) {
        $Candidates += Join-Path $LocalAppData 'Programs\GitHub CLI\gh.exe'
        $Candidates += Join-Path $LocalAppData 'Microsoft\WinGet\Links\gh.exe'
    }

    foreach ($Candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace([string]$Candidate) -and
            (Test-Path -LiteralPath $Candidate -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($Candidate)
        }
    }

    return $null
}

function Invoke-Gh {
    param(
        [Parameter(Mandatory = $true)][string]$Gh,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $Output = (& $Gh @Arguments 2>&1 | Out-String).Trim()
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $Output
    }
}

$VersionFile = Join-Path $ProjectDir 'config\release\version.txt'
$HistoryPath = Join-Path $ProjectDir 'artifacts\changelog-history.json'
$ConfigPath = Join-Path $ProjectDir 'config\release\release-publish.json'
$OutputDir = Join-Path $ProjectDir 'artifacts\repair-published-release'

if (-not (Test-Path -LiteralPath $VersionFile -PathType Leaf)) {
    throw "version.txt não encontrado: $VersionFile"
}
if (-not (Test-Path -LiteralPath $HistoryPath -PathType Leaf)) {
    throw "Histórico não encontrado: $HistoryPath"
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    $Version = (Read-TextUtf8 -Path $VersionFile).Trim()
}
if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
    throw "Versão inválida: '$Version'."
}
$Tag = "v$Version"

if ([string]::IsNullOrWhiteSpace($Repository)) {
    if (Test-Path -LiteralPath $ConfigPath -PathType Leaf) {
        $Config = Read-JsonUtf8 -Path $ConfigPath
        $Repository = [string](Get-PropertyValue -Object $Config -Name 'repository' -Default '')
    }
    if ([string]::IsNullOrWhiteSpace($Repository)) {
        $Repository = 'rodrigomatossilva07-rgb/OmniGhost-Updates'
    }
}

$History = Read-JsonUtf8 -Path $HistoryPath
$Release = @($History.releases | Where-Object { [string]$_.version -eq $Version }) | Select-Object -First 1
if (-not $Release) {
    throw "A versão $Version não existe em artifacts\changelog-history.json."
}
if ([string]$Release.tag -ne $Tag) {
    throw "A tag no changelog não corresponde à versão: '$($Release.tag)' em vez de '$Tag'."
}

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$ChangelogOutput = Join-Path $OutputDir 'changelog.json'
$NotesOutput = Join-Path $OutputDir 'release-notes.md'

$HistoryJson = ($History | ConvertTo-Json -Depth 16) + [Environment]::NewLine
Write-TextUtf8 -Path $ChangelogOutput -Text $HistoryJson

$Markdown = New-Object System.Text.StringBuilder
[void]$Markdown.AppendLine("# $($Release.title)")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("> $($Release.summary)")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("| Versão | Canal | Tag |")
[void]$Markdown.AppendLine("|:--|:--|:--|")
[void]$Markdown.AppendLine("| ``$Version`` | ``$($History.channel)`` | ``$Tag`` |")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("---")
[void]$Markdown.AppendLine()

foreach ($Module in @($Release.modules)) {
    [void]$Markdown.AppendLine("## $($Module.name)")
    [void]$Markdown.AppendLine()

    foreach ($Type in @('breaking', 'added', 'improved', 'performance', 'compatibility', 'fixed', 'security', 'removed')) {
        $Changes = @($Module.changes | Where-Object { [string]$_.type -eq $Type })
        if ($Changes.Count -eq 0) { continue }

        $Icon = Get-ChangeTypeIcon -Type $Type
        $Label = Get-ChangeTypeLabel -Type $Type
        [void]$Markdown.AppendLine("### $Icon $Label")
        [void]$Markdown.AppendLine()
        foreach ($Change in $Changes) {
            [void]$Markdown.AppendLine("- $($Change.text)")
        }
        [void]$Markdown.AppendLine()
    }
}

[void]$Markdown.AppendLine("---")
[void]$Markdown.AppendLine()
[void]$Markdown.AppendLine("_Histórico completo disponível na página **Atualizações** do OmniGhost._")
[void]$Markdown.AppendLine()

Write-TextUtf8 -Path $NotesOutput -Text $Markdown.ToString()

$Combined = (Read-TextUtf8 -Path $ChangelogOutput) + (Read-TextUtf8 -Path $NotesOutput)
$MojibakeMatch = [regex]::Match($Combined, $MojibakePattern)
if ($MojibakeMatch.Success) {
    throw "Ainda existem caracteres corrompidos nos ficheiros reparados: '$($MojibakeMatch.Value)'."
}

Write-RepairLog "Pré-visualização criada em: $OutputDir"
Write-RepairLog "Versão: $Version; tag: $Tag"
Write-RepairLog "Repositório: $Repository"

if (-not $Apply) {
    Write-RepairLog 'Nenhuma alteração remota foi feita.'
    Write-RepairLog 'Depois de rever os ficheiros, executa novamente com -Apply.'
    return
}

$Gh = Find-Gh
if (-not $Gh) {
    throw 'GitHub CLI não encontrada.'
}

$Auth = Invoke-Gh -Gh $Gh -Arguments @('auth', 'status')
if ($Auth.ExitCode -ne 0) {
    throw "GitHub CLI sem autenticação válida: $($Auth.Output)"
}

$View = Invoke-Gh -Gh $Gh -Arguments @(
    'release', 'view', $Tag,
    '--repo', $Repository,
    '--json', 'tagName,name,url,assets'
)
if ($View.ExitCode -ne 0) {
    throw "A Release ${Tag} não foi encontrada: $($View.Output)"
}

Write-RepairLog 'A atualizar a descrição da Release...'
$Edit = Invoke-Gh -Gh $Gh -Arguments @(
    'release', 'edit', $Tag,
    '--repo', $Repository,
    '--notes-file', $NotesOutput
)
if ($Edit.ExitCode -ne 0) {
    throw "Não foi possível atualizar a descrição da Release ${Tag}: $($Edit.Output)"
}

Write-RepairLog 'A substituir changelog.json e release-notes.md...'
$Upload = Invoke-Gh -Gh $Gh -Arguments @(
    'release', 'upload', $Tag,
    $ChangelogOutput,
    $NotesOutput,
    '--repo', $Repository,
    '--clobber'
)
if ($Upload.ExitCode -ne 0) {
    throw "Não foi possível substituir os assets da Release ${Tag}: $($Upload.Output)"
}

$FinalView = Invoke-Gh -Gh $Gh -Arguments @(
    'release', 'view', $Tag,
    '--repo', $Repository,
    '--json', 'tagName,name,url,assets'
)
if ($FinalView.ExitCode -ne 0) {
    throw "Não foi possível validar a Release ${Tag}: $($FinalView.Output)"
}

$Remote = $FinalView.Output | ConvertFrom-Json
$AssetNames = @($Remote.assets | ForEach-Object { [string]$_.name })
foreach ($Required in @('changelog.json', 'release-notes.md')) {
    if ($AssetNames -notcontains $Required) {
        throw "O asset '$Required' não ficou disponível na Release ${Tag}."
    }
}

Write-RepairLog 'Descrição e assets corrigidos com sucesso.'
Write-RepairLog "URL: $($Remote.url)"
