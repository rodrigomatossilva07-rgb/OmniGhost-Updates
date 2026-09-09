[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectDir,
    [string]$VcToolsRedistDir = '',
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
$Platform = ($Platform -replace '[^A-Za-z0-9_.-]', '_')
$privateStatic = $Configuration -ieq 'PrivateStatic' -or $Configuration -ieq 'Publish'
$GeneratedDir = Join-Path $ProjectDir ".cache\generated\$Configuration\$Platform"
$RcOutput = Join-Path $GeneratedDir 'embedded_runtime.rc2'
$HeaderOutput = Join-Path $GeneratedDir 'embedded_runtime_manifest.h'
New-Item -ItemType Directory -Path $GeneratedDir -Force | Out-Null

function Acquire-GeneratorLock([string]$Name) {
    $lockPath = Join-Path $GeneratedDir (".$Name.lock")
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { return [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
        catch [IO.IOException] { Start-Sleep -Milliseconds 100 }
    }
    throw "Timed out waiting for embedded generator lock: $Name"
}
$generatorLock = Acquire-GeneratorLock 'runtime'

$files = @{}
function Get-Sha256Hex([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','') }
        finally { $sha.Dispose() }
    }
    finally { $stream.Dispose() }
}
function Add-RuntimeFile([string]$Source, [string]$Relative) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) { return }
    $relativePath = $Relative.Replace('\','/').TrimStart('/')
    if ([string]::IsNullOrWhiteSpace($relativePath) -or $relativePath.Contains('../')) {
        throw "Invalid embedded runtime path: $Relative"
    }
    # MemProcFS supports pdbcrust as its local PDB backend. Shipping the private
    # Microsoft dbghelp/symsrv pair as well only duplicates the same optional
    # capability; crash dumps use the Windows System32 dbghelp explicitly.
    if ($relativePath -ieq 'libs/dbghelp.dll') { return }
    if ($relativePath -ieq 'libs/symsrv.dll') { return }
    # In the explicitly private prototype these two libraries are linked from
    # their preserved upstream source trees. Do not leave dead DLL resources in
    # the PE and do not materialize them at runtime.
    if ($privateStatic -and $relativePath -ieq 'libs/vmm.dll') { return }
    if ($privateStatic -and $relativePath -ieq 'libs/leechcore.dll') { return }
    $files[$relativePath.ToLowerInvariant()] = [pscustomobject]@{
        Source = [IO.Path]::GetFullPath($Source)
        Relative = $relativePath
    }
}
function Add-RuntimeTree([string]$Root, [string]$Prefix, [scriptblock]$Include = $null) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    foreach ($file in Get-ChildItem -LiteralPath $rootFull -Recurse -File | Sort-Object FullName) {
        $relative = $file.FullName.Substring($rootFull.Length).TrimStart('\','/')
        if ($null -ne $Include -and -not (& $Include $file $relative)) { continue }
        Add-RuntimeFile $file.FullName ((Join-Path $Prefix $relative).Replace('\','/'))
    }
}

# Primary source: ProjectDir\libs\*.dll (includes FTD3XX.dll / FTD3XXWU.dll when present)
$libsDir = Join-Path $ProjectDir 'libs'
$libsEmbedded = @()
if (Test-Path -LiteralPath $libsDir -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $libsDir -File -Filter '*.dll') {
        Add-RuntimeFile $file.FullName ("libs/" + $file.Name)
        $libsEmbedded += $file.Name
    }
}
Write-Host "[EmbeddedRuntime] from libs\: $($libsEmbedded.Count) files - $($libsEmbedded -join ', ')"

# Secondary: third_party\dma_stack\bin\*.dll (also mapped to libs/<name>)
$dmaBin = Join-Path $ProjectDir 'third_party\dma_stack\bin'
$dmaEmbedded = @()
if (Test-Path -LiteralPath $dmaBin -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $dmaBin -File -Filter '*.dll') {
        Add-RuntimeFile $file.FullName ("libs/" + $file.Name)
        $dmaEmbedded += $file.Name
    }
}
if ($dmaEmbedded.Count -gt 0) {
    Write-Host "[EmbeddedRuntime] from dma_stack\bin: $($dmaEmbedded.Count) files - $($dmaEmbedded -join ', ')"
}

$hasFtdi = ($libsEmbedded + $dmaEmbedded) | Where-Object { $_ -ieq 'FTD3XX.dll' -or $_ -ieq 'FTD3XXWU.dll' }
if (-not $hasFtdi) {
    Write-Warning "[EmbeddedRuntime] FTD3XX.dll / FTD3XXWU.dll not found in libs\ or third_party\dma_stack\bin — FPGA open will fail until one is present at Publish time."
} else {
    Write-Host "[EmbeddedRuntime] FTDI bridge will be embedded: $($hasFtdi -join ', ')"
}
# The current pinned vmm.dll, leechcore.dll and pdbcrust.dll import only
# VCRUNTIME140.dll from the private Microsoft VC runtime. Do not embed the
# complete redistributable directory: msvcp/concrt/vccorlib and the auxiliary
# vcruntime variants are not in the dependency closure of any bundled binary.
# Keep this allow-list deliberately explicit so a dependency update cannot
# silently grow the customer runtime again; Validate-Project.ps1/import-table
# checks must be updated together when the pinned binaries change.
if (-not [string]::IsNullOrWhiteSpace($VcToolsRedistDir) -and
    (Test-Path -LiteralPath $VcToolsRedistDir -PathType Container)) {
    $crt = Get-ChildItem -LiteralPath $VcToolsRedistDir -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $crt) {
        $vcruntime = Join-Path $crt.FullName 'vcruntime140.dll'
        if (-not (Test-Path -LiteralPath $vcruntime -PathType Leaf)) {
            throw 'vcruntime140.dll x64 não foi encontrado no redistributable do MSVC.'
        }
        Add-RuntimeFile $vcruntime 'libs/vcruntime140.dll'
    }
}
$cloudflared = Join-Path $ProjectDir 'src\runtime\cloudflared\cloudflared.exe'
if (-not (Test-Path -LiteralPath $cloudflared -PathType Leaf)) {
    throw 'cloudflared.exe canónico em src\runtime\cloudflared está em falta.'
}
$expectedCloudflaredSha256 = 'C29EEE2B121F5436A642EED69FD9767DA7E7B8C510FA50AAA130337F931357B5'
if ((Get-Sha256Hex $cloudflared) -cne $expectedCloudflaredSha256) {
    throw 'cloudflared.exe não corresponde ao binário Cloudflare 2026.8.2 aprovado.'
}
Add-RuntimeFile $cloudflared 'libs/cloudflared.exe'
# Only native DLLs and the selected pdbcrust symbol backend are materialized.
# OmniGhost builds VMM with -disable-infodb, so info.db is not part of the bundle.
# Windows cannot load ordinary DLL imports or execute cloudflared from RCDATA.
# Product data, plug-in scripts and UI resources are deliberately not extracted.

$ordered = @($files.Values | Sort-Object Relative)
if ($ordered.Count -eq 0) { throw 'No files were selected for the embedded runtime bundle.' }

$rcLines = New-Object System.Collections.Generic.List[string]
$headerLines = New-Object System.Collections.Generic.List[string]
$headerLines.Add('#pragma once')
$headerLines.Add('#include <array>')
$headerLines.Add('#include <cstddef>')
$headerLines.Add('#include <cstdint>')
$headerLines.Add('namespace OmniGhost::EmbeddedRuntimeGenerated {')
$headerLines.Add('struct Entry { std::uint16_t resourceId; const wchar_t* relativePath; std::uint64_t size; std::array<std::uint8_t, 32> sha256; };')
$headerLines.Add("inline constexpr std::array<Entry, $($ordered.Count)> kEntries = {{")

$id = 1000
$total = [uint64]0
$embeddedList = @()
$skippedList = @()
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $ProjectDir 'libs') -File -Filter '*.dll') {
    $relativePath = "libs/$($file.Name)"
    $key = $relativePath.ToLowerInvariant()
    if ($files.ContainsKey($key)) {
        $embeddedList += $relativePath
    } else {
        $skippedList += "$relativePath (from libs/)"
    }
}
# Add cloudflared and vcruntime to embedded list
if ($files.ContainsKey('libs/cloudflared.exe')) { $embeddedList += 'libs/cloudflared.exe' }
if ($files.ContainsKey('libs/vcruntime140.dll')) { $embeddedList += 'libs/vcruntime140.dll' }
# Explicit skips
$skippedList += 'libs/dbghelp.dll (explicit skip)'
$skippedList += 'libs/symsrv.dll (explicit skip)'
if ($privateStatic) {
    $skippedList += 'libs/vmm.dll (privateStatic skip)'
    $skippedList += 'libs/leechcore.dll (privateStatic skip)'
}

foreach ($entry in $ordered) {
    $id++
    $item = Get-Item -LiteralPath $entry.Source
    $hash = Get-Sha256Hex $entry.Source
    $bytes = for ($i=0; $i -lt 64; $i+=2) { '0x' + $hash.Substring($i,2) }
    $escapedSource = $entry.Source.Replace('\','\\').Replace('"','\"')
    $escapedRelative = $entry.Relative.Replace('\','/').Replace('"','\"')
    $rcLines.Add("$id RCDATA `"$escapedSource`"")
    $headerLines.Add("    Entry{$id, L`"$escapedRelative`", $($item.Length)ull, {$($bytes -join ',')}} ,")
    $total += [uint64]$item.Length
}
$headerLines.Add('}};')
$headerLines.Add('} // namespace OmniGhost::EmbeddedRuntimeGenerated')

[IO.File]::WriteAllLines($RcOutput, $rcLines, (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllLines($HeaderOutput, $headerLines, (New-Object Text.UTF8Encoding($false)))
Write-Host "[EmbeddedRuntime] from libs: $($embeddedList.Count) files - $($embeddedList -join ', ')"
Write-Host "[EmbeddedRuntime] embedded count=$($ordered.Count) total_bytes=$total"
Write-Host "[EmbeddedRuntime] skipped: $($skippedList -join '; ')"
Write-Host "[EmbeddedRuntime] configuration=$Configuration private_static=$privateStatic"
Write-Host "[EmbeddedRuntime] RC include: $RcOutput"
Write-Host "[EmbeddedRuntime] manifest: $HeaderOutput"
$generatorLock.Dispose()
