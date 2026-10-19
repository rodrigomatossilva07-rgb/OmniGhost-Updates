[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)][string]$ProjectDir = (Split-Path -Parent $PSScriptRoot),
    [Parameter(Mandatory=$false)][switch]$Check
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$OutputEncoding = [Console]::OutputEncoding
$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$Utf8NoBom = [Text.UTF8Encoding]::new($false, $true)
$Utf8Bom = [Text.UTF8Encoding]::new($true, $true)

function Read-SourceFile([string]$Path) {
    $bytes = [IO.File]::ReadAllBytes($Path)
    $bom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $offset = if ($bom) { 3 } else { 0 }
    [pscustomobject]@{
        Text = $Utf8NoBom.GetString($bytes, $offset, $bytes.Length - $offset)
        Bom = $bom
    }
}

function Write-SourceFile([string]$Path, [string]$Text, [bool]$Bom) {
    [IO.File]::WriteAllText($Path, $Text, $(if ($Bom) { $Utf8Bom } else { $Utf8NoBom }))
}

function Blank-Text([string]$Value) {
    $builder = [Text.StringBuilder]::new($Value.Length)
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq "`r" -or $character -eq "`n") { [void]$builder.Append($character) }
        else { [void]$builder.Append(' ') }
    }
    $builder.ToString()
}

function Trim-LineEndWhitespace([string]$Value) {
    [regex]::Replace($Value, '[ \t]+(?=\r?$)', '', [Text.RegularExpressions.RegexOptions]::Multiline)
}

function Remove-CLikeComments([string]$Text, [bool]$AllowLineComments) {
    $out = [Text.StringBuilder]::new($Text.Length)
    $length = $Text.Length
    $index = 0
    $removed = 0
    while ($index -lt $length) {
        $current = $Text[$index]
        if ($current -eq 'R' -and $index + 1 -lt $length -and $Text[$index + 1] -eq '"') {
            $open = $Text.IndexOf('(', $index + 2)
            if ($open -ge 0 -and ($open - ($index + 2)) -le 16) {
                $delimiter = $Text.Substring($index + 2, $open - ($index + 2))
                if ($delimiter.IndexOfAny([char[]]@(' ', "`t", "`r", "`n", '\', ')')) -lt 0) {
                    $closeToken = ')' + $delimiter + '"'
                    $close = $Text.IndexOf($closeToken, $open + 1, [StringComparison]::Ordinal)
                    if ($close -ge 0) {
                        $end = $close + $closeToken.Length
                        [void]$out.Append($Text, $index, $end - $index)
                        $index = $end
                        continue
                    }
                }
            }
        }
        if ($current -eq '"' -and $index + 2 -lt $length -and $Text[$index + 1] -eq '"' -and $Text[$index + 2] -eq '"') {
            $delimiterLength = 3
            while ($index + $delimiterLength -lt $length -and $Text[$index + $delimiterLength] -eq '"') { $delimiterLength++ }
            $delimiter = '"' * $delimiterLength
            $close = $Text.IndexOf($delimiter, $index + $delimiterLength, [StringComparison]::Ordinal)
            if ($close -ge 0) {
                $end = $close + $delimiterLength
                [void]$out.Append($Text, $index, $end - $index)
                $index = $end
                continue
            }
        }
        if ($current -eq '"' -or $current -eq "'") {
            $quote = $current
            $verbatim = $quote -eq '"' -and $index -gt 0 -and $Text[$index - 1] -eq '@'
            [void]$out.Append($current)
            $index++
            while ($index -lt $length) {
                $value = $Text[$index]
                [void]$out.Append($value)
                $index++
                if ($verbatim -and $value -eq '"' -and $index -lt $length -and $Text[$index] -eq '"') {
                    [void]$out.Append($Text[$index])
                    $index++
                    continue
                }
                if (-not $verbatim -and $value -eq '\' -and $index -lt $length) {
                    [void]$out.Append($Text[$index])
                    $index++
                    continue
                }
                if ($value -eq $quote) { break }
            }
            continue
        }
        if ($AllowLineComments -and $current -eq '/' -and $index + 1 -lt $length -and $Text[$index + 1] -eq '/') {
            $removed++
            [void]$out.Append('  ')
            $index += 2
            while ($index -lt $length -and $Text[$index] -ne "`r" -and $Text[$index] -ne "`n") {
                [void]$out.Append(' ')
                $index++
            }
            continue
        }
        if ($current -eq '/' -and $index + 1 -lt $length -and $Text[$index + 1] -eq '*') {
            $removed++
            [void]$out.Append('  ')
            $index += 2
            while ($index -lt $length) {
                if ($index + 1 -lt $length -and $Text[$index] -eq '*' -and $Text[$index + 1] -eq '/') {
                    [void]$out.Append('  ')
                    $index += 2
                    break
                }
                if ($Text[$index] -eq "`r" -or $Text[$index] -eq "`n") { [void]$out.Append($Text[$index]) }
                else { [void]$out.Append(' ') }
                $index++
            }
            continue
        }
        [void]$out.Append($current)
        $index++
    }
    [pscustomobject]@{ Text = (Trim-LineEndWhitespace $out.ToString()); Removed = $removed }
}

function Test-JavaScriptRegexStart([string]$Text, [int]$Index) {
    $cursor = $Index - 1
    while ($cursor -ge 0 -and [char]::IsWhiteSpace($Text[$cursor])) { $cursor-- }
    if ($cursor -lt 0) { return $true }
    if ('([{=,:;!?&|+-*%^~<>'.IndexOf($Text[$cursor]) -ge 0) { return $true }
    if ([char]::IsLetter($Text[$cursor])) {
        $end = $cursor
        while ($cursor -ge 0 -and ([char]::IsLetterOrDigit($Text[$cursor]) -or $Text[$cursor] -in @('_','$'))) { $cursor-- }
        $word = $Text.Substring($cursor + 1, $end - $cursor)
        return $word -in @('return','case','throw','delete','typeof','void','new','in','of','yield','await','else','do')
    }
    return $false
}

function Remove-JavaScriptComments([string]$Text) {
    $out = [Text.StringBuilder]::new($Text.Length)
    $length = $Text.Length
    $index = 0
    $removed = 0
    while ($index -lt $length) {
        $current = $Text[$index]
        if ($current -in @('"',"'",'`')) {
            $quote = $current
            [void]$out.Append($current)
            $index++
            while ($index -lt $length) {
                $value = $Text[$index]
                [void]$out.Append($value)
                $index++
                if ($value -eq '\' -and $index -lt $length) {
                    [void]$out.Append($Text[$index])
                    $index++
                    continue
                }
                if ($value -eq $quote) { break }
            }
            continue
        }
        if ($current -eq '/' -and $index + 1 -lt $length -and $Text[$index + 1] -eq '/') {
            $removed++
            [void]$out.Append('  ')
            $index += 2
            while ($index -lt $length -and $Text[$index] -ne "`r" -and $Text[$index] -ne "`n") {
                [void]$out.Append(' ')
                $index++
            }
            continue
        }
        if ($current -eq '/' -and $index + 1 -lt $length -and $Text[$index + 1] -eq '*') {
            $removed++
            [void]$out.Append('  ')
            $index += 2
            while ($index -lt $length) {
                if ($index + 1 -lt $length -and $Text[$index] -eq '*' -and $Text[$index + 1] -eq '/') {
                    [void]$out.Append('  ')
                    $index += 2
                    break
                }
                if ($Text[$index] -eq "`r" -or $Text[$index] -eq "`n") { [void]$out.Append($Text[$index]) }
                else { [void]$out.Append(' ') }
                $index++
            }
            continue
        }
        if ($current -eq '/' -and (Test-JavaScriptRegexStart $Text $index)) {
            [void]$out.Append($current)
            $index++
            $inClass = $false
            while ($index -lt $length) {
                $value = $Text[$index]
                [void]$out.Append($value)
                $index++
                if ($value -eq '\' -and $index -lt $length) {
                    [void]$out.Append($Text[$index])
                    $index++
                    continue
                }
                if ($value -eq '[') { $inClass = $true; continue }
                if ($value -eq ']' -and $inClass) { $inClass = $false; continue }
                if ($value -eq '/' -and -not $inClass) {
                    while ($index -lt $length -and [char]::IsLetter($Text[$index])) {
                        [void]$out.Append($Text[$index])
                        $index++
                    }
                    break
                }
            }
            continue
        }
        [void]$out.Append($current)
        $index++
    }
    [pscustomobject]@{ Text = (Trim-LineEndWhitespace $out.ToString()); Removed = $removed }
}

function Get-PowerShellComments([string]$Text, [string]$Path) {
    $tokens = $null
    $errors = $null
    [void][Management.Automation.Language.Parser]::ParseInput($Text, $Path, [ref]$tokens, [ref]$errors)
    @($tokens | Where-Object Kind -eq Comment | Sort-Object { $_.Extent.StartOffset } -Descending)
}

$git = (Get-Command git -ErrorAction Stop).Source
$tracked = @(& $git -C $ProjectDir ls-files)
if ($LASTEXITCODE -ne 0) { throw 'Não foi possível enumerar os ficheiros rastreados pelo Git.' }
$excluded = '^(third_party|libs|resources|build|\.cache|docs|src/games/Cs2/dumper/output)/'
$changes = [Collections.Generic.List[object]]::new()

$cLikeExtensions = @('.c','.cc','.cpp','.cxx','.h','.hh','.hpp','.inl','.cs','.json5')
foreach ($relative in $tracked | Where-Object { $_ -notmatch $excluded -and [IO.Path]::GetExtension($_).ToLowerInvariant() -in $cLikeExtensions }) {
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $result = Remove-CLikeComments $file.Text $true
    if ($result.Removed -gt 0) {
        $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=$result.Text; Bom=$file.Bom; Count=$result.Removed })
    }
}

foreach ($relative in $tracked | Where-Object { $_ -notmatch $excluded -and [IO.Path]::GetExtension($_).ToLowerInvariant() -eq '.css' }) {
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $result = Remove-CLikeComments $file.Text $false
    if ($result.Removed -gt 0) {
        $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=$result.Text; Bom=$file.Bom; Count=$result.Removed })
    }
}

foreach ($relative in $tracked | Where-Object { $_ -notmatch $excluded -and [IO.Path]::GetExtension($_).ToLowerInvariant() -eq '.js' }) {
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $result = Remove-JavaScriptComments $file.Text
    if ($result.Removed -gt 0) {
        $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=$result.Text; Bom=$file.Bom; Count=$result.Removed })
    }
}

foreach ($relative in $tracked | Where-Object { $_ -notmatch $excluded -and [IO.Path]::GetExtension($_).ToLowerInvariant() -in @('.ps1','.psm1','.psd1') }) {
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $comments = Get-PowerShellComments $file.Text $path
    if ($comments.Count -eq 0) { continue }
    $text = $file.Text
    foreach ($token in $comments) {
        $start = $token.Extent.StartOffset
        $length = $token.Extent.EndOffset - $start
        $text = $text.Remove($start, $length).Insert($start, (Blank-Text $text.Substring($start, $length)))
    }
    $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=(Trim-LineEndWhitespace $text); Bom=$file.Bom; Count=$comments.Count })
}

$xmlExtensions = @('.xml','.vcxproj','.filters','.props','.targets','.manifest','.slnx','.csproj')
foreach ($relative in $tracked | Where-Object { $_ -notmatch $excluded -and [IO.Path]::GetExtension($_).ToLowerInvariant() -in $xmlExtensions }) {
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $matches = [regex]::Matches($file.Text, '<!--.*?-->', [Text.RegularExpressions.RegexOptions]::Singleline)
    if ($matches.Count -eq 0) { continue }
    $text = $file.Text
    foreach ($match in @($matches | Sort-Object Index -Descending)) {
        $text = $text.Remove($match.Index, $match.Length).Insert($match.Index, (Blank-Text $match.Value))
    }
    $candidate = Trim-LineEndWhitespace $text
    try { [xml]$candidate | Out-Null } catch { throw "A remoção tornaria inválido o XML '$relative': $($_.Exception.Message)" }
    $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=$candidate; Bom=$file.Bom; Count=$matches.Count })
}

foreach ($relative in $tracked | Where-Object { $_ -notmatch $excluded -and [IO.Path]::GetExtension($_).ToLowerInvariant() -eq '.html' }) {
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $matches = [regex]::Matches($file.Text, '<!--.*?-->', [Text.RegularExpressions.RegexOptions]::Singleline)
    if ($matches.Count -eq 0) { continue }
    $text = $file.Text
    foreach ($match in @($matches | Sort-Object Index -Descending)) {
        $text = $text.Remove($match.Index, $match.Length).Insert($match.Index, (Blank-Text $match.Value))
    }
    $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=(Trim-LineEndWhitespace $text); Bom=$file.Bom; Count=$matches.Count })
}

foreach ($relative in @('.gitignore','.gitattributes','.dockerignore')) {
    if ($relative -notin $tracked) { continue }
    $path = Join-Path $ProjectDir $relative
    $file = Read-SourceFile $path
    $matches = [regex]::Matches($file.Text, '(?m)^[ \t]*#.*$')
    if ($matches.Count -eq 0) { continue }
    $text = $file.Text
    foreach ($match in @($matches | Sort-Object Index -Descending)) { $text = $text.Remove($match.Index, $match.Length) }
    $changes.Add([pscustomobject]@{ Path=$path; Relative=$relative; Original=$file.Text; Text=(Trim-LineEndWhitespace $text); Bom=$file.Bom; Count=$matches.Count })
}

$total = ($changes | Measure-Object Count -Sum).Sum
if ($null -eq $total) { $total = 0 }
if ($changes.Count -eq 0) {
    Write-Host '[Comments] Nenhum comentário removível encontrado.'
    exit 0
}

foreach ($change in $changes | Sort-Object Relative) {
    Write-Host ("[Comments] {0}: {1}" -f $change.Relative, $change.Count)
}
Write-Host ("[Comments] Total: {0} comentários em {1} ficheiros." -f $total, $changes.Count)
if ($Check) {
    Write-Host '[Comments] Modo Check: nenhum ficheiro foi alterado.'
    exit 1
}

foreach ($change in $changes) { Write-SourceFile $change.Path $change.Text $change.Bom }

try {
    foreach ($change in $changes | Where-Object { [IO.Path]::GetExtension($_.Relative).ToLowerInvariant() -in @('.ps1','.psm1','.psd1') }) {
        $written = Read-SourceFile $change.Path
        $remaining = Get-PowerShellComments $written.Text $change.Path
        if ($remaining.Count -ne 0) { throw "A validação encontrou comentários residuais em '$($change.Relative)'." }
    }
    $javascript = @($changes | Where-Object { [IO.Path]::GetExtension($_.Relative).ToLowerInvariant() -eq '.js' })
    if ($javascript.Count -gt 0) {
        $node = (Get-Command node -ErrorAction SilentlyContinue).Source
        if (-not $node) { throw 'Node.js é necessário para validar JavaScript antes de manter a limpeza.' }
        foreach ($change in $javascript) {
            $savedErrorPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            $nodeOutput = @(& $node --check $change.Path 2>&1)
            $nodeExitCode = $LASTEXITCODE
            $ErrorActionPreference = $savedErrorPreference
            if ($nodeExitCode -ne 0) {
                throw "A validação JavaScript falhou em '$($change.Relative)': $($nodeOutput -join ' ')"
            }
        }
    }
}
catch {
    foreach ($change in $changes) { Write-SourceFile $change.Path $change.Original $change.Bom }
    throw
}

Write-Host '[Comments] Limpeza concluída sem alterar dependências, gerados, dumps ou ficheiros privados.'
