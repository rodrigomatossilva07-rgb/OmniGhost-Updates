[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectDir,
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Configuration = ($Configuration -replace '[^A-Za-z0-9_.-]', '_')
$Platform = ($Platform -replace '[^A-Za-z0-9_.-]', '_')
$ConfigurationGeneratedDir = Join-Path $ProjectDir ".cache\generated\$Configuration\$Platform"
$GeneratedDir = Join-Path $ConfigurationGeneratedDir 'offsets'
$RcOutput = Join-Path $ConfigurationGeneratedDir 'embedded_offsets.rc2'
New-Item -ItemType Directory -Path $GeneratedDir -Force | Out-Null

function Acquire-GeneratorLock([string]$Name) {
    $lockPath = Join-Path $ConfigurationGeneratedDir (".$Name.lock")
    $deadline = [DateTime]::UtcNow.AddMinutes(5)
    while ([DateTime]::UtcNow -lt $deadline) {
        try { return [IO.File]::Open($lockPath, 'OpenOrCreate', 'ReadWrite', 'None') }
        catch [IO.IOException] { Start-Sleep -Milliseconds 100 }
    }
    throw "Timed out waiting for embedded generator lock: $Name"
}
$generatorLock = Acquire-GeneratorLock 'offsets'

$ContainerMagic = [uint32]0x464F474F # OGOF
$PayloadMagic = [uint32]0x504E534F   # OSNP
$ContainerVersion = [uint16]1
$PayloadVersion = [uint16]1
$CompressionPackBits = [byte]1
$Utf8 = New-Object System.Text.UTF8Encoding($false, $true)

$SkipMetadataKeys = @(
    'schema_version','game','build','cl','module','source','generated_at','verified_utc',
    'note','build_note','id','game_slug','version','label','pinned','published',
    'created_at','updated_at','markdown','updated_utc','build_id'
)

function Get-PropertyValue($Object, [string]$Name, $Default = $null) {
    if ($null -eq $Object) { return $Default }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $Default }
    return $property.Value
}

function Get-Fnv1a32([string]$Text) {
    [uint64]$hash = 2166136261
    foreach ($b in $Utf8.GetBytes($Text)) {
        $hash = (($hash -bxor [uint64]$b) * 16777619) -band [uint64]4294967295
    }
    return [uint32]$hash
}

function Add-FlattenedOffsets($Node, [string]$Path, [hashtable]$Entries) {
    if ($null -eq $Node) { return }
    if ($Node -is [System.Management.Automation.PSCustomObject]) {
        foreach ($property in $Node.PSObject.Properties) {
            if ($property.Name -in $SkipMetadataKeys) { continue }
            # Prefer walking offsets / offsets_flat / structs / globals without doubling path noise for top-level wrappers
            $child = if ([string]::IsNullOrEmpty($Path)) { $property.Name } else { "$Path.$($property.Name)" }
            Add-FlattenedOffsets $property.Value $child $Entries
        }
        return
    }
    if ($Node -is [string]) {
        if ($Node -match '^0[xX][0-9A-Fa-f]+$') {
            $hex = $Node.Substring(2)
            [uint64]$value = [Convert]::ToUInt64($hex, 16)
            [uint32]$hash = Get-Fnv1a32 $Path
            if ($Entries.ContainsKey($hash)) {
                # Same value collision ok; conflicting values fail
                if ([uint64]$Entries[$hash].Value -ne $value) {
                    throw "Offset-key hash collision between '$Path' and '$($Entries[$hash].Path)'."
                }
                return
            }
            $Entries[$hash] = [pscustomobject]@{ Path=$Path; Value=$value; Hash=$hash }
            return
        }
        if ($Node -match '^[0-9]+$') {
            throw "Ambiguous numeric string '$Node' at '$Path'. Use an explicit 0x... hexadecimal value."
        }
        return
    }
    if ($Node -is [bool]) { return }
    if ($Node -is [byte] -or $Node -is [int16] -or $Node -is [int32] -or $Node -is [int64] -or
        $Node -is [uint16] -or $Node -is [uint32] -or $Node -is [uint64] -or $Node -is [double] -or $Node -is [decimal]) {
        throw "Numeric offset at '$Path' must be encoded as an explicit 0x... string."
    }
    if ($Node -is [System.Collections.IEnumerable]) {
        # Skip arrays (markdown lists etc.)
        return
    }
    throw "Unsupported JSON value at '$Path' ($($Node.GetType().FullName))."
}

function Write-Utf8String([System.IO.BinaryWriter]$Writer, [string]$Value) {
    if ($null -eq $Value) { $Value = '' }
    [byte[]]$bytes = $Utf8.GetBytes($Value)
    if ($bytes.Length -gt 65535) { throw 'Metadata string exceeds 65535 bytes.' }
    $Writer.Write([uint16]$bytes.Length)
    if ($bytes.Length -gt 0) { $Writer.Write($bytes) }
}

function Compress-PackBits([byte[]]$Data) {
    $output = New-Object 'System.Collections.Generic.List[byte]'
    $i = 0
    while ($i -lt $Data.Length) {
        $run = 1
        while (($i + $run) -lt $Data.Length -and $run -lt 128 -and $Data[$i + $run] -eq $Data[$i]) { $run++ }
        if ($run -ge 4) {
            $output.Add([byte](0x80 -bor ($run - 1)))
            $output.Add($Data[$i])
            $i += $run
            continue
        }

        $literalStart = $i
        $i += $run
        while ($i -lt $Data.Length -and ($i - $literalStart) -lt 128) {
            $nextRun = 1
            while (($i + $nextRun) -lt $Data.Length -and $nextRun -lt 128 -and $Data[$i + $nextRun] -eq $Data[$i]) { $nextRun++ }
            if ($nextRun -ge 4) { break }
            if (($i - $literalStart) + $nextRun -gt 128) { break }
            $i += $nextRun
        }
        $length = $i - $literalStart
        $output.Add([byte]($length - 1))
        for ($j = 0; $j -lt $length; $j++) { $output.Add($Data[$literalStart + $j]) }
    }
    return $output.ToArray()
}

function Build-Snapshot([string]$InputPath, [uint16]$GameId, [string]$ExpectedGame, [string]$OutputPath) {
    if (-not (Test-Path -LiteralPath $InputPath -PathType Leaf)) { throw "Offset source missing: $InputPath" }
    $raw = Get-Content -LiteralPath $InputPath -Raw -Encoding UTF8
    try { $json = $raw | ConvertFrom-Json } catch { throw "Invalid JSON in $InputPath : $($_.Exception.Message)" }

    $schemaValue = Get-PropertyValue $json 'schema_version' 1
    [uint32]$schema = 0
    if (-not [uint32]::TryParse([string]$schemaValue, [ref]$schema) -or $schema -eq 0) { $schema = 1 }
    $game = [string](Get-PropertyValue $json 'game' $ExpectedGame)
    $build = [string](Get-PropertyValue $json 'build' '')
    if ([string]::IsNullOrWhiteSpace($build)) {
        $build = [string](Get-PropertyValue $json 'version' (Get-PropertyValue $json 'build_id' 'unknown'))
    }
    $cl = [string](Get-PropertyValue $json 'cl' '')
    if ([string]::IsNullOrWhiteSpace($cl)) { $cl = [string](Get-PropertyValue $json 'build_id' (Get-PropertyValue $json 'id' '')) }
    $module = [string](Get-PropertyValue $json 'module' '')
    if ([string]::IsNullOrWhiteSpace($module)) {
        switch ($ExpectedGame) {
            'cs2' { $module = 'client.dll' }
            'rust' { $module = 'GameAssembly.dll' }
            'fivem' { $module = 'GTA5.exe' }
            'apex' { $module = 'r5apex.exe' }
            'warzone' { $module = 'cod.exe' }
            'fortnite' { $module = 'FortniteClient-Win64-Shipping.exe' }
            default { $module = 'unknown' }
        }
    }
    $source = [string](Get-PropertyValue $json 'source' '')
    if ([string]::IsNullOrWhiteSpace($source)) { $source = "https://www.cheatoffsets.com/g/$ExpectedGame" }
    $generatedAt = [string](Get-PropertyValue $json 'generated_at' (Get-PropertyValue $json 'verified_utc' (Get-PropertyValue $json 'updated_at' (Get-PropertyValue $json 'updated_utc' ''))))
    $note = [string](Get-PropertyValue $json 'note' (Get-PropertyValue $json 'build_note' (Get-PropertyValue $json 'label' '')))
    if ($game -cne $ExpectedGame) { throw "Expected game '$ExpectedGame' in $InputPath, got '$game'." }
    if ([string]::IsNullOrWhiteSpace($build) -or [string]::IsNullOrWhiteSpace($module) -or [string]::IsNullOrWhiteSpace($source)) {
        throw "Required metadata build/module/source missing in $InputPath."
    }

    $entries = @{}
    # Prefer offsets_flat when present (API dumps) to avoid markdown noise
    $flat = Get-PropertyValue $json 'offsets_flat' $null
    if ($null -ne $flat) {
        Add-FlattenedOffsets $flat '' $entries
    }
    $nested = Get-PropertyValue $json 'offsets' $null
    if ($null -ne $nested) {
        Add-FlattenedOffsets $nested 'offsets' $entries
    }
    # Always walk remaining object (client.dll, globals, structs, etc.)
    Add-FlattenedOffsets $json '' $entries
    if ($entries.Count -eq 0) { throw "No hexadecimal offsets found in $InputPath." }
    $sorted = @($entries.Values | Sort-Object Hash)

    $payloadStream = New-Object System.IO.MemoryStream
    $payloadWriter = New-Object System.IO.BinaryWriter($payloadStream, $Utf8, $true)
    try {
        $payloadWriter.Write($PayloadMagic)
        $payloadWriter.Write($PayloadVersion)
        $payloadWriter.Write([uint16]0)
        $payloadWriter.Write($schema)
        Write-Utf8String $payloadWriter $game
        Write-Utf8String $payloadWriter $build
        Write-Utf8String $payloadWriter $cl
        Write-Utf8String $payloadWriter $module
        Write-Utf8String $payloadWriter $source
        Write-Utf8String $payloadWriter $generatedAt
        Write-Utf8String $payloadWriter $note
        $payloadWriter.Write([uint32]$sorted.Count)
        foreach ($entry in $sorted) {
            $payloadWriter.Write([uint32]$entry.Hash)
            $payloadWriter.Write([uint32]0)
            $payloadWriter.Write([uint64]$entry.Value)
        }
        $payloadWriter.Flush()
        [byte[]]$payload = $payloadStream.ToArray()
    } finally {
        $payloadWriter.Dispose(); $payloadStream.Dispose()
    }

    [byte[]]$compressed = Compress-PackBits $payload
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { [byte[]]$digest = $sha.ComputeHash($payload) } finally { $sha.Dispose() }

    $blobStream = New-Object System.IO.MemoryStream
    $blobWriter = New-Object System.IO.BinaryWriter($blobStream, $Utf8, $true)
    try {
        $blobWriter.Write($ContainerMagic)
        $blobWriter.Write($ContainerVersion)
        $blobWriter.Write($GameId)
        $blobWriter.Write($CompressionPackBits)
        $blobWriter.Write([byte]0); $blobWriter.Write([byte]0); $blobWriter.Write([byte]0)
        $blobWriter.Write([uint32]$compressed.Length)
        $blobWriter.Write([uint32]$payload.Length)
        $blobWriter.Write($digest)
        $blobWriter.Write($compressed)
        $blobWriter.Flush()
        [IO.File]::WriteAllBytes($OutputPath, $blobStream.ToArray())
    } finally {
        $blobWriter.Dispose(); $blobStream.Dispose()
    }

    Write-Host ("[EmbeddedOffsets] {0}: entries={1} payload={2} compressed={3} output={4}" -f $ExpectedGame,$sorted.Count,$payload.Length,$compressed.Length,$OutputPath)
}

# Game IDs must match EmbeddedOffsets::Game / resource.h
$games = @(
    @{ Id=1; Name='fortnite'; Json='data\fortnite_offsets.json'; Resource='IDR_OFFSETS_FORTNITE' },
    @{ Id=2; Name='warzone';  Json='data\warzone_offsets.json';  Resource='IDR_OFFSETS_WARZONE' },
    @{ Id=3; Name='cs2';      Json='data\cs2_offsets.json';      Resource='IDR_OFFSETS_CS2' },
    @{ Id=5; Name='fivem';    Json='data\fivem_offsets.json';    Resource='IDR_OFFSETS_FIVEM' },
    @{ Id=6; Name='apex';     Json='data\apex_offsets.json';     Resource='IDR_OFFSETS_APEX' }
)

$rcLines = @('// Generated by tools/Build-EmbeddedOffsets.ps1. Do not edit.')
function Escape-RcPath([string]$Path) { return ([IO.Path]::GetFullPath($Path)).Replace('\','\\') }

foreach ($g in $games) {
    $inPath = Join-Path $ProjectDir $g.Json
    $outPath = Join-Path $GeneratedDir ("{0}_offsets.bin" -f $g.Name)
    Build-Snapshot $inPath ([uint16]$g.Id) $g.Name $outPath
    $rcLines += ('{0} RCDATA "{1}"' -f $g.Resource, (Escape-RcPath $outPath))
}

[IO.File]::WriteAllText($RcOutput, ($rcLines -join [Environment]::NewLine) + [Environment]::NewLine, $Utf8)
Write-Host "[EmbeddedOffsets] RC include: $RcOutput"
$generatorLock.Dispose()
