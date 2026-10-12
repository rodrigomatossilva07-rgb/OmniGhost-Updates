[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'ReleaseOutput.ps1')
$resourceTimer = [Diagnostics.Stopwatch]::StartNew()
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
$Platform = ($Platform -replace '[^A-Za-z0-9_.-]', '_')
$configurationGeneratedDir = Join-Path $ProjectDir ".cache\generated\$Configuration\$Platform"
$manifestPath = Join-Path $ProjectDir 'resources\embedded-resources.json'
$generatedDir = Join-Path $configurationGeneratedDir 'resources'
$catalogPath = Join-Path $configurationGeneratedDir 'embedded_resource_catalog.h'
$rcPath = Join-Path $configurationGeneratedDir 'embedded_resources.rc2'
$compressionCacheRoot = $null
$compressionCacheEnabled = $env:OMNIGHOST_DISABLE_RESOURCE_CACHE -ne '1'
if ($compressionCacheEnabled) {
    try {
        $configuredCacheRoot = $env:OMNIGHOST_RESOURCE_CACHE_DIR
        if ([string]::IsNullOrWhiteSpace($configuredCacheRoot)) {
            $localAppData = [Environment]::GetFolderPath([Environment+SpecialFolder]::LocalApplicationData)
            if ([string]::IsNullOrWhiteSpace($localAppData)) { throw 'LOCALAPPDATA is unavailable.' }
            $configuredCacheRoot = Join-Path $localAppData 'OmniGhost\build-cache'
        }
        $compressionCacheRoot = Join-Path ([IO.Path]::GetFullPath($configuredCacheRoot)) 'lzms-v1'
        New-Item -ItemType Directory -Path $compressionCacheRoot -Force | Out-Null
    }
    catch {
        Write-Warning "Embedded resource compression cache disabled: $($_.Exception.Message)"
        $compressionCacheEnabled = $false
        $compressionCacheRoot = $null
    }
}

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
function Compute-Sha256([byte[]]$Bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ,([byte[]]$sha.ComputeHash($Bytes)) } finally { $sha.Dispose() }
}
function ConvertTo-Hex([byte[]]$Bytes) {
    return -join @($Bytes | ForEach-Object { $_.ToString('x2') })
}
function Test-BytesEqual([byte[]]$Left, [byte[]]$Right) {
    if ($null -eq $Left -or $null -eq $Right -or $Left.Length -ne $Right.Length) { return $false }
    for ($i = 0; $i -lt $Left.Length; $i++) {
        if ($Left[$i] -ne $Right[$i]) { return $false }
    }
    return $true
}
function Acquire-CompressionCacheLock([string]$Key) {
    $lockPath = Join-Path $compressionCacheRoot ($Key + '.lock')
    $deadline = [DateTime]::UtcNow.AddMinutes(10)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { return [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
        catch [IO.IOException] { Start-Sleep -Milliseconds 100 }
    }
    throw "Timed out waiting for compression cache entry: $Key"
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
    $extensions = @()
    if ($tree.PSObject.Properties.Name -contains 'extensions') {
        $extensions = @($tree.extensions | ForEach-Object { ([string]$_).ToLowerInvariant() })
    }
    if ($extensions.Count -gt 0) {
        $treeFiles = @($treeFiles | Where-Object { $extensions -contains $_.Extension.ToLowerInvariant() })
    }
    $index = 0
    foreach ($file in $treeFiles) {
        $relative = $file.FullName.Substring($treeRoot.TrimEnd('\').Length).TrimStart('\','/').Replace('\','/')
        $logicalName = (([string]$tree.logicalPrefix).TrimEnd('/') + '/' + $relative)
        if ($logicalName.StartsWith('cs2/collision/', [StringComparison]::OrdinalIgnoreCase) -and
            ($file.Length -lt 36000 -or $file.Length % 36 -ne 0)) {
            Write-Warning "Skipping incomplete CS2 collision resource: $relative"
            continue
        }
        $safeName = ($relative -replace '[^A-Za-z0-9_]', '_')
        $extension = $file.Extension.ToLowerInvariant()
        $type = if ($extension -eq '.json') {'json'} elseif ($extension -in @('.png','.jpg','.jpeg','.webp','.ico','.svg')) {'image'} else {'text'}
        $expanded.Add([pscustomobject]@{
            name = ([string]$tree.namePrefix) + '_' + $safeName
            id = [int]$tree.idStart + $index
            logicalName = $logicalName
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
$collisionItemCount = 0
$totalCollisionBytes = [uint64]0
foreach ($item in $items) {
    $source = [IO.Path]::GetFullPath((Join-Path $ProjectDir ([string]$item.source)))
    if (([string]$item.logicalName).StartsWith('cs2/collision/', [StringComparison]::OrdinalIgnoreCase)) {
        $collisionItemCount++
        if (Test-Path -LiteralPath $source -PathType Leaf) { $totalCollisionBytes += [uint64]([IO.FileInfo]$source).Length }
    }
}
if ($null -eq ('OmniGhostLzms' -as [type])) {
    # Add-Type invokes the C# compiler, which treats every LIB entry as a
    # compiler search path. A partially initialized VS environment can leave
    # relative entries such as "lib\um\x64" there; those are warnings-as-errors
    # even though this P/Invoke-only helper does not link against them.
    $originalProcessLib = [Environment]::GetEnvironmentVariable('LIB', 'Process')
    $validLibPaths = @(
        $originalProcessLib -split ';' | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_) -and
            [IO.Path]::IsPathRooted($_) -and
            (Test-Path -LiteralPath $_ -PathType Container)
        }
    )
    [Environment]::SetEnvironmentVariable('LIB', ($validLibPaths -join ';'), 'Process')
    try {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class OmniGhostLzms {
    const uint Lzms = 5;
    [DllImport("cabinet.dll", SetLastError=true)] static extern bool CreateCompressor(uint algorithm, IntPtr alloc, out IntPtr handle);
    [DllImport("cabinet.dll", SetLastError=true)] static extern bool CloseCompressor(IntPtr handle);
    [DllImport("cabinet.dll", SetLastError=true)] static extern bool Compress(IntPtr handle, byte[] source, UIntPtr sourceSize, byte[] destination, UIntPtr destinationSize, out UIntPtr needed);
    [DllImport("cabinet.dll", SetLastError=true)] static extern bool CreateDecompressor(uint algorithm, IntPtr alloc, out IntPtr handle);
    [DllImport("cabinet.dll", SetLastError=true)] static extern bool CloseDecompressor(IntPtr handle);
    [DllImport("cabinet.dll", SetLastError=true)] static extern bool Decompress(IntPtr handle, byte[] source, UIntPtr sourceSize, byte[] destination, UIntPtr destinationSize, out UIntPtr needed);
    public static byte[] CompressBytes(byte[] source) {
        IntPtr handle;
        if (!CreateCompressor(Lzms, IntPtr.Zero, out handle)) throw new InvalidOperationException("CreateCompressor failed");
        try {
            ulong initial = (ulong)source.Length + Math.Max(65536UL, (ulong)source.Length / 100UL);
            byte[] destination = new byte[checked((int)initial)]; UIntPtr needed;
            if (!Compress(handle, source, (UIntPtr)source.Length, destination, (UIntPtr)destination.Length, out needed)) {
                destination = new byte[checked((int)needed.ToUInt64())];
                if (!Compress(handle, source, (UIntPtr)source.Length, destination, (UIntPtr)destination.Length, out needed))
                    throw new InvalidOperationException("Compress failed");
            }
            Array.Resize(ref destination, checked((int)needed.ToUInt64())); return destination;
        } finally { CloseCompressor(handle); }
    }
    public static byte[] DecompressBytes(byte[] source, int expectedLength) {
        if (expectedLength <= 0) throw new ArgumentOutOfRangeException("expectedLength");
        IntPtr handle;
        if (!CreateDecompressor(Lzms, IntPtr.Zero, out handle)) throw new InvalidOperationException("CreateDecompressor failed");
        try {
            byte[] destination = new byte[expectedLength]; UIntPtr needed;
            if (!Decompress(handle, source, (UIntPtr)source.Length, destination, (UIntPtr)destination.Length, out needed))
                throw new InvalidOperationException("Decompress failed");
            if (needed.ToUInt64() != (ulong)expectedLength)
                throw new InvalidOperationException("Decompressed length mismatch");
            return destination;
        } finally { CloseDecompressor(handle); }
    }
}
'@
    }
    finally {
        [Environment]::SetEnvironmentVariable('LIB', $originalProcessLib, 'Process')
    }
}
function Compress-Lzms([byte[]]$InputBytes) { return [OmniGhostLzms]::CompressBytes($InputBytes) }
function Read-ValidatedLzmsCache([string]$CachePath, [byte[]]$SourceDigest, [int]$SourceLength) {
    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) { return $null }
    try {
        $record = [IO.File]::ReadAllBytes($CachePath)
        if ($record.Length -lt 89) { throw 'cache record is truncated' }
        $magic = [Text.Encoding]::ASCII.GetString($record, 0, 8)
        if ($magic -ne 'OGLZMS01') { throw 'cache record has an unsupported format' }
        $storedSourceDigest = New-Object byte[] 32
        $storedPayloadDigest = New-Object byte[] 32
        [Buffer]::BlockCopy($record, 8, $storedSourceDigest, 0, 32)
        [Buffer]::BlockCopy($record, 40, $storedPayloadDigest, 0, 32)
        if (-not (Test-BytesEqual $storedSourceDigest $SourceDigest)) { throw 'source digest mismatch' }
        $storedSourceLength = [BitConverter]::ToUInt64($record, 72)
        $payloadLength = [BitConverter]::ToUInt64($record, 80)
        if ($storedSourceLength -ne [uint64]$SourceLength) { throw 'source length mismatch' }
        if ($payloadLength -eq 0 -or $payloadLength -ge [uint64]$SourceLength -or
            $payloadLength -gt [uint64]([int]::MaxValue) -or
            [uint64]$record.Length -ne ([uint64]88 + $payloadLength)) { throw 'payload length mismatch' }
        $payload = New-Object byte[] ([int]$payloadLength)
        [Buffer]::BlockCopy($record, 88, $payload, 0, [int]$payloadLength)
        $actualPayloadDigest = Compute-Sha256 $payload
        if (-not (Test-BytesEqual $storedPayloadDigest $actualPayloadDigest)) { throw 'payload digest mismatch' }

        # The cache is local and must be treated as untrusted. Decompress it and
        # verify the reconstructed source hash before embedding any cached byte.
        $roundTrip = [OmniGhostLzms]::DecompressBytes($payload, $SourceLength)
        $roundTripDigest = Compute-Sha256 $roundTrip
        if (-not (Test-BytesEqual $SourceDigest $roundTripDigest)) { throw 'decompressed source digest mismatch' }
        return ,([byte[]]$payload)
    }
    catch {
        Write-Warning "Discarding invalid LZMS cache entry '$CachePath': $($_.Exception.Message)"
        Remove-Item -LiteralPath $CachePath -Force -ErrorAction SilentlyContinue
        return $null
    }
}
function Write-LzmsCacheAtomically([string]$CachePath, [byte[]]$SourceDigest, [int]$SourceLength, [byte[]]$Payload) {
    $payloadDigest = Compute-Sha256 $Payload
    $record = New-Object byte[] (88 + $Payload.Length)
    [Buffer]::BlockCopy([Text.Encoding]::ASCII.GetBytes('OGLZMS01'), 0, $record, 0, 8)
    [Buffer]::BlockCopy($SourceDigest, 0, $record, 8, 32)
    [Buffer]::BlockCopy($payloadDigest, 0, $record, 40, 32)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]$SourceLength), 0, $record, 72, 8)
    [Buffer]::BlockCopy([BitConverter]::GetBytes([uint64]$Payload.Length), 0, $record, 80, 8)
    [Buffer]::BlockCopy($Payload, 0, $record, 88, $Payload.Length)
    $temporary = $CachePath + '.' + $PID + '.' + [Guid]::NewGuid().ToString('N') + '.tmp'
    try {
        [IO.File]::WriteAllBytes($temporary, $record)
        if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
            [IO.File]::Move($temporary, $CachePath)
        }
    }
    finally {
        if (Test-Path -LiteralPath $temporary -PathType Leaf) {
            Remove-Item -LiteralPath $temporary -Force -ErrorAction SilentlyContinue
        }
    }
}
$processedCollisionBytes = [uint64]0
$processedCollisionItems = 0
$compressionCacheHits = 0
$compressionCacheMisses = 0
$compressionCacheInvalid = 0

foreach ($item in $items) {
    $id = [int]$item.id; $name = [string]$item.name; $logical = ([string]$item.logicalName).Replace('\','/')
    if ($id -lt 300 -or $id -gt 65535) { throw "Resource id out of range 300..65535: $id" }
    if ($name -notmatch '^[A-Za-z][A-Za-z0-9_]*$') { throw "Invalid C++ resource name: $name" }
    if ($seenIds.ContainsKey($id) -or $seenNames.ContainsKey($logical.ToLowerInvariant())) { throw "Duplicate embedded resource: $id / $logical" }
    $seenIds[$id]=$true; $seenNames[$logical.ToLowerInvariant()]=$true
    $source = [IO.Path]::GetFullPath((Join-Path $ProjectDir ([string]$item.source)))
    if (-not $source.StartsWith($ProjectDir, [StringComparison]::OrdinalIgnoreCase) -or -not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Invalid or missing source: $source" }
    $raw = [IO.File]::ReadAllBytes($source)
    # Git worktrees may materialize web text with different line endings.
    # Embed canonical UTF-8/LF bytes so independent Publish builds have
    # identical resource payloads regardless of checkout settings.
    $textExtensions = @('.js', '.css', '.html', '.svg', '.json', '.txt', '.xml')
    if ([IO.Path]::GetExtension($source).ToLowerInvariant() -in $textExtensions) {
        $utf8Strict = New-Object Text.UTF8Encoding($false, $true)
        $text = $utf8Strict.GetString($raw)
        $text = $text.Replace("`r`n", "`n").Replace("`r", "`n")
        $raw = $utf8Strict.GetBytes($text)
    }
    # Blob lengths and Windows PE resources are uint32-based.  The former 64 MiB
    # policy was artificial and rejected several valid CS2 collision meshes.
    if ($raw.Length -eq 0) { throw "Invalid resource size: $logical" }
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
    $digest = Compute-Sha256 $raw
    $compression = [byte]0; $payload = $raw
    if ([IO.Path]::GetExtension($source).ToLowerInvariant() -eq '.tri') {
        $lzms = $null
        if ($compressionCacheEnabled) {
            $sourceKey = ConvertTo-Hex $digest
            $cachePath = Join-Path $compressionCacheRoot ($sourceKey + '.oglzms')
            $cacheLock = Acquire-CompressionCacheLock $sourceKey
            try {
                $cacheExisted = Test-Path -LiteralPath $cachePath -PathType Leaf
                $lzms = Read-ValidatedLzmsCache $cachePath $digest $raw.Length
                if ($null -ne $lzms) {
                    $compressionCacheHits++
                } else {
                    if ($cacheExisted) { $compressionCacheInvalid++ }
                    $compressionCacheMisses++
                    $lzms = Compress-Lzms $raw
                    if ($lzms.Length -lt $raw.Length) {
                        Write-LzmsCacheAtomically $cachePath $digest $raw.Length $lzms
                    }
                }
            }
            finally {
                $cacheLock.Dispose()
            }
        } else {
            $compressionCacheMisses++
            $lzms = Compress-Lzms $raw
        }
        if ($lzms.Length -lt $raw.Length) { $compression = 2; $payload = $lzms }
    } else {
        $packed = Compress-PackBits $raw
        if ($packed.Length -lt $raw.Length) { $compression = 1; $payload = $packed }
    }
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
    $isCollision = $logical.StartsWith('cs2/collision/', [StringComparison]::OrdinalIgnoreCase)
    if ($isCollision) {
        $processedCollisionItems++
        $processedCollisionBytes += [uint64]$raw.Length
        $percent = if ($totalCollisionBytes -gt 0) { [math]::Min(100, [math]::Floor(($processedCollisionBytes * 100.0) / $totalCollisionBytes)) } else { 100 }
        $mapProgress = " mapa $processedCollisionItems/$collisionItemCount"
        Write-Progress -Activity 'Embedding CS2 collision maps' -Status "${percent}%$mapProgress - $logical" -PercentComplete $percent
        $mode = if($compression -eq 2){'LZMS'}elseif($compression -eq 1){'PACKBITS'}else{'NONE'}
		$verboseResources = $env:OMNIGHOST_VERBOSE_BUILD -eq '1'
		$progressMilestone = $processedCollisionItems -eq 1 -or
			$processedCollisionItems -eq $collisionItemCount -or
			($processedCollisionItems % 6) -eq 0
		if ($verboseResources -or $progressMilestone) {
			Write-Host "[EmbeddedResources] [$percent%$mapProgress] $logical mode=$mode original=$($raw.Length) stored=$($payload.Length)"
		}
    }
}
Write-Progress -Activity 'Embedding CS2 collision maps' -Completed

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
if ($totalOriginal -gt 0) { $savedPercent = (1.0 - ([double]$totalStored / [double]$totalOriginal)) * 100.0 } else { $savedPercent = 0.0 }
Write-Host ("[EmbeddedResources] {0} recursos: {1} -> {2} ({3:N1}% menor); tempo={4}" -f `
    $items.Count, (Format-OmniBytes $totalOriginal), (Format-OmniBytes $totalStored), $savedPercent,
    (Format-OmniDuration $resourceTimer.Elapsed))
Write-Host "[EmbeddedResources] LZMS cache hits=$compressionCacheHits misses=$compressionCacheMisses invalid=$compressionCacheInvalid root=$compressionCacheRoot"
if ($env:OMNIGHOST_VERBOSE_BUILD -eq '1') {
    Write-Host "[EmbeddedResources] Catalogo: $catalogPath"
    Write-Host "[EmbeddedResources] RC: $rcPath"
}
$generatorLock.Dispose()
