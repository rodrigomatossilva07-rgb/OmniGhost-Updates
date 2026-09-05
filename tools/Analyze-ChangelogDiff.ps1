[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [Parameter(Mandatory = $true)][string]$Version,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [string]$Model = '',
    [int]$MaximumDiffCharacters = 180000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$Utf8NoBom = New-Object Text.UTF8Encoding($false)

function Log([string]$Message) { Write-Host "[OmniGhost Semantic Changelog] $Message" }
function Normalize-Path([string]$Path) { return ($Path -replace '\\', '/').Trim() }
function Is-IgnoredPath([string]$Path) {
    $p = (Normalize-Path $Path).ToLowerInvariant()
    return $p -match '(^|/)(bin|obj|build|artifacts|\.cache|\.vs|x64|logs?|temp|tmp)(/|$)' -or
           $p -match '\.(exe|dll|lib|pdb|obj|ilk|exp|zip|7z|rar|png|jpe?g|gif|ico|woff2?|ttf|log|tmp|user|suo)$' -or
           $p -match '(^|/)(\.env(?:\..*)?|secrets?\.json|credentials?\.json)$' -or
           $p -match '\.(pfx|p12|pem|key|cer|crt)$' -or
           $p -match '(^|/)(changelog\.md|release-notes\.md|changelog\.json|release-meta\.json|update\.json)$' -or
           $p -match '(^|/)version\.txt$' -or
           $p -match 'source_package_manifest\.json|source_build_metadata\.json'
}

try {
    if ($Version -notmatch '^(0|[1-9]\d*)\.[0-9]\.[0-9]$') {
        throw "Versão inválida: $Version"
    }
    if ([string]::IsNullOrWhiteSpace($env:OPENAI_API_KEY)) {
        Log 'OPENAI_API_KEY não configurada; será usado o fallback local.'
        Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue
        exit 0
    }
    if ([string]::IsNullOrWhiteSpace($Model)) {
        $Model = if ([string]::IsNullOrWhiteSpace($env:OPENAI_CHANGELOG_MODEL)) { 'gpt-5-mini' } else { $env:OPENAI_CHANGELOG_MODEL }
    }
    $git = Get-Command git -ErrorAction Stop
    Push-Location $ProjectDir
    try {
        if ((& $git.Source rev-parse --is-inside-work-tree 2>$null) -ne 'true') { throw 'Não é um repositório Git.' }
        $currentTag = "v$Version"
        $previousTag = ''
        foreach ($candidateLine in @(& $git.Source tag --merged HEAD --list 'v*' --sort=-v:refname)) {
            $candidate = ([string]$candidateLine).Trim()
            if ($candidate -and $candidate -ne $currentTag) { $previousTag = $candidate; break }
        }
        if ($previousTag) {
            $range = "$previousTag..HEAD"
        } else {
            $emptyTree = ('' | & $git.Source hash-object -t tree --stdin).Trim()
            if (-not $emptyTree) { throw 'Não foi possível criar a referência da árvore vazia.' }
            $range = "$emptyTree..HEAD"
        }
        Log "Intervalo semântico: $range"

        $logText = ((& $git.Source log $range --no-merges --pretty=format:'%H%x09%s') -join "`n")
        if ($LASTEXITCODE -ne 0) { throw 'git log falhou.' }
        $nameStatusLines = @(& $git.Source diff --name-status --find-renames $range)
        if ($LASTEXITCODE -ne 0) { throw 'git diff --name-status falhou.' }

        $changed = New-Object Collections.Generic.List[string]
        $filteredStatus = New-Object Collections.Generic.List[string]
        foreach ($lineValue in $nameStatusLines) {
            $line = [string]$lineValue
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            $parts = $line -split "`t"
            if ($parts.Count -lt 2) { continue }
            $path = Normalize-Path $parts[$parts.Count - 1]
            if (Is-IgnoredPath $path) { continue }
            $changed.Add($path)
            $filteredStatus.Add($line)
        }
        if ($changed.Count -eq 0) {
            Log 'O diff não contém ficheiros textuais relevantes; será usado o fallback.'
            Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue
            exit 0
        }

        $diffBuilder = New-Object Text.StringBuilder
        $analyzedChanged = New-Object Collections.Generic.List[string]
        foreach ($path in $changed) {
            $fileDiff = ((& $git.Source diff --no-ext-diff --unified=5 $range -- $path) -join "`n")
            if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($fileDiff)) { continue }
            if ($fileDiff -match '(?m)^Binary files .* differ$') { continue }
            $remaining = $MaximumDiffCharacters - $diffBuilder.Length
            if ($remaining -le 0) { break }
            if ($fileDiff.Length -gt $remaining) {
                # Do not expose a path as valid evidence when only an arbitrary,
                # potentially misleading fragment of its patch would be supplied.
                break
            }
            [void]$diffBuilder.AppendLine($fileDiff)
            $analyzedChanged.Add($path)
        }
        if ($analyzedChanged.Count -eq 0) { throw 'O diff textual relevante está vazio ou excede o limite configurado.' }
    } finally { Pop-Location }

    $schema = [ordered]@{
        type = 'object'; additionalProperties = $false
        required = @('summary', 'modules')
        properties = [ordered]@{
            summary = @{ type = 'string'; minLength = 1; maxLength = 500 }
            modules = [ordered]@{
                type = 'array'; maxItems = 20
                items = [ordered]@{
                    type = 'object'; additionalProperties = $false
                    required = @('id', 'name', 'changes')
                    properties = [ordered]@{
                        id = @{ type = 'string'; enum = @('core','fivem','cs2','rust','warzone','valorant','apex','other') }
                        name = @{ type = 'string'; minLength = 1; maxLength = 100 }
                        changes = [ordered]@{
                            type = 'array'; maxItems = 30
                            items = [ordered]@{
                                type = 'object'; additionalProperties = $false
                                required = @('type','text','evidence')
                                properties = [ordered]@{
                                    type = @{ type = 'string'; enum = @('added','improved','fixed','performance','compatibility','security','removed','breaking','maintenance') }
                                    text = @{ type = 'string'; minLength = 10; maxLength = 500 }
                                    evidence = @{ type = 'array'; minItems = 1; maxItems = 12; items = @{ type = 'string'; minLength = 1; maxLength = 300 } }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    $instructions = @'
You are a conservative release-note analyst. Analyse behavior shown by the real textual diff, not filenames alone.
Return only the requested structured data. Include only concrete, user-observable changes supported by the diff.
Never claim performance, stability, security, compatibility, or a fix without code evidence proving that claim.
Ignore formatting, renames, generated files, version bumps, changelog outputs, and internal refactors without observable impact.
Every change must cite one or more exact paths from CHANGED FILES. Use maintenance only when no user-facing change is justified.
Use specific descriptions. Do not describe cheats, exploitation, bypasses, or evasion; describe neutral product/UI/reliability behavior only.
'@
    $inputText = @"
RELEASE VERSION: $Version
PREVIOUS TAG: $previousTag

GIT LOG:
$logText

CHANGED FILES (all filtered name-status records):
$($filteredStatus -join "`n")

ANALYSED FILES (authoritative evidence allow-list):
$($analyzedChanged -join "`n")

FILTERED TEXTUAL DIFF:
$($diffBuilder.ToString())
"@
    $body = [ordered]@{
        model = $Model
        store = $false
        instructions = $instructions
        input = $inputText
        text = @{ format = @{ type = 'json_schema'; name = 'omnighost_release_changes'; strict = $true; schema = $schema } }
    } | ConvertTo-Json -Depth 30 -Compress
    $headers = @{ Authorization = "Bearer $($env:OPENAI_API_KEY)" }
    $response = Invoke-RestMethod -Uri 'https://api.openai.com/v1/responses' -Method Post `
        -Headers $headers -ContentType 'application/json; charset=utf-8' -Body $body -TimeoutSec 180
    $jsonText = ''
    foreach ($item in @($response.output)) {
        foreach ($content in @($item.content)) {
            if ([string]$content.type -eq 'output_text' -and $content.text) { $jsonText += [string]$content.text }
        }
    }
    if ([string]::IsNullOrWhiteSpace($jsonText)) { throw 'A API não devolveu output_text.' }
    $analysis = $jsonText | ConvertFrom-Json

    $allow = @{}
    foreach ($path in $analyzedChanged) { $allow[(Normalize-Path $path).ToLowerInvariant()] = $true }
    $validTypes = @('added','improved','fixed','performance','compatibility','security','removed','breaking','maintenance')
    $validIds = @('core','fivem','cs2','rust','warzone','valorant','apex','other')
    foreach ($module in @($analysis.modules)) {
        if ($validIds -notcontains [string]$module.id) { throw "Módulo inválido: $($module.id)" }
        foreach ($change in @($module.changes)) {
            if ($validTypes -notcontains [string]$change.type) { throw "Tipo inválido: $($change.type)" }
            if (@($change.evidence).Count -eq 0) { throw 'Alteração sem evidence.' }
            foreach ($evidence in @($change.evidence)) {
                $normalized = (Normalize-Path ([string]$evidence)).ToLowerInvariant()
                if (-not $allow.ContainsKey($normalized)) { throw "Evidence fora do diff: $evidence" }
            }
        }
    }
    $directory = Split-Path -Parent $OutputPath
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    [IO.File]::WriteAllText($OutputPath, (($analysis | ConvertTo-Json -Depth 16) + [Environment]::NewLine), $Utf8NoBom)
    Log "Análise semântica validada e guardada: $OutputPath"
}
catch {
    Remove-Item -LiteralPath $OutputPath -Force -ErrorAction SilentlyContinue
    $safe = $_.Exception.Message -replace 'sk-[A-Za-z0-9_-]+', '[redacted]'
    Write-Warning "[OmniGhost Semantic Changelog] Análise indisponível: $safe. Será usado o fallback conservador."
}
