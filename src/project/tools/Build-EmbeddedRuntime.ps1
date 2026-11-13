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
. (Join-Path $PSScriptRoot 'ReleaseOutput.ps1')
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
$Platform = ($Platform -replace '[^A-Za-z0-9_.-]', '_')
$privateStatic = $Configuration -ieq 'PrivateStatic'
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


$libsDir = Join-Path $ProjectDir 'src\project\libs'
$libsEmbedded = @()
if (Test-Path -LiteralPath $libsDir -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $libsDir -File -Filter '*.dll') {
        Add-RuntimeFile $file.FullName ("libs/" + $file.Name)
        $libsEmbedded += $file.Name
    }
}


$dmaBin = Join-Path $ProjectDir 'src\project\third_party\dma_stack\bin'
$requiredDmaRuntime = @(
    'dbghelp.dll',
    'FTD3XX.dll',
    'FTD3XXWU.dll',
    'leechcore.dll',
    'leechcore_device_hvsavedstate.dll',
    'leechcore_device_rawtcp.dll',
    'leechcore_driver.dll',
    'symsrv.dll',
    'tinylz4.dll',
    'vmm.dll',
    'vmmyara.dll'
)
if ($Configuration -ieq 'Publish') {
    $missingDmaRuntime = @($requiredDmaRuntime | Where-Object {
        -not (Test-Path -LiteralPath (Join-Path $dmaBin $_) -PathType Leaf)
    })
    if ($missingDmaRuntime.Count -gt 0) {
        throw "Canonical DMA runtime is incomplete for Publish: $($missingDmaRuntime -join ', '). Restore src/project/third_party/dma_stack/bin from the source revision."
    }
}
$dmaEmbedded = @()
if (Test-Path -LiteralPath $dmaBin -PathType Container) {
    foreach ($file in Get-ChildItem -LiteralPath $dmaBin -File -Filter '*.dll') {
        Add-RuntimeFile $file.FullName ("libs/" + $file.Name)
        $dmaEmbedded += $file.Name
    }
}
$hasFtdi = @(($libsEmbedded + $dmaEmbedded) |
    Where-Object { $_ -ieq 'FTD3XX.dll' -or $_ -ieq 'FTD3XXWU.dll' } |
    Sort-Object -Unique)
if (-not $hasFtdi) {
    Write-Warning "[EmbeddedRuntime] FTD3XX.dll / FTD3XXWU.dll not found; FPGA open will fail until Publish includes one."
} else {
    Write-Host "[EmbeddedRuntime] FTDI: $($hasFtdi -join ', ')"
}







if (-not [string]::IsNullOrWhiteSpace($VcToolsRedistDir) -and
    (Test-Path -LiteralPath $VcToolsRedistDir -PathType Container)) {
    $crt = Get-ChildItem -LiteralPath $VcToolsRedistDir -Directory -Filter 'Microsoft.VC*.CRT' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($null -ne $crt) {
        $vcruntime = Join-Path $crt.FullName 'vcruntime140.dll'
        if (-not (Test-Path -LiteralPath $vcruntime -PathType Leaf)) {
            throw 'vcruntime140.dll x64 nao foi encontrado no redistributable do MSVC.'
        }
        Add-RuntimeFile $vcruntime 'libs/vcruntime140.dll'
    }
}
$cloudflared = Join-Path $ProjectDir 'src\runtime\cloudflared\cloudflared.exe'
& (Join-Path $PSScriptRoot 'Update-Cloudflared.ps1') -Ensure -ProjectDir $ProjectDir
if ($LASTEXITCODE -ne 0) {
    throw 'cloudflared.exe nao pode ser verificado/atualizado; consulta logs\cloudflared-update-*.log.'
}
if (-not (Test-Path -LiteralPath $cloudflared -PathType Leaf)) {
    throw 'cloudflared.exe canonico em src\runtime\cloudflared esta em falta.'
}
Add-RuntimeFile $cloudflared 'libs/cloudflared.exe'





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
foreach ($file in Get-ChildItem -LiteralPath (Join-Path $ProjectDir 'src\project\libs') -File -Filter '*.dll') {
    $relativePath = "libs/$($file.Name)"
    $key = $relativePath.ToLowerInvariant()
    if ($files.ContainsKey($key)) {
        $embeddedList += $relativePath
    } else {
        $skippedList += "$relativePath (from libs/)"
    }
}

if ($files.ContainsKey('libs/cloudflared.exe')) { $embeddedList += 'libs/cloudflared.exe' }
if ($files.ContainsKey('libs/vcruntime140.dll')) { $embeddedList += 'libs/vcruntime140.dll' }
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
$headerLines.Add('inline constexpr std::size_t kEntryCount = kEntries.size();')
$headerLines.Add('} // namespace OmniGhost::EmbeddedRuntimeGenerated')

[IO.File]::WriteAllLines($RcOutput, $rcLines, (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllLines($HeaderOutput, $headerLines, (New-Object Text.UTF8Encoding($false)))
Write-Host "[EmbeddedRuntime] $($ordered.Count) ficheiros; tamanho=$(Format-OmniBytes $total); configuração=$Configuration"
if ($skippedList.Count -gt 0) { Write-Host "[EmbeddedRuntime] Ignorados: $($skippedList -join '; ')" }
if ($env:OMNIGHOST_VERBOSE_BUILD -eq '1') {
    Write-Host "[EmbeddedRuntime] Ficheiros: $($ordered.Relative -join ', ')"
    Write-Host "[EmbeddedRuntime] RC: $RcOutput"
    Write-Host "[EmbeddedRuntime] Manifesto: $HeaderOutput"
}
$generatorLock.Dispose()
