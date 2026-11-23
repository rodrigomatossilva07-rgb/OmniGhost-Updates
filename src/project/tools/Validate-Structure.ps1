





















[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [switch]$ShowVerbose
)

$ErrorActionPreference = 'Stop'




$REQUIRED_DIRS = @(
    "src/auth",
    "src/config",
    "src/ferrum",
    "src/gameplay",
    "src/games/Cs2",
    "src/games/Fivem",
    "src/games/Fortnite",
    "src/games/Rust",
    "src/games/Warzone",
    "src/games/Apex",
    "src/kmbox",
    "src/launcher",
    "src/licensing",
    "src/makcu",
    "src/math",
    "src/platform",
    "src/project",
    "src/runtime",
    "src/security",
    "src/ui",
    "src/updater",
    "src/window"
)

$REQUIRED_FILES = @(
    "CMakeLists.txt",
    "CMakePresets.json",
    "vcpkg.json",
    ".clang-format",
    ".clang-tidy",
    ".editorconfig",
    ".pre-commit-config.yaml",
    "src/main.cpp",
    "src/app_version.h",
    "src/constants.h",
    "src/globals.h",
    "src/project/data/rust_offsets.json",
    "src/project/data/cs2_offsets.json",
    "src/project/data/fivem_offsets.json",
    "src/project/data/fortnite_offsets.json",
    "src/project/data/warzone_offsets.json",
    "src/project/data/apex_offsets.json"
)

$FORBIDDEN_PATTERNS = @(
    @{ Pattern = 'C:\\\\Users\\\\'; Description = "Hardcoded user path" }
    @{ Pattern = 'C:\\\\Program Files\\\\'; Description = "Hardcoded program files path" }
    @{ Pattern = 'D:\\\\'; Description = "Hardcoded drive letter" }
    @{ Pattern = 'E:\\\\'; Description = "Hardcoded drive letter" }
    @{ Pattern = '\.vcxproj\.user'; Description = "User-specific project file" }
    @{ Pattern = '\.vs\\\\'; Description = "Visual Studio cache directory" }
    @{ Pattern = 'TODO:'; Description = "TODO comment" }
    @{ Pattern = 'FIXME:'; Description = "FIXME comment" }
    @{ Pattern = 'HACK:'; Description = "HACK comment" }
    @{ Pattern = 'XXX:'; Description = "XXX comment" }
    @{ Pattern = 'TEMP:'; Description = "TEMP comment" }
)

$REQUIRED_HEADERS = @(
    "// OmniGhost - DMA Overlay for Games",
    "// Copyright (C) 2024 OmniGhost Team",
    "// Licensed under the MIT License"
)

$EXCLUDED_DIRS = @('third_party', '.cache', 'build', '.vs', '.git', 'vcpkg', 'packages', 'dumper', 'node_modules', 'target')




function Write-Log {
    param(
        [string]$Message,
        [ValidateSet('Info','Warn','Error','Pass','Fail','Debug')]
        [string]$Level = 'Info'
    )
    $prefix = switch ($Level) {
        'Info'  { '[INFO]' }
        'Warn'  { '[WARN]' }
        'Error' { '[ERROR]' }
        'Pass'  { '[PASS]' }
        'Fail'  { '[FAIL]' }
        'Debug' { '[DEBUG]' }
    }
    Write-Host "$prefix $Message"
}

function Is-Excluded {
    param([string]$Path)
    foreach ($ex in $EXCLUDED_DIRS) {
        if ($Path -like "*\$ex\*") {
            return $true
        }
    }
    return $false
}

function Get-SourceFiles {
    $files = @()
    foreach ($ext in @('.cpp', '.h', '.hpp', '.inl')) {
        $files += Get-ChildItem -Path "src" -Recurse -Filter "*$ext" -File -ErrorAction SilentlyContinue
    }
    return $files | Where-Object { -not (Is-Excluded $_.FullName) }
}

function Get-TextFiles {
    $files = @()
    foreach ($ext in @('.cpp', '.h', '.hpp', '.inl', '.json', '.md', '.txt', '.ps1', '.bat', '.cmd', '.yaml', '.yml', '.toml', '.cfg', '.ini')) {
        $files += Get-ChildItem -Path "." -Recurse -Filter "*$ext" -File -ErrorAction SilentlyContinue
    }
    return $files | Where-Object { -not (Is-Excluded $_.FullName) }
}

function Get-CrlfFiles {
    $files = @()
    foreach ($ext in @('.cpp', '.h', '.hpp', '.inl', '.ps1', '.bat', '.cmd')) {
        $files += Get-ChildItem -Path "src" -Recurse -Filter "*$ext" -File -ErrorAction SilentlyContinue
    }
    return $files | Where-Object { -not (Is-Excluded $_.FullName) }
}

function Get-JsonFiles {
    $files = Get-ChildItem -Path "." -Recurse -Filter "*.json" -File -ErrorAction SilentlyContinue
    return $files | Where-Object {
        -not (Is-Excluded $_.FullName) -and
        $_.Name -ne "package-lock.json"
    }
}




function Check-Directories {
    $errors = @()
    foreach ($dir in $REQUIRED_DIRS) {
        if (-not (Test-Path $dir -PathType Container)) {
            $errors += "Missing directory: $dir"
        }
    }
    return $errors
}

function Check-Files {
    $errors = @()
    foreach ($file in $REQUIRED_FILES) {
        if (-not (Test-Path $file -PathType Leaf)) {
            $errors += "Missing file: $file"
        }
    }
    return $errors
}

function Check-SourceFiles {
    $errors = @()
    $warnings = @()
    $files = Get-SourceFiles

    foreach ($file in $files) {
        try {
            $content = Get-Content $file.FullName -Raw -Encoding UTF8
        }
        catch {
            try {
                $content = Get-Content $file.FullName -Raw -Encoding Latin1
                $warnings += "$($file.FullName): Non-UTF-8 encoding (latin-1)"
            }
            catch {
                $warnings += "$($file.FullName): Unable to read file"
                continue
            }
        }

        $relPath = $file.FullName.Replace((Get-Location).Path + "\", "")


        foreach ($fp in $FORBIDDEN_PATTERNS) {
            if ($content -match $fp.Pattern) {
                if ($fp.Pattern -in @('TODO:', 'FIXME:', 'HACK:', 'XXX:', 'TEMP:')) {
                    $warnings += "${relPath}: Found '$($fp.Pattern)' ($($fp.Description))"
                }
                else {
                    $errors += "${relPath}: $($fp.Description) matched pattern '$($fp.Pattern)'"
                }
            }
        }


        if ($file.Extension -in @('.cpp', '.h', '.hpp', '.inl')) {
            $hasHeader = $false
            foreach ($header in $REQUIRED_HEADERS) {
                if ($content.Substring(0, [Math]::Min(500, $content.Length)) -like "*$header*") {
                    $hasHeader = $true
                    break
                }
            }
            if (-not $hasHeader) {
                $warnings += "${relPath}: Missing license header"
            }
        }


        if ($content.StartsWith([char]0xFEFF)) {
            $warnings += "${relPath}: UTF-8 BOM detected"
        }
    }
    return @($errors, $warnings)
}

function Check-Encoding {
    $warnings = @()
    $files = Get-TextFiles

    foreach ($file in $files) {
        try {
            $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
            if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
                $basePath = (Get-Location).Path
$relPath = $file.FullName.Replace($basePath + "\", "")
$warnings += "${relPath}: UTF-8 BOM detected"
            }
            if ($bytes.Length -ge 2 -and (($bytes[0] -eq 0xFF -and $bytes[1] -eq 0xFE) -or ($bytes[0] -eq 0xFE -and $bytes[1] -eq 0xFF))) {
                $basePath = (Get-Location).Path
$relPath = $file.FullName.Replace($basePath + "\", "")
$warnings += "${relPath}: UTF-16 BOM detected (should be UTF-8)"
            }
        }
        catch { }
    }
    return $warnings
}

function Check-LineEndings {
    $warnings = @()
    $files = Get-CrlfFiles

    foreach ($file in $files) {
        try {
            $content = [System.IO.File]::ReadAllBytes($file.FullName)
            $hasCrlf = $false
            $hasLf = $false
            for ($i = 0; $i -lt $content.Length - 1; $i++) {
                if ($content[$i] -eq 0x0D -and $content[$i+1] -eq 0x0A) {
                    $hasCrlf = $true
                }
                elseif ($content[$i] -eq 0x0A -and $content[$i-1] -ne 0x0D) {
                    $hasLf = $true
                }
            }
            if ($hasLf -and -not $hasCrlf) {
                $relPath = $file.FullName.Replace((Get-Location).Path + "\", "")
                $warnings += "${relPath}: LF line endings (should be CRLF)"
            }
        }
        catch { }
    }
    return $warnings
}

function Check-JsonFiles {
    $errors = @()
    $files = Get-JsonFiles

    foreach ($file in $files) {
        try {
            $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
            $hasBom = $false

            if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
                $hasBom = $true
                $basePath = (Get-Location).Path
$relPath = $file.FullName.Replace($basePath + "\", "")
$errors += "${relPath}: UTF-8 BOM in JSON file"
            }


            if ($hasBom) {
                $content = [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
            } else {
                $content = [System.Text.Encoding]::UTF8.GetString($bytes)
            }

            $json = $content | ConvertFrom-Json
        }
        catch {
            $basePath = (Get-Location).Path
$relPath = $file.FullName.Replace($basePath + "\", "")
$errors += "${relPath}: Invalid JSON - $($_.Exception.Message)"
        }
    }
    return $errors
}

function Check-CmakeConsistency {
    $warnings = @()
    $cmakeFile = "CMakeLists.txt"
    if (-not (Test-Path $cmakeFile)) {
        return @("CMakeLists.txt not found")
    }

    $content = Get-Content $cmakeFile -Raw -Encoding UTF8

    if ($content -match 'C:\\\\') {
        $warnings += "CMakeLists.txt: Contains hardcoded Windows paths"
    }
    if ($content -notmatch 'cmake_minimum_required\(VERSION 3\.25') {
        $warnings += "CMakeLists.txt: Should require CMake 3.25+"
    }
    if ($content -notmatch 'CMAKE_CXX_STANDARD 20') {
        $warnings += "CMakeLists.txt: Should set C++20 standard"
    }
    return $warnings
}

function Check-VcpkgConsistency {
    $warnings = @()
    $vcpkgFile = "vcpkg.json"
    if (-not (Test-Path $vcpkgFile)) {
        return @("vcpkg.json not found")
    }

    try {
        $content = Get-Content $vcpkgFile -Raw -Encoding UTF8
        $vcpkg = $content | ConvertFrom-Json -Depth 100

        $deps = $vcpkg.dependencies
        if ($deps) {
            $unpinned = $deps | Where-Object { $_ -is [System.Collections.Hashtable] -and $_.ContainsKey('version>=') -and -not $_.ContainsKey('version') }
            if ($unpinned.Count -gt 0) {
                $warnings += "vcpkg.json: $($unpinned.Count) dependencies use version>= without explicit version"
            }
        }

        if (-not $vcpkg.PSObject.Properties.Match('builtin-baseline')) {
            $warnings += "vcpkg.json: Missing builtin-baseline for reproducibility"
        }
    }
    catch {
        $warnings += "vcpkg.json: Error parsing - $($_.Exception.Message)"
    }
    return $warnings
}




Write-Log ("=" * 60)
Write-Log "OmniGhost Project Structure Validation"
Write-Log ("=" * 60)

$allErrors = @()
$allWarnings = @()

Write-Log "`n[1/8] Checking required directories..."
$errors = Check-Directories
$allErrors += $errors
Write-Log "  $(if ($errors.Count -eq 0) { 'PASS' } else { 'FAIL' }) ($($errors.Count) errors)"

Write-Log "`n[2/8] Checking required files..."
$errors = Check-Files
$allErrors += $errors
Write-Log "  $(if ($errors.Count -eq 0) { 'PASS' } else { 'FAIL' }) ($($errors.Count) errors)"

Write-Log "`n[3/8] Checking source files..."
$srcResults = Check-SourceFiles
$errors = $srcResults[0]
$warnings = $srcResults[1]
$allErrors += $errors
$allWarnings += $warnings
Write-Log "  $(if ($errors.Count -eq 0) { 'PASS' } else { 'WARN' }) ($($errors.Count) errors, $($warnings.Count) warnings)"

Write-Log "`n[4/8] Checking file encoding..."
$warnings = Check-Encoding
$allWarnings += $warnings
Write-Log "  $(if ($warnings.Count -eq 0) { 'PASS' } else { 'WARN' }) ($($warnings.Count) warnings)"

Write-Log "`n[5/8] Checking line endings..."
$warnings = Check-LineEndings
$allWarnings += $warnings
Write-Log "  $(if ($warnings.Count -eq 0) { 'PASS' } else { 'WARN' }) ($($warnings.Count) warnings)"

Write-Log "`n[6/8] Checking JSON files..."
$errors = Check-JsonFiles
$allErrors += $errors
Write-Log "  $(if ($errors.Count -eq 0) { 'PASS' } else { 'FAIL' }) ($($errors.Count) errors)"

Write-Log "`n[7/8] Checking CMake consistency..."
$warnings = Check-CmakeConsistency
$allWarnings += $warnings
Write-Log "  $(if ($warnings.Count -eq 0) { 'PASS' } else { 'WARN' }) ($($warnings.Count) warnings)"

Write-Log "`n[8/8] Checking vcpkg consistency..."
$warnings = Check-VcpkgConsistency
$allWarnings += $warnings
Write-Log "  $(if ($warnings.Count -eq 0) { 'PASS' } else { 'WARN' }) ($($warnings.Count) warnings)"

Write-Log ("`n" + "=" * 60)
Write-Log "SUMMARY"
Write-Log ("=" * 60)
Write-Log "Errors:   $($allErrors.Count)"
Write-Log "Warnings: $($allWarnings.Count)"

if ($allErrors.Count -gt 0) {
    Write-Log "`nERRORS:"
    foreach ($err in $allErrors) {
        Write-Log "  [ERROR] $err"
    }
}

if ($allWarnings.Count -gt 0 -and $ShowVerbose) {
    Write-Log "`nWARNINGS:"
    foreach ($warn in $allWarnings) {
        Write-Log "  [WARN] $warn"
    }
}
elseif ($allWarnings.Count -gt 0) {
    Write-Log "`n($($allWarnings.Count) warnings - run with -Verbose to see details)"
}

if ($allErrors.Count -eq 0 -and $allWarnings.Count -eq 0) {
    Write-Log "`n[PASS] All checks passed!"
    exit 0
}
elseif ($allErrors.Count -eq 0) {
    Write-Log "`n[PASS] No errors (warnings only)"
    exit 0
}
else {
    Write-Log "`n[FAIL] Validation FAILED with $($allErrors.Count) error(s)"
    exit 1
}