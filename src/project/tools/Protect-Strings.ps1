# ============================================================================
# Protect-Strings.ps1 - OmniGhost String Protection System (OGSP)
#
# Build-time automation for string protection:
#   scan      - extract + classify string literals (read-only)
#   protect   - scan + manifest + history diff + key generation (build step)
#   validate  - enforcement policy check (Tier-1 violations fail Release/Publish)
#   apply     - rewrite eligible unprotected literals with OGS_LIT / OGS_PACK
#   keygen    - (re)generate omni_strprotect_key.h
#   selftest  - lexer / classification assertions on a synthetic fixture
#
# See src\project\config\strings\README.md for the full system documentation.
# ============================================================================

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [ValidateSet('scan', 'protect', 'validate', 'apply', 'keygen', 'selftest', 'plaintexts')]
    [string]$Command = 'protect',

    [string]$Configuration = 'Release',
    [string]$Platform = 'x64',

    # apply: wrap Tier-1 unprotected literals in place.
    [switch]$ApplyAutoProtect,
    # apply: also include files outside the Tier-1 areas.
    [switch]$AllFiles,
    # apply: rewrite the localization table (src\window\localization.cpp).
    [switch]$ProtectLocalization,

    # Overrides used by the scenario tests (fixture directories).
    [string]$SourceRoot = '',
    [string]$ConfigPath = '',
    [string]$ManifestPath = '',
    [string]$HistoryPath = '',
    [string]$ReportPath = '',
    [string]$GeneratedDir = '',

    # Force enforcement regardless of Configuration.
    [switch]$Enforce
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$script:LogTag = '[OGSP]'

function Write-Log {
    param([string]$Message)
    Write-Host "$script:LogTag $Message"
}

function Write-Fail {
    param([string]$Message)
    Write-Host "[FAIL] $Message"
}

# ---------------------------------------------------------------------------
# Paths + configuration.
# ---------------------------------------------------------------------------

$ProjectDir = [System.IO.Path]::GetFullPath($ProjectDir).TrimEnd('\')

function Join-UnderProject {
    param([string]$Relative)
    return Join-Path $ProjectDir $Relative
}

$StringsConfigDir = Join-UnderProject 'src\project\config\strings'
if (-not $ConfigPath) { $ConfigPath = Join-Path $StringsConfigDir 'protection.config.json' }
if (-not $ManifestPath) { $ManifestPath = Join-Path $StringsConfigDir 'strings.manifest.json' }
if (-not $HistoryPath) { $HistoryPath = Join-Path $StringsConfigDir 'strings.history.jsonl' }
if (-not $ReportPath) {
    $ReportPath = Join-Path (Join-UnderProject '.cache\string-protection') 'report.json'
}
if (-not $GeneratedDir) {
    $GeneratedDir = Join-UnderProject ".cache\generated\$Configuration\$Platform"
}

if (-not (Test-Path -LiteralPath $ConfigPath -PathType Leaf)) {
    throw "$script:LogTag protection config not found: $ConfigPath"
}
$Config = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json

# ---------------------------------------------------------------------------
# Small helpers.
# ---------------------------------------------------------------------------

$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Read-TextPreserveBom {
    param([string]$Path)
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF)
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    if ($hasBom) { $text = $text.TrimStart([char]0xFEFF) }
    return @{ Text = $text; HasBom = $hasBom }
}

function Write-TextPreserveBom {
    param([string]$Path, [string]$Text, [bool]$HasBom)
    $encoding = New-Object System.Text.UTF8Encoding($HasBom)
    [System.IO.File]::WriteAllText($Path, $Text, $encoding)
}

function Get-Sha256Hex {
    param([string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
        $hash = $sha.ComputeHash($bytes)
        return ([System.BitConverter]::ToString($hash)).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Get-AppVersion {
    $versionPath = Join-UnderProject 'src\project\config\release\version.txt'
    if (Test-Path -LiteralPath $versionPath -PathType Leaf) {
        $candidate = ([System.IO.File]::ReadAllText($versionPath)).Trim()
        if ($candidate -match '^\d+\.\d+\.\d+$') { return $candidate }
    }
    return 'unknown'
}

function Get-GitInfo {
    $info = @{ Commit = ''; Branch = '' }
    $gitDir = Join-UnderProject '.git'
    if (-not (Test-Path -LiteralPath $gitDir)) { return $info }
    try {
        $commit = & git -C $ProjectDir rev-parse HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $commit) { $info.Commit = ([string]$commit).Trim() }
        $branch = & git -C $ProjectDir rev-parse --abbrev-ref HEAD 2>$null
        if ($LASTEXITCODE -eq 0 -and $branch) { $info.Branch = ([string]$branch).Trim() }
    } catch {
        # Git present but unusable: history stays inventory-based.
    }
    return $info
}

function ConvertTo-RepoRelative {
    param([string]$FullPath)
    $full = [System.IO.Path]::GetFullPath($FullPath)
    $root = $ProjectDir.TrimEnd('\') + '\'
    if ($full.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($root.Length).Replace('\', '/')
    }
    return $full.Replace('\', '/')
}

function Test-PathPattern {
    param([string]$RelativePath, [System.Collections.IList]$Patterns)
    foreach ($pattern in $Patterns) {
        if (-not $pattern) { continue }
        if ([regex]::IsMatch($RelativePath, [string]$pattern, 'IgnoreCase')) { return $true }
    }
    return $false
}

# ---------------------------------------------------------------------------
# C++ lexer: extracts string literals with protection context.
# ---------------------------------------------------------------------------

# Ordered alternation: comments, preprocessor directives, raw strings,
# prefixed plain strings, char literals. Singleline so "." spans raw strings
# and block comments; every other construct uses explicit character classes.
$script:TokenPattern = '(?<blockcomment>/\*.*?\*/)|(?<linecomment>//[^\r\n]*)|(?<preproc>(?m)^[ \t]*#[^\r\n]*)|(?<rawstring>(?:u8|u|U|L)?R"(?<rdelim>[^ ()\\\r\n]{0,16})\(.*?\)\k<rdelim>")|(?<string>(?:u8|u|U|L)?"(?:\\.|[^"\\\r\n])*")|(?<charliteral>(?:u8|u|U|L)?''(?:\\.|[^''\\\r\n])*'')'
$script:TokenRegex = [regex]::new($script:TokenPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)

$script:SkipGroupNames = @('blockcomment', 'linecomment', 'preproc', 'rawstring', 'string', 'charliteral')

function Get-CppLiterals {
    param([string]$Path)

    $loaded = Read-TextPreserveBom -Path $Path
    $text = $loaded.Text
    if ([string]::IsNullOrWhiteSpace($text)) { return @() }

    $matches_ = $script:TokenRegex.Matches($text)
    $wrapperNames = @($Config.wrappers)

    # Ranges the wrapper scan must jump over (comments, directives, literals).
    $skipRanges = New-Object System.Collections.Generic.List[object]
    foreach ($m in $matches_) {
        foreach ($groupName in $script:SkipGroupNames) {
            $g = $m.Groups[$groupName]
            if ($g.Success) {
                $skipRanges.Add(@{ Start = $g.Index; End = $g.Index + $g.Length })
            }
        }
    }
    $skipRanges = [System.Linq.Enumerable]::OrderBy($skipRanges, [Func[object, int]]{ param($r) $r.Start })

    function Find-SkipEnd {
        param([int]$Position)
        foreach ($range in $skipRanges) {
            if ($Position -ge $range.Start -and $Position -lt $range.End) { return $range.End }
        }
        return -1
    }

    # Protected spans: wrapper identifier + '(' tracked to the matching ')',
    # ignoring parentheses inside strings/comments/directives.
    $spans = New-Object System.Collections.Generic.List[object]
    # Unsafe spans: constant-expression calls where wrapping would break compilation.
    $unsafeSpans = New-Object System.Collections.Generic.List[object]
    $unsafeNames = @('sizeof', 'static_assert', 'alignof', 'alignas', 'offsetof', '_countof', 'static_cast', 'reinterpret_cast')
    $allSpanNames = @()
    foreach ($name in $wrapperNames) { $allSpanNames += ,@('protected', [string]$name) }
    foreach ($name in $unsafeNames) { $allSpanNames += ,@('unsafe', [string]$name) }
    foreach ($entry in $allSpanNames) {
        $kind = $entry[0]; $name = $entry[1]
        foreach ($m in [regex]::Matches($text, '\b' + [regex]::Escape([string]$name) + '\s*\(')) {
            $openAt = $m.Index + $m.Value.LastIndexOf('(')
            $depth = 0
            $end = $text.Length - 1
            $i = $openAt
            while ($i -lt $text.Length) {
                $skipEnd = Find-SkipEnd -Position $i
                if ($skipEnd -ge 0) { $i = $skipEnd; continue }
                $c = $text[$i]
                if ($c -eq '(') { $depth++ }
                elseif ($c -eq ')') {
                    $depth--
                    if ($depth -eq 0) { $end = $i; break }
                }
                $i++
            }
            if ($kind -eq 'protected') {
                $spans.Add(@{ Start = $openAt; End = $end })
            } else {
                $unsafeSpans.Add(@{ Start = $openAt; End = $end })
            }
        }
    }

    # Line start offsets.
    $lineStarts = New-Object System.Collections.Generic.List[int]
    $lineStarts.Add(0)
    for ($i = 0; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq "`n") { $lineStarts.Add($i + 1) }
    }
    function Get-LineOf {
        param([int]$Offset)
        $lo = 0; $hi = $lineStarts.Count - 1
        while ($lo -lt $hi) {
            $mid = [math]::Floor(($lo + $hi + 1) / 2)
            if ($lineStarts[$mid] -le $Offset) { $lo = $mid } else { $hi = $mid - 1 }
        }
        return $lo + 1
    }

    # Collect string tokens (raw + plain), then merge adjacent literals.
    $tokens = New-Object System.Collections.Generic.List[object]
    foreach ($m in $matches_) {
        foreach ($groupName in @('rawstring', 'string')) {
            $g = $m.Groups[$groupName]
            if ($g.Success) {
                $tokens.Add(@{ Start = $g.Index; End = $g.Index + $g.Length; Raw = $g.Value })
            }
        }
    }
    if ($tokens.Count -eq 0) { return @() }
    $tokens = [System.Linq.Enumerable]::OrderBy($tokens, [Func[object, int]]{ param($t) $t.Start })

    $merged = New-Object System.Collections.Generic.List[object]
    $current = $null
    foreach ($token in $tokens) {
        if ($null -eq $current) {
            $current = @{ Start = $token.Start; End = $token.End; Raw = $token.Raw }
            continue
        }
        $gap = $text.Substring($current.End, $token.Start - $current.End)
        $isAdjacent = ($gap.Trim().Length -eq 0)
        if ($isAdjacent) {
            $current.Raw = $text.Substring($current.Start, $token.End - $current.Start)
            $current.End = $token.End
        } else {
            $merged.Add($current)
            $current = @{ Start = $token.Start; End = $token.End; Raw = $token.Raw }
        }
    }
    if ($null -ne $current) { $merged.Add($current) }

    $results = New-Object System.Collections.Generic.List[object]
    foreach ($literal in $merged) {
        $content = ConvertFrom-CppLiteral -Raw $literal.Raw
        if ($null -eq $content) { continue }
        $protected = $false
        foreach ($span in $spans) {
            if ($literal.Start -ge $span.Start -and $literal.End -le $span.End) {
                $protected = $true
                break
            }
        }
        $inUnsafe = $false
        foreach ($span in $unsafeSpans) {
            if ($literal.Start -ge $span.Start -and $literal.End -le $span.End) {
                $inUnsafe = $true
                break
            }
        }
        $lineStart = 0
        for ($i = $lineStarts.Count - 1; $i -ge 0; $i--) {
            if ($lineStarts[$i] -le $literal.Start) { $lineStart = $lineStarts[$i]; break }
        }
        $lineEnd = $text.IndexOf("`n", $literal.Start)
        if ($lineEnd -lt 0) { $lineEnd = $text.Length }
        $lineText = $text.Substring($lineStart, $lineEnd - $lineStart)

        $prevChar = ' '
        for ($i = $literal.Start - 1; $i -ge 0; $i--) {
            if (-not [char]::IsWhiteSpace($text[$i])) { $prevChar = $text[$i]; break }
        }
        $nextChar = ' '
        for ($i = $literal.End; $i -lt $text.Length; $i++) {
            if (-not [char]::IsWhiteSpace($text[$i])) { $nextChar = $text[$i]; break }
        }
        $beforeStart = [Math]::Max(0, $literal.Start - 120)
        $beforeText = $text.Substring($beforeStart, $literal.Start - $beforeStart) -replace '\s+', ' '

        $results.Add(@{
            Raw        = $literal.Raw
            Text       = $content
            Offset     = $literal.Start
            Line       = (Get-LineOf -Offset $literal.Start)
            Protected  = $protected
            InUnsafe   = $inUnsafe
            LineText   = $lineText
            BeforeText = $beforeText
            PrevChar   = [string]$prevChar
            NextChar   = [string]$nextChar
        })
    }
    return $results
}

function ConvertFrom-CppLiteral {
    param([string]$Raw)
    # Raw strings: content is the text between the delimiters.
    if ($Raw -match '(?:u8|u|U|L)?R"(?<d>[^ ()\\\r\n]{0,16})\(') {
        $delim = $Matches['d']
        $openPattern = '(?:u8|u|U|L)?R"' + [regex]::Escape($delim) + '\('
        $open = [regex]::Match($Raw, $openPattern)
        if (-not $open.Success) { return $null }
        $contentStart = $open.Index + $open.Length
        $closePattern = '\)' + [regex]::Escape($delim) + '"\s*$'
        $close = [regex]::Match($Raw, $closePattern)
        if (-not $close.Success) { return $null }
        return $Raw.Substring($contentStart, $close.Index - $contentStart)
    }

    # Plain literals: may be several adjacent segments ("a" "b" => "ab").
    $segments = [regex]::Matches($Raw, '(?:u8|u|U|L)?"(?:\\.|[^"\\\r\n])*"')
    if ($segments.Count -eq 0) { return $null }
    $builder = New-Object System.Text.StringBuilder
    foreach ($segment in $segments) {
        $body = $segment.Value
        $firstQuote = $body.IndexOf('"')
        $lastQuote = $body.LastIndexOf('"')
        if ($lastQuote -le $firstQuote) { continue }
        $body = $body.Substring($firstQuote + 1, $lastQuote - $firstQuote - 1)
        # Resolve escapes to logical characters (if/elseif chain: PS switch
        # interacts badly with manual index advancement).
        $i = 0
        while ($i -lt $body.Length) {
            $c = $body[$i]
            if ($c -eq '\' -and $i + 1 -lt $body.Length) {
                $n = $body[$i + 1]
                $i += 2
                if ($n -eq 'n') { [void]$builder.Append("`n") }
                elseif ($n -eq 't') { [void]$builder.Append("`t") }
                elseif ($n -eq 'r') { [void]$builder.Append("`r") }
                elseif ($n -eq '0') { [void]$builder.Append([char]0) }
                elseif ($n -eq '\') { [void]$builder.Append('\') }
                elseif ($n -eq '"') { [void]$builder.Append('"') }
                elseif ($n -eq "'") { [void]$builder.Append("'") }
                elseif ($n -eq '?') { [void]$builder.Append('?') }
                elseif ($n -eq 'x' -or $n -eq 'u' -or $n -eq 'U') {
                    $maxDigits = 2
                    if ($n -eq 'u') { $maxDigits = 4 }
                    if ($n -eq 'U') { $maxDigits = 8 }
                    $hex = ''
                    while ($hex.Length -lt $maxDigits -and $i -lt $body.Length -and
                           [regex]::IsMatch($body[$i], '[0-9a-fA-F]')) {
                        $hex += $body[$i]; $i++
                    }
                    if ($hex.Length -gt 0) {
                        [void]$builder.Append([char][Convert]::ToInt32($hex, 16))
                    }
                }
                else { [void]$builder.Append($n) }
                continue
            }
            [void]$builder.Append($c)
            $i++
        }
    }
    return $builder.ToString()
}

# ---------------------------------------------------------------------------
# Classification.
# ---------------------------------------------------------------------------

function Get-Classification {
    param([object]$Literal, [string]$RelativePath)

    $text = [string]$Literal.Text
    $result = @{ Category = 'none'; Sensitivity = 'low'; Eligible = $false }

    if ($text.Length -lt [int]$Config.minEligibleLength) { return $result }

    $hasWords = [regex]::IsMatch($text, '[A-Za-z]{3,}')
    $isUrl = ($text -match '^[a-zA-Z][a-zA-Z0-9+.\-]*://')
    if (-not $hasWords -and -not $isUrl) { return $result } # pure format/punctuation

    if ([regex]::IsMatch($text, '^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z0-9_\-]+)+$')) {
        $result.Category = 'locator-key' # e.g. Loc::Tr("nav.visuals") lookup keys
        return $result
    }

    if ($isUrl) {
        $result.Category = 'url'; $result.Sensitivity = 'high'; $result.Eligible = $true
        return $result
    }
    if ($text -match '^(GET|POST|PUT|DELETE|HEAD|OPTIONS|HTTP/|Upgrade:|Content-|Authorization:|User-Agent:|Host:|Accept|Sec-WebSocket|Connection:)' -or
        $text -match '^--[a-z][a-z0-9\-]*') {
        $result.Category = 'protocol'; $result.Sensitivity = 'high'; $result.Eligible = $true
        return $result
    }
    if ($text -match '(?i)(api[_-]?key|apikey|access[_-]?token|refresh[_-]?token|owner_?id|secret|password|passwd|pwd|licence[_-]?key|license[_-]?key)') {
        $result.Category = 'credential'; $result.Sensitivity = 'high'; $result.Eligible = $true
        return $result
    }
    if ($text -match '(?i)(\.dll\b|\.exe\b|\.json\b|SOFTWARE\\|%LOCALAPPDATA%|/api/|\.com\b|\.win\b|trycloudflare\.com|keyauth)') {
        $result.Category = 'path'; $result.Sensitivity = 'medium'; $result.Eligible = $true
        return $result
    }
    if (Test-PathPattern -RelativePath $RelativePath -Patterns @($Config.tier1)) {
        $result.Category = 'auth'; $result.Sensitivity = 'high'; $result.Eligible = $true
        return $result
    }
    if ([regex]::IsMatch($text, '[A-Za-z]{3,}[ /]') -or $text -match '[A-Za-z]{4,}') {
        $result.Category = 'ui'; $result.Sensitivity = 'standard'; $result.Eligible = $true
        return $result
    }
    return $result
}

# ---------------------------------------------------------------------------
# Scan all first-party sources.
# ---------------------------------------------------------------------------

function Get-SourceFiles {
    $roots = @('src')
    if ($SourceRoot) { $roots = @($SourceRoot) }
    $files = New-Object System.Collections.Generic.List[string]
    foreach ($root in $roots) {
        $rootPath = if ([System.IO.Path]::IsPathRooted($root)) { $root } else { Join-UnderProject $root }
        if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) { continue }
        foreach ($ext in @('*.h', '*.hpp', '*.cpp', '*.cc', '*.cxx', '*.inl')) {
            foreach ($file in (Get-ChildItem -LiteralPath $rootPath -Filter $ext -Recurse -File)) {
                $files.Add($file.FullName)
            }
        }
    }
    $files = [System.Linq.Enumerable]::Distinct($files)
    $files = [System.Linq.Enumerable]::OrderBy($files, [Func[string, string]]{ param($f) $f })
    return @($files)
}

function Invoke-Scan {
    $entries = New-Object System.Collections.Generic.List[object]
    $files = Get-SourceFiles
    $scannedFiles = New-Object System.Collections.Generic.List[string]
    foreach ($file in $files) {
        $relative = ConvertTo-RepoRelative -FullPath $file
        if (Test-PathPattern -RelativePath $relative -Patterns @($Config.exemptFiles)) { continue }
        $scannedFiles.Add($relative)
        $literals = Get-CppLiterals -Path $file
        foreach ($literal in $literals) {
            $classification = Get-Classification -Literal $literal -RelativePath $relative
            if (-not $classification.Eligible) { continue }
            $fp = (Get-Sha256Hex -Text $literal.Text).Substring(0, 16)
            $id = (Get-Sha256Hex -Text "$relative|$fp").Substring(0, 16)
            $statusValue = 'unprotected'
            if ($literal.Protected) { $statusValue = 'protected' }
            elseif ($literal.InUnsafe) { $statusValue = 'unsafe' }
            $entries.Add([ordered]@{
                id          = $id
                fp          = $fp
                category    = $classification.Category
                sensitivity = $classification.Sensitivity
                file        = $relative
                line        = $literal.Line
                length      = $literal.Text.Length
                status      = $statusValue
            })
        }
    }
    return @{
        Entries = $entries
        Files   = $scannedFiles
    }
}

# ---------------------------------------------------------------------------
# Manifest + history.
# ---------------------------------------------------------------------------

function Read-Manifest {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { return $null }
    try {
        return Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
    } catch {
        Write-Fail "Manifest unreadable ($ManifestPath): $($_.Exception.Message)"
        return $null
    }
}

function New-ManifestDocument {
    param([object]$Scan, [string]$Version, [hashtable]$Git)

    $entries = [System.Linq.Enumerable]::OrderBy($Scan.Entries, [Func[object, string]]{ param($e) "$($e.file):$($e.line):$($e.fp)" })

    return [ordered]@{
        schemaVersion = 1
        tool          = 'Protect-Strings.ps1'
        appVersion    = $Version
        gitCommit     = $Git.Commit
        gitBranch     = $Git.Branch
        updatedUtc    = (Get-Date).ToUniversalTime().ToString('o')
        entryCount    = @($entries).Count
        entries       = $entries
    }
}

function New-EventRecord {
    param([string]$Kind, [object]$Entry, [string]$Version, [hashtable]$Git, [string]$PrevFp = '', [string]$PrevFile = '')
    $record = [ordered]@{
        ts          = (Get-Date).ToUniversalTime().ToString('o')
        version     = $Version
        kind        = $Kind
        id          = [string]$Entry.id
        fp          = [string]$Entry.fp
        category    = [string]$Entry.category
        sensitivity = [string]$Entry.sensitivity
        file        = [string]$Entry.file
        line        = [int]$Entry.line
        length      = [int]$Entry.length
        status      = [string]$Entry.status
        gitCommit   = $Git.Commit
    }
    if ($PrevFp) { $record.prevFp = $PrevFp }
    if ($PrevFile) { $record.prevFile = $PrevFile }
    return $record
}

function Add-HistoryEvents {
    param([object]$Old, [object]$Scan, [string]$Version, [hashtable]$Git)

    $events = New-Object System.Collections.Generic.List[object]
    $oldById = @{}
    $oldFiles = New-Object 'System.Collections.Generic.HashSet[string]'
    if ($null -ne $Old -and $null -ne $Old.entries) {
        foreach ($entry in $Old.entries) {
            $oldById[[string]$entry.id] = $entry
            [void]$oldFiles.Add([string]$entry.file)
        }
    }
    $newById = @{}
    $fpCount = @{}
    foreach ($entry in $Scan.Entries) {
        $newById[[string]$entry.id] = $entry
        $key = [string]$entry.fp
        if (-not $fpCount.ContainsKey($key)) { $fpCount[$key] = 0 }
        $fpCount[$key] = [int]$fpCount[$key] + 1
    }

    $removedFps = New-Object 'System.Collections.Generic.HashSet[string]'
    if (Test-Path -LiteralPath $HistoryPath -PathType Leaf) {
        foreach ($line in (Get-Content -LiteralPath $HistoryPath -Encoding UTF8)) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try {
                $event = $line | ConvertFrom-Json
                if ($event.kind -eq 'removed' -and $event.fp) { [void]$removedFps.Add([string]$event.fp) }
            } catch { # ignore malformed history lines
            }
        }
    }

    $added = New-Object System.Collections.Generic.List[object]
    $removed = New-Object System.Collections.Generic.List[object]
    foreach ($key in $newById.Keys) { if (-not $oldById.ContainsKey($key)) { $added.Add($newById[$key]) } }
    foreach ($key in $oldById.Keys) { if (-not $newById.ContainsKey($key)) { $removed.Add($oldById[$key]) } }

    $matchedRemoved = New-Object 'System.Collections.Generic.HashSet[string]'
    $matchedAdded = New-Object 'System.Collections.Generic.HashSet[string]'

    # modified: same file + category within a small line window.
    foreach ($newEntry in $added) {
        foreach ($oldEntry in $removed) {
            if ($matchedRemoved.Contains([string]$oldEntry.id)) { continue }
            if ($oldEntry.file -ne $newEntry.file) { continue }
            if ($oldEntry.category -ne $newEntry.category) { continue }
            if ([math]::Abs([int]$oldEntry.line - [int]$newEntry.line) -le 8) {
                $events.Add((New-EventRecord -Kind 'modified' -Entry $newEntry -Version $Version -Git $Git -PrevFp ([string]$oldEntry.fp)))
                [void]$matchedRemoved.Add([string]$oldEntry.id)
                [void]$matchedAdded.Add([string]$newEntry.id)
                break
            }
        }
    }

    # moved: same fingerprint, different file.
    foreach ($newEntry in $added) {
        if ($matchedAdded.Contains([string]$newEntry.id)) { continue }
        foreach ($oldEntry in $removed) {
            if ($matchedRemoved.Contains([string]$oldEntry.id)) { continue }
            if ($oldEntry.fp -eq $newEntry.fp -and $oldEntry.file -ne $newEntry.file) {
                $events.Add((New-EventRecord -Kind 'moved' -Entry $newEntry -Version $Version -Git $Git -PrevFp ([string]$oldEntry.fp) -PrevFile ([string]$oldEntry.file)))
                [void]$matchedRemoved.Add([string]$oldEntry.id)
                [void]$matchedAdded.Add([string]$newEntry.id)
                break
            }
        }
    }

    foreach ($newEntry in $added) {
        if ($matchedAdded.Contains([string]$newEntry.id)) { continue }
        $kind = 'added'
        if ($removedFps.Contains([string]$newEntry.fp)) { $kind = 'reintroduced' }
        if (-not $oldFiles.Contains([string]$newEntry.file)) { $kind = "$kind+new-component" }
        $events.Add((New-EventRecord -Kind $kind -Entry $newEntry -Version $Version -Git $Git))
    }
    foreach ($oldEntry in $removed) {
        if ($matchedRemoved.Contains([string]$oldEntry.id)) { continue }
        $events.Add((New-EventRecord -Kind 'removed' -Entry $oldEntry -Version $Version -Git $Git))
    }

    # duplicated fingerprints (informational, current state only).
    $seenFp = @{}
    foreach ($entry in $Scan.Entries) {
        $key = [string]$entry.fp
        if (-not $seenFp.ContainsKey($key)) {
            if ([int]$fpCount[$key] -gt 1) {
                $events.Add((New-EventRecord -Kind 'duplicated' -Entry $entry -Version $Version -Git $Git))
            }
            $seenFp[$key] = $true
        }
    }

    if ($events.Count -gt 0) {
        $lines = New-Object System.Collections.Generic.List[string]
        foreach ($event in $events) {
            $lines.Add(($event | ConvertTo-Json -Compress -Depth 4))
        }
        [System.IO.File]::AppendAllLines($HistoryPath, $lines, $Utf8NoBom)
    }
    return $events
}

# ---------------------------------------------------------------------------
# Enforcement.
# ---------------------------------------------------------------------------

function Get-EnforceMode {
    $envMode = $env:OMNIGHOST_STRING_ENFORCE
    if ($envMode -eq 'tier1' -or $envMode -eq 'all' -or $envMode -eq 'off') { return $envMode }
    if ($Enforce) { return 'tier1' }
    if ($Configuration -eq 'Release' -or $Configuration -eq 'Publish') { return 'tier1' }
    return 'warn'
}

function Invoke-Enforce {
    param([object]$Scan)

    $mode = Get-EnforceMode
    if ($mode -eq 'off') {
        Write-Log 'Enforcement disabled (OMNIGHOST_STRING_ENFORCE=off).'
        return @{ Violations = @(); Mode = $mode; Failed = $false; UnprotectedCount = 0 }
    }

    $violations = New-Object System.Collections.Generic.List[object]
    $unprotectedCount = 0
    foreach ($entry in $Scan.Entries) {
        if ($entry.status -ne 'unprotected') { continue } # protected + unsafe (const-expr) contexts
        $unprotectedCount++
        $inScope = ($mode -eq 'all') -or (Test-PathPattern -RelativePath ([string]$entry.file) -Patterns @($Config.tier1))
        if (-not $inScope) { continue }
        $skip = $false
        if ($null -ne $Config.allowlist) {
            foreach ($item in @($Config.allowlist)) {
                if (-not $item -or -not $item.file) { continue }
                if (-not [regex]::IsMatch([string]$entry.file, [string]$item.file, 'IgnoreCase')) { continue }
                if ($null -ne $item.length -and [int]$item.length -ne [int]$entry.length) { continue }
                $skip = $true
                break
            }
        }
        if (-not $skip) { $violations.Add($entry) }
    }

    $failed = $false
    if ($violations.Count -gt 0) {
        $preview = New-Object System.Collections.Generic.List[string]
        $count = 0
        foreach ($v in $violations) {
            $preview.Add("  $($v.file):$($v.line) [$($v.category)]")
            $count++
            if ($count -ge 20) { $preview.Add('  ...'); break }
        }
        $message = "OGPS001: $($violations.Count) string(s) elegiveis desprotegidas (modo=$mode):`n" + ($preview -join "`n")
        if ($mode -eq 'tier1' -or $mode -eq 'all') {
            Write-Fail $message
            Write-Fail 'Envolvem com OGS_LIT()/OGS_ONE() (ou executa: Protect-Strings.ps1 -Command apply -ApplyAutoProtect) e volta a compilar.'
            $failed = $true
        } else {
            Write-Host "[WARN] $message"
        }
    } else {
        Write-Log "Sem violacoes de protecao (modo=$mode); strings desprotegidas fora de escopo: $unprotectedCount."
    }
    return @{ Violations = $violations; Mode = $mode; Failed = $failed; UnprotectedCount = $unprotectedCount }
}

# ---------------------------------------------------------------------------
# Apply: rewrite sources (Tier-1 wrapping + localization table).
# ---------------------------------------------------------------------------

function Add-StrProtectInclude {
    param([string]$Text, [string]$IncludeLine)
    if ($Text -match [regex]::Escape($IncludeLine)) { return $Text }
    $includeRegex = [regex]'(?m)^[ \t]*#[ \t]*include[ \t][^\r\n]*'
    $lastMatch = $null
    foreach ($m in $includeRegex.Matches($Text)) { $lastMatch = $m }
    if ($null -eq $lastMatch) {
        return $IncludeLine + "`r`n" + $Text
    }
    $insertAt = $lastMatch.Index + $lastMatch.Length
    return $Text.Substring(0, $insertAt) + "`r`n" + $IncludeLine + $Text.Substring($insertAt)
}

function ConvertTo-NarrowCppLiteral {
    param([string]$Content)
    # Re-encodes a decoded string as a narrow C++ literal. Non-ASCII and
    # control characters become \uXXXX escapes so the result is independent of
    # the source file encoding (the project compiles with /utf-8, and universal
    # character names are converted to UTF-8 execution bytes by MSVC).
    $builder = New-Object System.Text.StringBuilder
    foreach ($ch in $Content.ToCharArray()) {
        $code = [int]$ch
        if ($ch -eq '\') { [void]$builder.Append('\\') }
        elseif ($ch -eq '"') { [void]$builder.Append('\"') }
        elseif ($ch -eq "`n") { [void]$builder.Append('\n') }
        elseif ($ch -eq "`t") { [void]$builder.Append('\t') }
        elseif ($ch -eq "`r") { [void]$builder.Append('\r') }
        elseif ($code -eq 0) { [void]$builder.Append('\0') }
        elseif ($code -ge 32 -and $code -le 126) { [void]$builder.Append($ch) }
        else { [void]$builder.Append(('\u{0:x4}' -f $code)) }
    }
    return $builder.ToString()
}

function New-WrapExpression {
    param([object]$Literal)
    $raw = [string]$Literal.Raw
    if ($raw -match '^L"') {
        # Wide literal: store the UTF-8 form, restore UTF-16 at first use.
        return 'OGS_WIDE("' + (ConvertTo-NarrowCppLiteral -Content $Literal.Text) + '")'
    }
    if ($raw -match '^u8"') {
        # C++20 u8 literals are char8_t; the plain form has identical bytes.
        return 'OGS_LIT(' + $raw.Substring(2) + ')'
    }
    return 'OGS_LIT(' + $raw + ')'
}

function Invoke-ApplyWrapping {
    param([bool]$IncludeAll)

    $includeLine = '#include "platform/strprotect/strprotect.h"'
    $modifiedFiles = New-Object System.Collections.Generic.List[string]
    $wrappedCount = 0
    $files = Get-SourceFiles
    foreach ($file in $files) {
        $relative = ConvertTo-RepoRelative -FullPath $file
        if (Test-PathPattern -RelativePath $relative -Patterns @($Config.exemptFiles)) { continue }
        $isTier1 = Test-PathPattern -RelativePath $relative -Patterns @($Config.tier1)
        if (-not $IncludeAll -and -not $isTier1) { continue }

        $loaded = Read-TextPreserveBom -Path $file
        $text = $loaded.Text
        $literals = Get-CppLiterals -Path $file
        $wraps = New-Object System.Collections.Generic.List[object]
        foreach ($literal in $literals) {
            if ($literal.Protected) { continue }
            $classification = Get-Classification -Literal $literal -RelativePath $relative
            if (-not $classification.Eligible) { continue }
            # C++20 u"/U" literals are char16_t/char32_t: OGS cannot wrap them.
            if ($literal.Raw -match '^[uU]"') { continue }
            # Conservative contexts only: literal as a complete call argument,
            # assignment initializer, ternary branch or return/throw operand.
            $prevOk = ($literal.PrevChar -eq '(') -or ($literal.PrevChar -eq ',') -or ($literal.PrevChar -eq '=') -or ($literal.PrevChar -eq '?') -or ($literal.PrevChar -eq ':') -or ($literal.PrevChar -eq '+') -or ($literal.PrevChar -eq '/') -or ($literal.PrevChar -eq '{') -or (($literal.PrevChar -eq '<') -and ($literal.LineText -match '<<'))
            $nextOk = ($literal.NextChar -eq ')') -or ($literal.NextChar -eq ',') -or ($literal.NextChar -eq '}') -or ($literal.NextChar -eq ';') -or ($literal.NextChar -eq ':') -or ($literal.NextChar -eq '|') -or ($literal.NextChar -eq '&') -or ($literal.NextChar -eq '+') -or ($literal.NextChar -eq '/') -or ($literal.NextChar -eq '?') -or (($literal.NextChar -eq '<') -and ($literal.LineText -match '<<'))
            if (-not ($prevOk -and $nextOk)) {
                $returnContext = (($literal.NextChar -eq ';') -or ($literal.NextChar -eq '+')) -and ($literal.BeforeText -match '\b(return|co_return|throw)\s*$')
                if (-not $returnContext) { continue }
            }
            $line = $literal.LineText
            if ($literal.InUnsafe) { continue } # sizeof/static_assert/... spans
            if ($line -match '\bconstexpr\b') { continue }
            if ($line -match '__declspec' -or $line -match '\buuid\s*\(' -or $line -match '\[\[') { continue }
            if ($line -match '(wchar_t|char)\s+\w+\s*\[\s*\]\s*=') { continue } # char array init from literal
            if ($line -match '^[ \t]*#') { continue }
            $wraps.Add($literal)
        }
        if ($wraps.Count -eq 0) { continue }

        # Replace from the end so offsets stay valid.
        $wraps = @([System.Linq.Enumerable]::OrderByDescending($wraps, [Func[object, int]]{ param($w) $w.Offset }))
        $builder = $text
        foreach ($wrap in $wraps) {
            $start = $wrap.Offset
            $end = $wrap.Offset + $wrap.Raw.Length
            $replacement = New-WrapExpression -Literal $wrap
            $builder = $builder.Substring(0, $start) + $replacement + $builder.Substring($end)
            $wrappedCount++
        }
        $builder = Add-StrProtectInclude -Text $builder -IncludeLine $includeLine
        Write-TextPreserveBom -Path $file -Text $builder -HasBom $loaded.HasBom
        $modifiedFiles.Add($relative)
        Write-Log "Protecao aplicada em $relative ($($wraps.Count) strings)"
    }
    return @{ ModifiedFiles = $modifiedFiles; WrappedCount = $wrappedCount }
}

function Invoke-ApplyLocalization {
    $path = Join-UnderProject 'src\window\localization.cpp'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Write-Fail "localization.cpp nao encontrado: $path"
        return @{ Modified = $false }
    }
    $loaded = Read-TextPreserveBom -Path $path
    $text = $loaded.Text
    if ($text -match 'OGS_PACK[67]\(') {
        Write-Log 'localization.cpp ja usa OGS_PACK; nada a fazer.'
        return @{ Modified = $false }
    }

    $tableStartMarker = 'const std::unordered_map<std::string_view, Pack> kTable = {'
    $tableStart = $text.IndexOf($tableStartMarker)
    if ($tableStart -lt 0) {
        Write-Fail 'Estrutura inesperada em localization.cpp (tabela kTable nao encontrada); reescrita abortada.'
        return @{ Modified = $false }
    }
    $tableEndMatch = [regex]::Match($text.Substring($tableStart), '(?m)^[ \t]*\};')
    if (-not $tableEndMatch.Success) {
        Write-Fail 'Estrutura inesperada em localization.cpp (fim da tabela nao encontrado); reescrita abortada.'
        return @{ Modified = $false }
    }
    $tableEnd = $tableStart + $tableEndMatch.Index
    $tableText = $text.Substring($tableStart, $tableEnd - $tableStart)

    $entryRegex = [regex]'(?m)^([ \t]*)\{\s*"(?<key>(?:[^"\\]|\\.)*)"\s*,\s*\{(?<values>(?:[^{}]|\{[^{}]*\})*)\}\s*,[ \t]*$'
    $replacedCount = 0
    $newTable = $entryRegex.Replace($tableText, {
            param($m)
            $key = $m.Groups['key'].Value
            $valuesRaw = $m.Groups['values'].Value
            $valueTokens = New-Object System.Collections.Generic.List[string]
            foreach ($vm in [regex]::Matches($valuesRaw, '"(?:\\.|[^"\\])*"')) {
                $valueTokens.Add($vm.Value)
            }
            if ($valueTokens.Count -lt 6 -or $valueTokens.Count -gt 7) {
                throw "$script:LogTag entrada de localizacao com $($valueTokens.Count) linguagens ('$key'); esperado 6 ou 7. Reescrita abortada."
            }
            $macro = 'OGS_PACK6'
            if ($valueTokens.Count -eq 7) { $macro = 'OGS_PACK7' }
            $indent = $m.Groups[1].Value
            $script:ReplacedCount++
            return "$indent{$key, $macro($($valueTokens -join ', '))},"
        })
    $replacedCount = $script:ReplacedCount

    if ($replacedCount -eq 0) {
        Write-Fail 'Nenhuma entrada de localizacao foi reescrita; reescrita abortada.'
        return @{ Modified = $false }
    }
    $updated = $text.Substring(0, $tableStart) + $newTable + $text.Substring($tableEnd)

    # kTable value type.
    $updated = $updated.Replace($tableStartMarker,
                                 'const std::unordered_map<std::string_view, ogs::LocPackRef> kTable = {')

    # Remove the local Pack struct (replaced by ogs::LocPackRef lookups).
    $packStructPattern = [regex]'(?s)[ \t]*struct Pack \{[^}]*\};\r?\n\r?\n'
    $updated = $packStructPattern.Replace($updated, '', 1)

    # Replace Pick() with the LocPackRef version.
    $pickPattern = [regex]'(?s)[ \t]*const char\* Pick\(const Pack& pack\)\r?\n[ \t]*\{.*?\r?\n[ \t]*\}\r?\n'
    $newPick = @'
    const char* Pick(const ogs::LocPackRef& ref)
    {
        const auto& pack = ref.get();
        using app_settings::Language;
        switch (app_settings::config.language) {
        case Language::PT: return pack[0] && pack[0][0] ? pack[0] : pack[1];
        case Language::EN: return pack[1] && pack[1][0] ? pack[1] : pack[0];
        case Language::DE: return pack[2] && pack[2][0] ? pack[2] : pack[1];
        case Language::ES: return pack[3] && pack[3][0] ? pack[3] : pack[1];
        case Language::FR: return pack[4] && pack[4][0] ? pack[4] : pack[1];
        case Language::IT: return pack[5] && pack[5][0] ? pack[5] : pack[1];
        case Language::RU: return pack[6] && pack[6][0] ? pack[6] : pack[1];
        default:           return pack[1] ? pack[1] : pack[0];
        }
    }
'@
    $newPick = $newPick.Replace("`r`n", "`n").Replace("`n", "`r`n")
    if (-not $pickPattern.IsMatch($updated)) {
        Write-Fail 'Funcao Pick(const Pack&) nao encontrada; reescrita abortada.'
        return @{ Modified = $false }
    }
    $updated = $pickPattern.Replace($updated, $newPick, 1)

    $updated = Add-StrProtectInclude -Text $updated -IncludeLine '#include "../platform/strprotect/strprotect.h"'

    Write-TextPreserveBom -Path $path -Text $updated -HasBom $loaded.HasBom
    Write-Log "localization.cpp reescrito ($replacedCount entradas) para OGS_PACK6/OGS_PACK7."
    return @{ Modified = $true }
}

# ---------------------------------------------------------------------------
# Key generation.
# ---------------------------------------------------------------------------

function Invoke-KeyGen {
    $version = Get-AppVersion
    $publish = ($Configuration -eq 'Publish')
    $channel = 'stable'
    $inputs = @($version, $Configuration, $Platform, $channel, "publish=$publish") -join '|'
    $seedHex = Get-Sha256Hex -Text "omnighogst-strings|$inputs"

    $bytes = New-Object byte[] 32
    for ($i = 0; $i -lt 32; $i++) { $bytes[$i] = [Convert]::ToByte($seedHex.Substring($i * 2, 2), 16) }
    $keyParts = [System.UInt64[]]::new(4)
    for ($i = 0; $i -lt 4; $i++) {
        $lo = [System.UInt64]$bytes[$i * 8] -bor ([System.UInt64]$bytes[$i * 8 + 1] -shl 8) -bor ([System.UInt64]$bytes[$i * 8 + 2] -shl 16) -bor ([System.UInt64]$bytes[$i * 8 + 3] -shl 24)
        $hi = [System.UInt64]$bytes[$i * 8 + 4] -bor ([System.UInt64]$bytes[$i * 8 + 5] -shl 8) -bor ([System.UInt64]$bytes[$i * 8 + 6] -shl 16) -bor ([System.UInt64]$bytes[$i * 8 + 7] -shl 24)
        $keyParts[$i] = $lo -bor ($hi -shl 32)
    }

    $maskHex = Get-Sha256Hex -Text "ogsp-masks|$inputs"
    $masks = [System.UInt64[]]::new(4)
    for ($i = 0; $i -lt 4; $i++) {
        $lo = [Convert]::ToUInt64($maskHex.Substring($i * 16, 8), 16)
        $hi = [Convert]::ToUInt64($maskHex.Substring($i * 16 + 8, 8), 16)
        $masks[$i] = $lo -bor ($hi -shl 32)
    }

    $permHex = Get-Sha256Hex -Text "ogsp-perm|$inputs"
    $perm = New-Object 'byte[]' 8
    for ($i = 0; $i -lt 8; $i++) { $perm[$i] = [byte]$i }
    for ($i = 7; $i -ge 1; $i--) {
        $j = [Convert]::ToUInt32($permHex.Substring($i * 2, 2), 16) % [uint32]($i + 1)
        $tmp = $perm[$i]; $perm[$i] = $perm[$j]; $perm[$j] = $tmp
    }

    $saltHex = Get-Sha256Hex -Text "ogsp-salts|$inputs"
    $saltA = [Convert]::ToUInt64($saltHex.Substring(0, 16), 16)
    $saltB = [Convert]::ToUInt64($saltHex.Substring(16, 16), 16)

    $permText = ($perm | ForEach-Object { $_.ToString() }) -join ', '
    $partsText = ($keyParts | ForEach-Object { '0x' + $_.ToString('x16') + 'ull' }) -join ",`n    "
    $masksText = ($masks | ForEach-Object { '0x' + $_.ToString('x16') + 'ull' }) -join ",`n    "

    $header = @"
#pragma once

// ============================================================================
// GENERATED FILE - omni_strprotect_key.h (do not edit by hand).
// Produced by src\project\tools\Protect-Strings.ps1 (keygen) during the build.
//
// 256-bit string-protection key for version $version / $Configuration / $Platform,
// emitted split + masked (a single contiguous key constant never exists in the
// source or the binary). Rebuilt releases derive a new key; rebuilding the same
// version + configuration stays byte-identical (reproducible builds).
// ============================================================================

#include <array>
#include <cstddef>
#include <cstdint>

namespace ogs::detail::buildkeys {

inline constexpr std::array<std::uint64_t, 4> kKeyPartsMasked = {
    $partsText
};

inline constexpr std::array<std::uint64_t, 4> kKeyMasks = {
    $masksText
};

inline constexpr std::array<std::uint8_t, 8> kKeyPermutation = { $permText };

inline constexpr std::uint64_t kNonceSaltA = 0x$($saltA.ToString('x16'))ull;
inline constexpr std::uint64_t kNonceSaltB = 0x$($saltB.ToString('x16'))ull;

} // namespace ogs::detail::buildkeys
"@

    if (-not (Test-Path -LiteralPath $GeneratedDir -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $GeneratedDir | Out-Null
    }
    $keyPath = Join-Path $GeneratedDir 'omni_strprotect_key.h'
    $existing = ''
    if (Test-Path -LiteralPath $keyPath -PathType Leaf) {
        $existing = [System.IO.File]::ReadAllText($keyPath)
    }
    $changed = ($existing -ne $header)
    if ($changed) {
        [System.IO.File]::WriteAllText($keyPath, $header, $Utf8NoBom)
        Write-Log "Chave de protecao gerada: $keyPath"
    } else {
        Write-Log 'Chave de protecao inalterada (incremental).'
    }
    return @{ Path = $keyPath; Changed = $changed }
}

# ---------------------------------------------------------------------------
# Selftest: lexer + classification assertions on a synthetic fixture.
# ---------------------------------------------------------------------------

function Invoke-SelfTest {
    $failures = 0

    $fixtureDir = Join-Path ([System.IO.Path]::GetTempPath()) ("ogsp-selftest-" + [guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $fixtureDir | Out-Null
    try {
        $sample = @'
// comment with "not a string"
const char* a = "hello world";
const char* b = u8"utf8 text";
const char* c = "adjacent one" "adjacent two";
const char* d = "escaped \"quote\" inside";
const char* e = R"raw(JSON {"a":"b"})raw";
char ch = 'x'; // 'not a string'
#define MACRO "macro value"
const char* f = OGS_LIT("already protected");
/* block "comment" */
const char* g = "https://api.example.com/v1/endpoint";
const char* h = OGS_ONE("https://keyauth.win/api/1.3/");
'@
        $samplePath = Join-Path $fixtureDir 'sample.cpp'
        [System.IO.File]::WriteAllText($samplePath, $sample, $Utf8NoBom)

        $literals = Get-CppLiterals -Path $samplePath
        $texts = @($literals | ForEach-Object { $_.Text })

        $assert = {
            param([string]$Name, [bool]$Condition)
            if ($Condition) { Write-Log "  [OK] $Name" }
            else { Write-Fail "  selftest: $Name"; $script:FailCount++ }
        }
        $script:FailCount = 0

        & $assert 'literal count' ($texts.Count -eq 8)
        & $assert 'plain literal' ($texts -contains 'hello world')
        & $assert 'utf8 prefix' ($texts -contains 'utf8 text')
        & $assert 'adjacent merge' ($texts -contains 'adjacent oneadjacent two')
        & $assert 'escape decode' ($texts -contains 'escaped "quote" inside')
        & $assert 'raw string' ($texts -contains 'JSON {"a":"b"}')
        & $assert 'url literal' ($texts -contains 'https://api.example.com/v1/endpoint')
        & $assert 'no macro value' (-not ($texts -contains 'macro value'))
        & $assert 'no comment content' (-not ($texts -contains 'not a string'))

        $protected = @($literals | Where-Object { $_.Protected } | ForEach-Object { $_.Text })
        & $assert 'wrapper span detection' ($protected.Count -eq 2 -and $protected -contains 'already protected' -and $protected -contains 'https://keyauth.win/api/1.3/')

        $urlEntry = @($literals | Where-Object { $_.Text -eq 'https://api.example.com/v1/endpoint' })[0]
        $classification = Get-Classification -Literal $urlEntry -RelativePath 'src/x/y.cpp'
        & $assert 'url classified' ($classification.Category -eq 'url' -and $classification.Eligible)

        $keyEntry = @($literals | Where-Object { $_.Text -eq 'nav.visuals' })
        & $assert 'locator-key not eligible' ($keyEntry.Count -eq 0)

        $failures += $script:FailCount
    } finally {
        if (Test-Path -LiteralPath $fixtureDir) {
            Remove-Item -LiteralPath $fixtureDir -Recurse -Force -ErrorAction SilentlyContinue
        }
    }

    if ($failures -eq 0) {
        Write-Log 'selftest: todos os testes passaram.'
        return $true
    }
    Write-Fail "selftest: $failures falhas."
    return $false
}

# ---------------------------------------------------------------------------
# Report.
# ---------------------------------------------------------------------------

function Write-Report {
    param([object]$Scan, [object]$EnforceResult, [hashtable]$Extra)

    $reportDir = Split-Path -Parent $ReportPath
    if (-not (Test-Path -LiteralPath $reportDir -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
    }
    $protectedCount = 0
    foreach ($entry in $Scan.Entries) { if ($entry.status -eq 'protected') { $protectedCount++ } }
    $report = [ordered]@{
        generatedUtc     = (Get-Date).ToUniversalTime().ToString('o')
        configuration    = $Configuration
        platform         = $Platform
        appVersion       = Get-AppVersion
        scannedFiles     = $Scan.Files.Count
        literalCount     = $Scan.Entries.Count
        protectedCount   = $protectedCount
        unprotectedCount = $EnforceResult.UnprotectedCount
        violations       = @($EnforceResult.Violations | ForEach-Object { @{ file = $_.file; line = $_.line; category = $_.category } })
        enforceMode      = $EnforceResult.Mode
        failed           = $EnforceResult.Failed
    }
    foreach ($key in $Extra.Keys) { $report[$key] = $Extra[$key] }
    [System.IO.File]::WriteAllText($ReportPath, ($report | ConvertTo-Json -Depth 5), $Utf8NoBom)
}

# ---------------------------------------------------------------------------
# Main dispatch.
# ---------------------------------------------------------------------------

$script:FailCount = 0
$script:ReplacedCount = 0

switch ($Command) {
    'selftest' {
        $ok = Invoke-SelfTest
        if (-not $ok) { exit 1 }
        exit 0
    }
    'keygen' {
        Invoke-KeyGen | Out-Null
        exit 0
    }
    'scan' {
        $scan = Invoke-Scan
        $protected = 0
        foreach ($entry in $scan.Entries) { if ($entry.status -eq 'protected') { $protected++ } }
        Write-Log "scan: $($scan.Files.Count) ficheiros, $($scan.Entries.Count) strings elegiveis ($protected protegidas)."
        $enforceResult = @{ Violations = @(); Mode = 'warn'; Failed = $false; UnprotectedCount = ($scan.Entries.Count - $protected) }
        Write-Report -Scan $scan -EnforceResult $enforceResult -Extra @{}
        exit 0
    }
    'plaintexts' {
        # Emits the plaintexts of protected strings (JSON) for the post-link
        # binary validator. Strings that are substrings of any other literal in
        # the tree are flagged allowIf=true (they can legitimately appear via
        # that other literal; the validator must not fail on them).
        $scan = Invoke-Scan
        $plainEntries = New-Object System.Collections.Generic.List[object]
        foreach ($file in (Get-SourceFiles)) {
            $relative = ConvertTo-RepoRelative -FullPath $file
            if (Test-PathPattern -RelativePath $relative -Patterns @($Config.exemptFiles)) { continue }
            foreach ($literal in (Get-CppLiterals -Path $file)) {
                $classification = Get-Classification -Literal $literal -RelativePath $relative
                if (-not $classification.Eligible) { continue }
                $plainEntries.Add(@{
                    text   = [string]$literal.Text
                    prot   = [bool]$literal.Protected
                    file   = $relative
                })
            }
        }
        $candidates = New-Object System.Collections.Generic.List[object]
        foreach ($entry in $plainEntries) {
            if (-not $entry.prot) { continue }
            if ([string]$entry.text.Length -lt 8) { continue }
            $allowIf = $false
            foreach ($other in $plainEntries) {
                if ($other.text -eq $entry.text) { continue }
                if ([string]($other.text).Contains([string]$entry.text)) { $allowIf = $true; break }
            }
            $fp = (Get-Sha256Hex -Text $entry.text).Substring(0, 16)
            $candidates.Add(@{
                fp      = $fp
                file    = $entry.file
                length  = [string]($entry.text).Length
                text    = $entry.text
                allowIf = $allowIf
            })
        }
        $out = [ordered]@{
            generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
            candidates   = $candidates
        }
        Write-Output ($out | ConvertTo-Json -Depth 4 -Compress)
        exit 0
    }
    'validate' {
        $scan = Invoke-Scan
        $enforceResult = Invoke-Enforce -Scan $scan
        Write-Report -Scan $scan -EnforceResult $enforceResult -Extra @{}
        if ($enforceResult.Failed) { exit 1 }
        exit 0
    }
    'apply' {
        if ($ProtectLocalization) { Invoke-ApplyLocalization | Out-Null }
        $apply = Invoke-ApplyWrapping -IncludeAll ([bool]$AllFiles)
        Write-Log "apply: $($apply.ModifiedFiles.Count) ficheiros alterados, $($apply.WrappedCount) strings protegidas."
        $scan = Invoke-Scan
        $enforceResult = Invoke-Enforce -Scan $scan
        Write-Report -Scan $scan -EnforceResult $enforceResult -Extra @{ appliedFiles = @($apply.ModifiedFiles) }
        if ($enforceResult.Failed) { exit 1 }
        exit 0
    }
    'protect' {
        $keygen = Invoke-KeyGen
        $scan = Invoke-Scan
        $version = Get-AppVersion
        $git = Get-GitInfo
        $previous = Read-Manifest
        $manifest = New-ManifestDocument -Scan $scan -Version $version -Git $git

        $events = @()
        if ($null -ne $previous) {
            $events = @(Add-HistoryEvents -Old $previous -Scan $scan -Version $version -Git $git)
        } else {
            Write-Log 'Primeiro manifesto: o historico comeca a registar a partir da proxima comparacao.'
        }

        $json = $manifest | ConvertTo-Json -Depth 6
        [System.IO.File]::WriteAllText($ManifestPath, $json, $Utf8NoBom)

        $protected = 0
        foreach ($entry in $scan.Entries) { if ($entry.status -eq 'protected') { $protected++ } }
        $eventSummary = @{}
        foreach ($event in $events) {
            $kind = [string]$event.kind
            if (-not $eventSummary.ContainsKey($kind)) { $eventSummary[$kind] = 0 }
            $eventSummary[$kind] = [int]$eventSummary[$kind] + 1
        }
        $summaryParts = New-Object System.Collections.Generic.List[string]
        foreach ($kind in $eventSummary.Keys) { $summaryParts.Add("$kind=$($eventSummary[$kind])") }
        $summary = 'sem alteracoes'
        if ($summaryParts.Count -gt 0) { $summary = $summaryParts -join ', ' }
        Write-Log "protect: $($scan.Files.Count) ficheiros, $($scan.Entries.Count) strings ($protected protegidas). Historico: $summary"

        $enforceResult = Invoke-Enforce -Scan $scan
        Write-Report -Scan $scan -EnforceResult $enforceResult -Extra @{
            historyEvents = $eventSummary
            keyGenerated  = $keygen.Changed
            gitCommit     = $git.Commit
        }
        if ($enforceResult.Failed) { exit 1 }
        exit 0
    }
    default {
        throw "$script:LogTag comando desconhecido: $Command"
    }
}
