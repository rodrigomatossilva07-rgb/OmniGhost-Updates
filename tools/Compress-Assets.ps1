# Asset Compression Build Script
# Compresses images and other assets for embedded resources

param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter()][string]$Configuration = 'Release',
    [switch]$ForceRecompress
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$ResourcesDir = Join-Path $ProjectDir 'resources'
$GamesDir = Join-Path $ResourcesDir 'games'
$RadarWebAppDir = Join-Path $ProjectDir 'Cs2\radar_webapp'
$CacheDir = Join-Path $ProjectDir ".cache\compressed\$Configuration"
$LogFile = Join-Path $CacheDir "compression.log"

New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null

function Log([string]$Message) {
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $entry = "[$timestamp] $Message"
    Write-Host $entry
    Add-Content -Path $LogFile -Value $entry -Encoding UTF8
}

function Compress-Image([string]$InputPath, [string]$OutputPath, [int]$Quality = 85) {
    if (-not (Test-Path $InputPath)) { return $false }

    $ext = [IO.Path]::GetExtension($InputPath).ToLowerInvariant()
    # System.Drawing cannot load SVG / modern WebP reliably — copy as-is.
    if ($ext -eq '.svg' -or $ext -eq '.webp' -or $ext -eq '.ico') {
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return $true
    }

    # Use .NET System.Drawing for raster compression (png/jpg/jpeg)
    Add-Type -AssemblyName System.Drawing

    try {
        $image = [System.Drawing.Image]::FromFile($InputPath)
        try {
            $encoder = [System.Drawing.Imaging.Encoder]::Quality
            $encoderParams = New-Object System.Drawing.Imaging.EncoderParameters(1)
            $encoderParams.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter($encoder, $Quality)

            $codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() |
                Where-Object { $_.MimeType -eq 'image/jpeg' } |
                Select-Object -First 1

            if ($codec -and ($ext -eq '.jpg' -or $ext -eq '.jpeg')) {
                $image.Save($OutputPath, $codec, $encoderParams)
            } else {
                # Keep PNG lossless; re-saving via default encoder is fine
                $image.Save($OutputPath, $image.RawFormat)
            }
        } finally {
            $image.Dispose()
        }
        return $true
    } catch {
        Write-Warning "Failed to compress ${InputPath}: $_"
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return $false
    }
}

function Compress-Json([string]$InputPath, [string]$OutputPath) {
    if (-not (Test-Path $InputPath)) { return $false }

    try {
        # Strip UTF-8 BOM if present, parse + re-emit compact JSON (safe minify).
        $bytes = [IO.File]::ReadAllBytes($InputPath)
        if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
            $bytes = $bytes[3..($bytes.Length - 1)]
        }
        $content = [Text.Encoding]::UTF8.GetString($bytes)
        $obj = $content | ConvertFrom-Json
        $minified = $obj | ConvertTo-Json -Compress -Depth 100
        $utf8NoBom = New-Object System.Text.UTF8Encoding $false
        [IO.File]::WriteAllText($OutputPath, $minified, $utf8NoBom)
        return $true
    } catch {
        Write-Warning "Failed to minify JSON ${InputPath}: $_"
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return $false
    }
}

function Compress-Css([string]$InputPath, [string]$OutputPath) {
    if (-not (Test-Path $InputPath)) { return $false }
    
    try {
        $content = Get-Content -Path $InputPath -Raw -Encoding UTF8
        $minified = $content -replace '(?s)/\*.*?\*/', '' -replace '\s+', ' ' -replace '\s*([{};:,])\s*', '$1'
        $minified = $minified.Trim()
        Set-Content -Path $OutputPath -Value $minified -Encoding UTF8 -NoNewline
        return $true
    } catch {
        Write-Warning "Failed to minify CSS ${InputPath}: $_"
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return $false
    }
}

function Compress-Js([string]$InputPath, [string]$OutputPath) {
    if (-not (Test-Path $InputPath)) { return $false }
    
    try {
        $content = Get-Content -Path $InputPath -Raw -Encoding UTF8
        # Basic JS minification
        $minified = $content -replace '(?s)/\*.*?\*/', '' -replace '(?m)^\s*//.*$', '' -replace '\s+', ' ' -replace '\s*([{}();,])\s*', '$1'
        $minified = $minified.Trim()
        Set-Content -Path $OutputPath -Value $minified -Encoding UTF8 -NoNewline
        return $true
    } catch {
        Write-Warning "Failed to minify JS ${InputPath}: $_"
        Copy-Item -Path $InputPath -Destination $OutputPath -Force
        return $false
    }
}

function Get-CacheKey([string]$FilePath) {
    $hash = Get-FileHash -Path $FilePath -Algorithm SHA256
    return $hash.Hash.ToLower()
}

function Should-Compress([string]$SourcePath, [string]$DestPath) {
    if ($ForceRecompress) { return $true }
    if (-not (Test-Path $DestPath)) { return $true }
    
    $sourceHash = Get-CacheKey $SourcePath
    $cacheKeyPath = Join-Path $CacheDir (($sourceHash) + '.key')
    
    if (Test-Path $cacheKeyPath) {
        $cachedHash = (Get-Content -Path $cacheKeyPath -Raw).Trim()
        if ($cachedHash -eq $sourceHash) {
            return $false
        }
    }
    return $true
}

function Update-CacheKey([string]$FilePath) {
    $hash = Get-CacheKey $FilePath
    $cacheKeyPath = Join-Path $CacheDir (($hash) + '.key')
    Set-Content -Path $cacheKeyPath -Value $hash -Encoding UTF8
}

# Main compression logic
Log "Starting asset compression for $Configuration"
$compressedCount = 0
$skippedCount = 0
$totalSaved = 0

# Process game resources
$imageExtensions = @('.png', '.jpg', '.jpeg', '.webp', '.svg', '.ico')
$compressExtensions = @('.json', '.json5', '.css', '.js', '.html', '.htm')

# Game resources
if (Test-Path $GamesDir) {
    $files = Get-ChildItem -Path $GamesDir -Recurse -File | Where-Object { $imageExtensions -contains $_.Extension.ToLower() }
    foreach ($file in $files) {
        $relativePath = $file.FullName.Substring($ResourcesDir.Length + 1)
        $destPath = Join-Path $CacheDir $relativePath
        
        if (Should-Compress $file.FullName $destPath) {
            $origSize = $file.Length
            $destDir = Split-Path -Parent $destPath
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            
            if (Compress-Image $file.FullName $destPath 85) {
                $newSize = (Get-Item $destPath).Length
                $saved = $origSize - $newSize
                $totalSaved += $saved
                $compressedCount++
                Log "Compressed image: $relativePath ($origSize -> $newSize, saved $saved bytes)"
                Update-CacheKey $file.FullName
            }
        } else {
            $skippedCount++
        }
    }
}

# Radar webapp
if (Test-Path $RadarWebAppDir) {
    $files = Get-ChildItem -Path $RadarWebAppDir -Recurse -File
    foreach ($file in $files) {
        $ext = $file.Extension.ToLower()
        $relativePath = $file.FullName.Substring($ProjectDir.Length + 1)
        $destPath = Join-Path $CacheDir $relativePath
        
        if (Should-Compress $file.FullName $destPath) {
            $origSize = $file.Length
            $destDir = Split-Path -Parent $destPath
            New-Item -ItemType Directory -Path $destDir -Force | Out-Null
            
            $success = $false
            if ($imageExtensions -contains $ext) {
                $success = Compress-Image $file.FullName $destPath 85
            } elseif ($ext -eq '.json' -or $ext -eq '.json5') {
                $success = Compress-Json $file.FullName $destPath
            } elseif ($ext -eq '.css') {
                $success = Compress-Css $file.FullName $destPath
            } elseif ($ext -eq '.js') {
                $success = Compress-Js $file.FullName $destPath
            } else {
                Copy-Item -Path $file.FullName -Destination $destPath -Force
                $success = $true
            }
            
            if ($success) {
                $newSize = (Get-Item $destPath).Length
                $saved = $origSize - $newSize
                if ($saved -gt 0) {
                    $totalSaved += $saved
                    $compressedCount++
                    Log "Compressed ${ext}: $relativePath ($origSize -> $newSize, saved $saved bytes)"
                } else {
                    $skippedCount++
                }
                Update-CacheKey $file.FullName
            }
        } else {
            $skippedCount++
        }
    }
}

Log "Compression complete: $compressedCount files compressed, $skippedCount skipped, $totalSaved bytes saved"
Log "Compressed assets available at: $CacheDir"

# Copy compressed assets to build output for embedding
    # Skip for Publish builds - assets are embedded via resource system
    if ($Configuration -ine 'Publish') {
        $buildDir = Join-Path $ProjectDir "build\$Configuration"
        if (Test-Path $buildDir) {
            $assetsDir = Join-Path $buildDir "data\compressed"
            New-Item -ItemType Directory -Path $assetsDir -Force | Out-Null
            
            # Copy compressed assets
            Copy-Item -Path (Join-Path $CacheDir '*') -Destination $assetsDir -Recurse -Force
            Log "Copied compressed assets to $assetsDir for embedding"
        }
    } else {
        Log "Publish build: skipping copy of compressed assets to build output (assets embedded via resource system)"
    }

exit 0