











[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$Executable,

    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [System.IO.Path]::GetFullPath($ProjectDir).TrimEnd('\')

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    Write-Host "[FAIL] [OGPS-VAL] Executavel nao encontrado: $Executable"
    exit 1
}


$plaintextsJson = & (Join-Path $PSScriptRoot 'Protect-Strings.ps1') `
    -ProjectDir $ProjectDir -Command plaintexts -Configuration $Configuration -Platform $Platform
if ($LASTEXITCODE -ne 0) {
    Write-Host "[FAIL] [OGPS-VAL] Falha ao recolher os plaintexts protegidos."
    exit 1
}
$plaintexts = $plaintextsJson -join "`n" | ConvertFrom-Json

$exeBytes = [System.IO.File]::ReadAllBytes($Executable)
Write-Host "[OGPS-VAL] Executavel: $Executable ($($exeBytes.Length) bytes); candidatos: $(@($plaintexts.candidates).Count)"

function Find-Bytes {
    param([byte[]]$Haystack, [byte[]]$Needle)
    if ($Needle.Length -eq 0 -or $Haystack.Length -lt $Needle.Length) { return $false }
    $limit = $Haystack.Length - $Needle.Length
    $first = $Needle[0]
    for ($i = 0; $i -le $limit; $i++) {
        if ($Haystack[$i] -ne $first) { continue }
        $ok = $true
        for ($j = 1; $j -lt $Needle.Length; $j++) {
            if ($Haystack[$i + $j] -ne $Needle[$j]) { $ok = $false; break }
        }
        if ($ok) { return $true }
    }
    return $false
}

function Get-BytesOfString {
    param([string]$Text, [bool]$Wide)
    if ($Wide) {
        $chars = $Text.ToCharArray()
        $bytes = New-Object byte[] ($chars.Length * 2)
        for ($i = 0; $i -lt $chars.Length; $i++) {
            $code = [int]$chars[$i]
            $bytes[$i * 2] = [byte]($code -band 0xFF)
            $bytes[$i * 2 + 1] = [byte](($code -shr 8) -band 0xFF)
        }
        return $bytes
    }
    return [System.Text.Encoding]::UTF8.GetBytes($Text)
}

$binaryAllowlist = @()
$configPath = Join-Path $ProjectDir 'src\project\config\strings\protection.config.json'
if (Test-Path -LiteralPath $configPath -PathType Leaf) {
    $config = Get-Content -LiteralPath $configPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($null -ne $config.binaryAllowlist) { $binaryAllowlist = @($config.binaryAllowlist) }
}

$leaks = New-Object System.Collections.Generic.List[object]
$checked = 0
foreach ($candidate in $plaintexts.candidates) {
    if ($candidate.allowIf) { continue }
    $skip = $false
    foreach ($item in $binaryAllowlist) {
        if ($item.fp -eq $candidate.fp) { $skip = $true; break }
    }
    if ($skip) { continue }

    $checked++
    $text = [string]$candidate.text
    $utf8 = Get-BytesOfString -Text $text -Wide $false
    $utf16 = Get-BytesOfString -Text $text -Wide $true
    $foundUtf8 = Find-Bytes -Haystack $exeBytes -Needle $utf8
    $foundUtf16 = Find-Bytes -Haystack $exeBytes -Needle $utf16
    if ($foundUtf8 -or $foundUtf16) {
        $leaks.Add(@{
            fp      = $candidate.fp
            file    = $candidate.file
            length  = $candidate.length
            foundAs = $(if ($foundUtf8 -and $foundUtf16) { 'utf8+utf16' } elseif ($foundUtf8) { 'utf8' } else { 'utf16' })
        })
    }
}

if ($leaks.Count -gt 0) {
    Write-Host "[FAIL] [OGPS-VAL] $($leaks.Count) string(s) protegidas encontradas em TEXTO SIMPLES no executavel:"
    foreach ($leak in $leaks) {
        Write-Host "  [OGPS-VAL] fp=$($leak.fp) origem=$($leak.file) len=$($leak.length) encoding=$($leak.foundAs)"
    }
    Write-Host '[FAIL] [OGPS-VAL] Corre a origem listada: o literal deve estar envolvido com OGS_LIT/OGS_ONE/OGS_WIDE ou a origem do leak investigada.'
    exit 1
}

Write-Host "[OK] [OGPS-VAL] Nenhum texto protegido encontrado no executavel ($checked candidatos verificados em UTF-8 e UTF-16LE)."
exit 0
