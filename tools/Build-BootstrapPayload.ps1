[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$CoreExecutable,
    [Parameter(Mandatory=$true)][string]$VcToolsRedistDir,
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Normalize-ExistingPathArgument {
    param(
        [Parameter(Mandatory=$true)][string]$Value,
        [Parameter(Mandatory=$true)][string]$Name,
        [ValidateSet('Leaf','Container')][string]$PathType = 'Container'
    )

    # Arguments arrive through MSBuild -> cmd.exe -> PowerShell.  Do not run
    # Path.GetFullPath() over the raw command-line value: on Windows PowerShell
    # 5.1 a stray wrapper quote introduced by that chain produces the opaque
    # "Caracteres inválidos no caminho" exception. Windows file names cannot
    # contain a double quote, so trimming wrapper quotes is both safe and
    # deterministic here.
    $clean = $Value.Trim()
    while ($clean.Length -ge 2 -and
           (($clean[0] -eq [char]34 -and $clean[$clean.Length - 1] -eq [char]34) -or
            ($clean[0] -eq [char]39 -and $clean[$clean.Length - 1] -eq [char]39))) {
        $clean = $clean.Substring(1, $clean.Length - 2).Trim()
    }
    $clean = $clean.Trim([char[]]@([char]34, [char]39))

    if ([string]::IsNullOrWhiteSpace($clean)) {
        throw "$Name path is empty."
    }
    if ($clean.IndexOf([char]34) -ge 0) {
        throw "$Name contains an unexpected quote: $clean"
    }
    if (-not (Test-Path -LiteralPath $clean -PathType $PathType)) {
        throw "$Name not found ($PathType): $clean"
    }

    # Get-Item resolves the existing filesystem object without feeding the raw
    # MSBuild argument through System.IO.Path.GetFullPath().
    return (Get-Item -LiteralPath $clean -ErrorAction Stop).FullName
}

$ProjectDir = Normalize-ExistingPathArgument -Value $ProjectDir -Name 'ProjectDir' -PathType Container
$CoreExecutable = Normalize-ExistingPathArgument -Value $CoreExecutable -Name 'CoreExecutable' -PathType Leaf
$VcToolsRedistDir = Normalize-ExistingPathArgument -Value $VcToolsRedistDir -Name 'VcToolsRedistDir' -PathType Container
$Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
$Platform = ($Platform -replace '[^A-Za-z0-9_.-]', '_')

Write-Host "[BootstrapPayload] project=$ProjectDir"
Write-Host "[BootstrapPayload] redist=$VcToolsRedistDir"

$crtDir = Get-ChildItem -LiteralPath $VcToolsRedistDir -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if ($null -eq $crtDir) {
    throw "Microsoft.VC*.CRT was not found under: $VcToolsRedistDir"
}

$crtDlls = @(Get-ChildItem -LiteralPath $crtDir.FullName -File -Filter '*.dll' | Sort-Object Name)
if ($crtDlls.Count -eq 0) {
    throw "No MSVC CRT DLLs were found under: $($crtDir.FullName)"
}

$requiredNames = @('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll')
foreach ($required in $requiredNames) {
    if (-not ($crtDlls | Where-Object { $_.Name -ieq $required })) {
        throw "Required bootstrap runtime DLL is missing: $required"
    }
}

$generatedDir = Join-Path $ProjectDir ".cache\generated\bootstrap\$Configuration\$Platform"
New-Item -ItemType Directory -Path $generatedDir -Force | Out-Null
$rcOutput = Join-Path $generatedDir 'bootstrap_payload.rc2'
$headerOutput = Join-Path $generatedDir 'bootstrap_payload_manifest.h'

function Get-Sha256Hex([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '') }
    finally { $sha.Dispose(); $stream.Dispose() }
}

$entries = New-Object System.Collections.Generic.List[object]
$coreItem = Get-Item -LiteralPath $CoreExecutable
$coreHash = Get-Sha256Hex $coreItem.FullName
$entries.Add([pscustomobject]@{
    Source = $coreItem.FullName
    FileName = 'OmniGhost.exe'
    Size = [uint64]$coreItem.Length
    Sha256 = $coreHash
    IsCore = $true
})
foreach ($dll in $crtDlls) {
    $entries.Add([pscustomobject]@{
        Source = $dll.FullName
        FileName = $dll.Name
        Size = [uint64]$dll.Length
        Sha256 = Get-Sha256Hex $dll.FullName
        IsCore = $false
    })
}

# A content-derived directory prevents a running payload from blocking a future
# update and also makes stale bootstrap files harmless. Include the complete
# payload identity, not only the core EXE: an MSVC runtime servicing update must
# get a new directory even when OmniGhostCore.exe itself is byte-identical.
$identityText = (($entries | ForEach-Object {
    "$($_.FileName)|$($_.Size)|$($_.Sha256)"
}) -join "`n")
$identityBytes = [Text.Encoding]::UTF8.GetBytes($identityText)
$identityHasher = [Security.Cryptography.SHA256]::Create()
try {
    $identityHash = ([BitConverter]::ToString($identityHasher.ComputeHash($identityBytes))).Replace('-','')
}
finally {
    $identityHasher.Dispose()
}
$payloadKey = $identityHash.Substring(0, 24)

$rcLines = New-Object System.Collections.Generic.List[string]
$headerLines = New-Object System.Collections.Generic.List[string]
$headerLines.Add('#pragma once')
$headerLines.Add('#include <array>')
$headerLines.Add('#include <cstddef>')
$headerLines.Add('#include <cstdint>')
$headerLines.Add('namespace OmniGhost::BootstrapPayloadGenerated {')
$headerLines.Add('struct Entry { std::uint16_t resourceId; const wchar_t* fileName; std::uint64_t size; std::array<std::uint8_t, 32> sha256; bool core; };')
$headerLines.Add(('inline constexpr wchar_t kPayloadKey[] = L"{0}";' -f $payloadKey))
$headerLines.Add("inline constexpr std::array<Entry, $($entries.Count)> kEntries = {{")

$id = 3000
foreach ($entry in $entries) {
    $id++
    $source = $entry.Source.Replace('\','\\').Replace('"','\"')
    $name = $entry.FileName.Replace('"','\"')
    $bytes = for ($i = 0; $i -lt 64; $i += 2) { '0x' + $entry.Sha256.Substring($i, 2) }
    $coreLiteral = if ($entry.IsCore) { 'true' } else { 'false' }
    $rcLines.Add("$id RCDATA `"$source`"")
    $headerLines.Add("    Entry{$id, L`"$name`", $($entry.Size)ull, {$($bytes -join ',')}, $coreLiteral},")
}
$headerLines.Add('}};')
$headerLines.Add('inline constexpr std::size_t kEntryCount = kEntries.size();')
$headerLines.Add('} // namespace OmniGhost::BootstrapPayloadGenerated')

[IO.File]::WriteAllLines($rcOutput, $rcLines, (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllLines($headerOutput, $headerLines, (New-Object Text.UTF8Encoding($false)))

Write-Host "[BootstrapPayload] core=$CoreExecutable"
Write-Host "[BootstrapPayload] key=$payloadKey"
Write-Host "[BootstrapPayload] VC runtime files=$($crtDlls.Count): $($crtDlls.Name -join ', ')"
Write-Host "[BootstrapPayload] RC=$rcOutput"
Write-Host "[BootstrapPayload] header=$headerOutput"
