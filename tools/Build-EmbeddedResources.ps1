[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
$Platform = ($Platform -replace '[^A-Za-z0-9_.-]', '_')
$configurationGeneratedDir = Join-Path $ProjectDir ".cache\generated\$Configuration\$Platform"
$manifestPath = Join-Path $ProjectDir 'resources\embedded-resources.json'
$generatedDir = Join-Path $configurationGeneratedDir 'resources'
$catalogPath = Join-Path $configurationGeneratedDir 'embedded_resource_catalog.h'
$rcPath = Join-Path $configurationGeneratedDir 'embedded_resources.rc2'

function Acquire-GeneratorLock([string]$Name) {
    $lockDirectory = $configurationGeneratedDir
    New-Item -ItemType Directory -Path $lockDirectory -Force | Out-Null
    $lockPath = Join-Path $lockDirectory (".$Name.lock")
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { return [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
        catch [IO.IOException] { Start-Sleep -Milliseconds 100 }
    }
    throw "Timed out waiting for embedded generator lock: $Name"
}
$generatorLock = Acquire-GeneratorLock 'resources'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw "Embedded resource manifest not found: $manifestPath" }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.schemaVersion -ne 1 -or $null -eq $manifest.resources) { throw 'Unsupported embedded resource manifest.' }
New-Item -ItemType Directory -Path $generatedDir -Force | Out-Null

function Put-U16([Collections.Generic.List[byte]]$Out, [uint16]$Value) {
    $Out.Add([byte]($Value -band 0xff)); $Out.Add([byte](($Value -shr 8) -band 0xff))
}
function Put-U32([Collections.Generic.List[byte]]$Out, [uint32]$Value) {
    0..3 | ForEach-Object { $Out.Add([byte](($Value -shr ($_ * 8)) -band 0xff)) }
}
function Compress-PackBits([byte[]]$InputBytes) {
    $out = New-Object 'Collections.Generic.List[byte]'
    $i = 0
    while ($i -lt $InputBytes.Length) {
        $run = 1
        while (($i + $run) -lt $InputBytes.Length -and $run -lt 130 -and $InputBytes[$i + $run] -eq $InputBytes[$i]) { $run++ }
        if ($run -ge 3) {
            $out.Add([byte](0x80 -bor ($run - 3))); $out.Add($InputBytes[$i]); $i += $run; continue
        }
        $start = $i; $i++
        while ($i -lt $InputBytes.Length -and ($i - $start) -lt 128) {
            $nextRun = 1
            while (($i + $nextRun) -lt $InputBytes.Length -and $nextRun -lt 3 -and $InputBytes[$i + $nextRun] -eq $InputBytes[$i]) { $nextRun++ }
            if ($nextRun -ge 3) { break }
            $i++
        }
        $length = $i - $start
        $out.Add([byte]($length - 1))
        for ($j=0; $j -lt $length; $j++) { $out.Add($InputBytes[$start + $j]) }
    }
    return $out.ToArray()
}
function Escape-Cpp([string]$Value) { return $Value.Replace('\','\\').Replace('"','\"') }
function Escape-Rc([string]$Value) { return $Value.Replace('\','\\').Replace('"','\"') }

$expanded = New-Object 'Collections.Generic.List[object]'
foreach ($item in @($manifest.resources)) { $expanded.Add($item) }
foreach ($tree in @($manifest.trees)) {
    $treeRoot = [IO.Path]::GetFullPath((Join-Path $ProjectDir ([string]$tree.source)))
    if (-not $treeRoot.StartsWith($ProjectDir, [StringComparison]::OrdinalIgnoreCase) -or
        -not (Test-Path -LiteralPath $treeRoot -PathType Container)) { throw "Invalid resource tree: $treeRoot" }
    $treeFiles = @(Get-ChildItem -LiteralPath $treeRoot -Recurse -File | Sort-Object FullName)
    $index = 0
    foreach ($file in $treeFiles) {
        $relative = $file.FullName.Substring($treeRoot.TrimEnd('\').Length).TrimStart('\','/').Replace('\','/')
        $safeName = ($relative -replace '[^A-Za-z0-9_]', '_')
        $extension = $file.Extension.ToLowerInvariant()
        $type = if ($extension -eq '.json') {'json'} elseif ($extension -in @('.png','.jpg','.jpeg','.webp','.ico','.svg')) {'image'} else {'text'}
        $expanded.Add([pscustomobject]@{
            name = ([string]$tree.namePrefix) + '_' + $safeName
            id = [int]$tree.idStart + $index
            logicalName = (([string]$tree.logicalPrefix).TrimEnd('/') + '/' + $relative)
            source = $file.FullName.Substring($ProjectDir.Length).TrimStart('\','/')
            type = $type
            mandatory = [bool]$tree.mandatory
        })
        $index++
    }
}
$items = @($expanded | Sort-Object {[int]$_.id}, {[string]$_.logicalName})
if ($items.Count -eq 0) { throw 'Embedded resource manifest is empty.' }
$seenIds = @{}; $seenNames = @{}; $rc = New-Object 'Collections.Generic.List[string]'
$entries = New-Object 'Collections.Generic.List[string]'
$enum = New-Object 'Collections.Generic.List[string]'
$totalOriginal = [uint64]0; $totalStored = [uint64]0

foreach ($item in $items) {
    $id = [int]$item.id; $name = [string]$item.name; $logical = ([string]$item.logicalName).Replace('\','/')
    if ($id -lt 300 -or $id -gt 65535) { throw "Resource id out of range 300..65535: $id" }
    if ($name -notmatch '^[A-Za-z][A-Za-z0-9_]*$') { throw "Invalid C++ resource name: $name" }
    if ($seenIds.ContainsKey($id) -or $seenNames.ContainsKey($logical.ToLowerInvariant())) { throw "Duplicate embedded resource: $id / $logical" }
    $seenIds[$id]=$true; $seenNames[$logical.ToLowerInvariant()]=$true
    $source = [IO.Path]::GetFullPath((Join-Path $ProjectDir ([string]$item.source)))
    if (-not $source.StartsWith($ProjectDir, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Invalid or missing source: $source" }
    $raw = [IO.File]::ReadAllBytes($source)
    if ($raw.Length -eq 0 -or $raw.Length -gt 64MB) { throw "Invalid resource size: $logical" }
    if ([string]$item.type -eq 'json') {
        try {
            $jsonBytes = $raw
            if ($jsonBytes.Length -ge 3 -and $jsonBytes[0] -eq 0xEF -and $jsonBytes[1] -eq 0xBB -and $jsonBytes[2] -eq 0xBF) {
                $jsonBytes = $jsonBytes[3..($jsonBytes.Length - 1)]
            }
            $null = ([Text.Encoding]::UTF8.GetString($jsonBytes) | ConvertFrom-Json)
        } catch {
            throw "Invalid JSON resource ${logical}: $($_.Exception.Message)"
        }
    }
    $packed = Compress-PackBits $raw
    $compression = [byte]0; $payload = $raw
    if ($packed.Length -lt $raw.Length) { $compression = 1; $payload = $packed }
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $digest = $sha.ComputeHash($raw) } finally { $sha.Dispose() }
    $blob = New-Object 'Collections.Generic.List[byte]'
    Put-U32 $blob 0x5352474F; Put-U16 $blob 1; Put-U16 $blob 0
    $blob.Add($compression); $blob.Add(0); $blob.Add(0); $blob.Add(0)
    Put-U32 $blob ([uint32]$raw.Length); Put-U32 $blob ([uint32]$payload.Length)
    $blob.AddRange([byte[]]$digest); $blob.AddRange([byte[]]$payload)
    $blobPath = Join-Path $generatedDir (('{0:D4}-{1}.bin' -f $id,$name))
    [IO.File]::WriteAllBytes($blobPath, $blob.ToArray())
    $rc.Add(('{0} RCDATA "{1}"' -f $id,(Escape-Rc $blobPath)))
    $enum.Add(('    {0} = {1},' -f $name,$id))
    $entries.Add(('    EmbeddedResourceDescriptor{{EmbeddedResourceId::{0}, "{1}", {2}u, {3}u, {4}}},' -f $name,(Escape-Cpp $logical),$raw.Length,$payload.Length,([string]([bool]$item.mandatory)).ToLowerInvariant()))
    $totalOriginal += [uint64]$raw.Length; $totalStored += [uint64]$payload.Length
    Write-Host "[EmbeddedResources] $logical mode=$(if($compression -eq 1){'PACKBITS'}else{'NONE'}) original=$($raw.Length) stored=$($payload.Length)"
}

$header = @(
    '#pragma once', '#include <array>', '#include <cstddef>', '#include <cstdint>',
    'namespace OmniGhost {', 'enum class EmbeddedResourceId : std::uint16_t {',
    ($enum -join "`n"), '};',
    'struct EmbeddedResourceDescriptor { EmbeddedResourceId id; const char* logicalName; std::uint32_t originalSize; std::uint32_t storedSize; bool mandatory; };',
    "inline constexpr std::array<EmbeddedResourceDescriptor, $($items.Count)> kEmbeddedResourceCatalog = {{", ($entries -join "`n"), '}};',
    '} // namespace OmniGhost'
)
[IO.File]::WriteAllLines($catalogPath, $header, (New-Object Text.UTF8Encoding($false)))
[IO.File]::WriteAllLines($rcPath, $rc, (New-Object Text.UTF8Encoding($false)))
Write-Host "[EmbeddedResources] resources=$($items.Count) original=$totalOriginal stored=$totalStored"
Write-Host "[EmbeddedResources] catalog=$catalogPath"
Write-Host "[EmbeddedResources] rc=$rcPath"
$generatorLock.Dispose()
