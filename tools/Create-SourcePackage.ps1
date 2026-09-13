[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$OutputPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)

# Never create a distributable source snapshot from a tree that would fail the
# same engineering/dependency checks used by Visual Studio. In particular, this
# prevents source packages from silently omitting canonical runtime DLLs.
$Validator = Join-Path $ProjectDir 'tools\Validate-Project.ps1'
if (-not (Test-Path -LiteralPath $Validator -PathType Leaf)) {
    throw "Project validator not found: $Validator"
}
Write-Host '[OmniGhost Source Package] Preflight engineering validation...'
& $Validator -ProjectDir $ProjectDir
Write-Host '[OmniGhost Source Package] Preflight PASS. Creating source archive.' -ForegroundColor Green
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $ProjectDir ("artifacts\OmniGhost-source-{0:yyyyMMdd-HHmmss}.zip" -f (Get-Date))
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
if (Test-Path -LiteralPath $OutputPath) { Remove-Item -LiteralPath $OutputPath -Force }

# Source packages are produced from an explicit allowlist. New top-level folders
# do not silently enter a release/source archive just because they were created
# in a developer workspace.
$allowedRootFiles = @(
    '.clang-format', '.editorconfig', '.gitattributes', '.gitignore',
    'OmiGhost.slnx', 'OmiGhost.vcxproj', 'OmiGhost.vcxproj.filters',
    'OmniGhost.Common.props', 'OmniGhost.Release.props',
    'README.txt', 'THIRD_PARTY_NOTICES.txt', 'version.txt',
    'release-notes.override.json', 'release-publish.json'
)
$allowedRoots = @(
    'Cs2', 'DMALibrary', 'Fivem', 'ImGui', 'Utils',
    'Valorant', 'Warzone', 'data', 'docs', 'libs', 'resources', 'runtime', 'src',
    'third_party', 'tools'
)
$excludedDirectoryNames = @('.vs', '.cache', 'build', 'artifacts', '.temp', 'logs', 'x64')
$excludedExtensions = @('.user', '.suo', '.ipch', '.obj', '.pdb', '.ilk', '.tlog', '.lastbuildstate', '.idb', '.iobj', '.ipdb', '.exp')
$allowedMarkdown = @() # Source delivery intentionally carries no loose Markdown documentation.

function Test-IncludedFile([System.IO.FileInfo]$File, [string]$Relative) {
    if ($excludedExtensions -contains $File.Extension.ToLowerInvariant()) { return $false }
    $segments = $Relative -split '[\\/]'
    if (@($segments | Where-Object { $excludedDirectoryNames -contains $_ }).Count -gt 0) { return $false }
    if ($File.Extension -in @('.md', '.markdown') -and ($allowedMarkdown -notcontains $Relative)) { return $false }
    if (($File.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Reparse point is not allowed in source package: $Relative" }
    return $true
}

$files = New-Object System.Collections.Generic.List[object]
foreach ($relative in $allowedRootFiles) {
    $path = Join-Path $ProjectDir $relative
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $item = Get-Item -LiteralPath $path
        if (Test-IncludedFile $item $relative) { $files.Add([pscustomobject]@{ File=$item; Relative=$relative }) }
    }
}
foreach ($rootName in $allowedRoots) {
    $rootPath = Join-Path $ProjectDir $rootName
    if (-not (Test-Path -LiteralPath $rootPath -PathType Container)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force -ErrorAction Stop) {
        $relative = $file.FullName.Substring($ProjectDir.TrimEnd('\').Length).TrimStart('\')
        if (Test-IncludedFile $file $relative) { $files.Add([pscustomobject]@{ File=$file; Relative=$relative }) }
    }
}

# Guard against duplicate/case-colliding archive paths on Windows.
$seen = @{}
foreach ($entry in $files) {
    $key = ([string]$entry.Relative).Replace('/', '\').ToLowerInvariant()
    if ($seen.ContainsKey($key)) { throw "Duplicate source package path: $($entry.Relative)" }
    $seen[$key] = $true
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::Open($OutputPath, [IO.Compression.ZipArchiveMode]::Create)
try {
    $manifestEntries = New-Object System.Collections.Generic.List[object]
    foreach ($entry in ($files | Sort-Object Relative)) {
        [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
            $zip, $entry.File.FullName, $entry.Relative,
            [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        $manifestEntries.Add([ordered]@{
            path = $entry.Relative.Replace('\','/')
            size = [int64]$entry.File.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $entry.File.FullName).Hash.ToLowerInvariant()
        })
    }

    $versionPath = Join-Path $ProjectDir 'version.txt'
    $version = if (Test-Path -LiteralPath $versionPath -PathType Leaf) { (Get-Content -LiteralPath $versionPath -Raw).Trim() } else { 'unknown' }

    # Preserve build provenance without shipping the .git directory.
    $commit = if ($env:GITHUB_SHA) { [string]$env:GITHUB_SHA } else { 'unknown' }
    $sourceEpoch = $null
    $dirty = $false
    if (Test-Path -LiteralPath (Join-Path $ProjectDir '.git')) {
        try {
            $git = Get-Command git -ErrorAction Stop
            $candidate = (& $git.Source -C $ProjectDir rev-parse HEAD 2>$null | Select-Object -First 1)
            if ($candidate) { $commit = ([string]$candidate).Trim() }
            $epochText = (& $git.Source -C $ProjectDir show -s --format=%ct HEAD 2>$null | Select-Object -First 1)
            $epochValue = 0L
            if ([long]::TryParse([string]$epochText, [ref]$epochValue) -and $epochValue -gt 0) { $sourceEpoch = $epochValue }
            & $git.Source -C $ProjectDir diff --quiet --ignore-submodules HEAD -- . 2>$null
            $dirty = ($LASTEXITCODE -ne 0)
        } catch {}
    }
    if ($null -eq $sourceEpoch -and $env:SOURCE_DATE_EPOCH) {
        $epochValue = 0L
        if ([long]::TryParse([string]$env:SOURCE_DATE_EPOCH, [ref]$epochValue) -and $epochValue -gt 0) { $sourceEpoch = $epochValue }
    }

    # Extracted source packages may legitimately have no .git directory. Give them
    # a deterministic source-tree identity instead of serialising commit_id=unknown.
    if ([string]::IsNullOrWhiteSpace($commit) -or $commit -eq 'unknown') {
        $IdentityLines = @($manifestEntries | Sort-Object path | ForEach-Object { "{0}`t{1}`t{2}" -f $_.path, $_.sha256, $_.size })
        $IdentityBytes = (New-Object Text.UTF8Encoding($false)).GetBytes(($IdentityLines -join "`n") + "`n")
        $IdentitySha = [Security.Cryptography.SHA256]::Create()
        try {
            $TreeHash = ([BitConverter]::ToString($IdentitySha.ComputeHash($IdentityBytes))).Replace('-','').ToLowerInvariant()
        } finally { $IdentitySha.Dispose() }
        $commit = 'source-tree-' + $TreeHash.Substring(0,16)
    }

    $sourceMetadataObject = [ordered]@{
        schema = 1
        product = 'OmniGhost'
        version = $version
        commit_id = $commit
        dirty = [bool]$dirty
        source_date_epoch = $sourceEpoch
        packaged_utc = (Get-Date).ToUniversalTime().ToString('o')
    }
    $sourceMetadataJson = ($sourceMetadataObject | ConvertTo-Json -Depth 4) + [Environment]::NewLine
    $sourceMetadataBytes = (New-Object Text.UTF8Encoding($false)).GetBytes($sourceMetadataJson)
    $sourceMetadataEntry = $zip.CreateEntry('SOURCE_BUILD_METADATA.json', [IO.Compression.CompressionLevel]::Optimal)
    $sourceStream = $sourceMetadataEntry.Open()
    try { $sourceStream.Write($sourceMetadataBytes, 0, $sourceMetadataBytes.Length) } finally { $sourceStream.Dispose() }
    $sourceHashAlgorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $sourceMetadataHash = ([BitConverter]::ToString($sourceHashAlgorithm.ComputeHash($sourceMetadataBytes))).Replace('-','').ToLowerInvariant()
    } finally { $sourceHashAlgorithm.Dispose() }
    $manifestEntries.Add([ordered]@{ path='SOURCE_BUILD_METADATA.json'; size=[int64]$sourceMetadataBytes.Length; sha256=$sourceMetadataHash })

    $manifest = [ordered]@{
        schemaVersion = 1
        product = 'OmniGhost'
        version = $version
        generatedUtc = (Get-Date).ToUniversalTime().ToString('o')
        fileCount = $manifestEntries.Count
        files = $manifestEntries
    } | ConvertTo-Json -Depth 6
    $manifestEntry = $zip.CreateEntry('SOURCE_PACKAGE_MANIFEST.json', [IO.Compression.CompressionLevel]::Optimal)
    $stream = $manifestEntry.Open()
    try {
        $writer = New-Object IO.StreamWriter($stream, (New-Object Text.UTF8Encoding($false)))
        try { $writer.Write($manifest + [Environment]::NewLine) } finally { $writer.Dispose() }
    } finally { $stream.Dispose() }
}
finally {
    $zip.Dispose()
}

$item = Get-Item -LiteralPath $OutputPath
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $OutputPath).Hash.ToLowerInvariant()
Write-Host "Source package: $OutputPath" -ForegroundColor Green
Write-Host "Files: $($manifestEntries.Count) + SOURCE_PACKAGE_MANIFEST.json Size: $($item.Length) SHA256: $sha"
