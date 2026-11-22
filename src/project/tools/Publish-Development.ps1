[CmdletBinding()]
param(
    [string]$ProjectDir = '',
    [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Read-EnvValue {
    param([string]$Name, [string]$EnvFile)
    $existing = [Environment]::GetEnvironmentVariable($Name)
    if (-not [string]::IsNullOrWhiteSpace($existing)) { return $existing }
    if (-not (Test-Path -LiteralPath $EnvFile -PathType Leaf)) { return $null }
    foreach ($line in Get-Content -LiteralPath $EnvFile) {
        if ($line -match ('^\s*' + [regex]::Escape($Name) + '\s*=\s*(?<value>.*)\s*$')) {
            return $Matches['value'].Trim().Trim('"').Trim("'")
        }
    }
    return $null
}

if ([string]::IsNullOrWhiteSpace($ProjectDir)) { $ProjectDir = Join-Path $PSScriptRoot '..\..\..' }
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
if ([string]::IsNullOrWhiteSpace($BuildDir)) { $BuildDir = Join-Path $ProjectDir 'build\development' }
$BuildDir = [IO.Path]::GetFullPath($BuildDir)

$websiteDir = Join-Path (Split-Path -Parent $ProjectDir) 'Site OmniGhost'
$envFile = Join-Path $websiteDir '.env.local'
$supabaseUrl = Read-EnvValue -Name 'VITE_SUPABASE_URL' -EnvFile $envFile
$serviceKey = Read-EnvValue -Name 'SUPABASE_SECRET_KEY' -EnvFile $envFile
if ([string]::IsNullOrWhiteSpace($supabaseUrl) -or [string]::IsNullOrWhiteSpace($serviceKey)) {
    throw 'SUPABASE_URL/VITE_SUPABASE_URL e SUPABASE_SECRET_KEY são obrigatórios no ambiente ou em ..\Site OmniGhost\.env.local.'
}
$supabaseUrl = $supabaseUrl.TrimEnd('/')

$versionPath = Join-Path $ProjectDir 'src\project\config\release\development-version.txt'
$version = ([IO.File]::ReadAllText($versionPath)).Trim()
if ($version -notmatch '^[0-9]+\.[0-9]+\.[0-9]+-dev\.[0-9]+$') { throw "Versão Development inválida: $version" }
$exe = Join-Path $BuildDir 'OmniGhost.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Executável Development em falta: $exe" }

$output = Join-Path $ProjectDir 'artifacts\development'
$stage = Join-Path ([IO.Path]::GetTempPath()) ('OmniGhost-Development-' + [guid]::NewGuid().ToString('N'))
try {
    New-Item -ItemType Directory -Path $output,$stage -Force | Out-Null
    Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'OmniGhost.exe') -Force
    $exeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $stage 'OmniGhost.exe')).Hash.ToLowerInvariant()
    $exeLength = (Get-Item -LiteralPath (Join-Path $stage 'OmniGhost.exe')).Length
    [IO.File]::WriteAllText((Join-Path $stage 'install-manifest.sha256'), "$exeHash`t$exeLength`tOmniGhost.exe`n", [Text.UTF8Encoding]::new($false))

    $zip = Join-Path $output 'OmniGhost.zip'
    Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [IO.Compression.CompressionLevel]::Fastest, $false)
    $zipInfo = Get-Item -LiteralPath $zip
    $zipHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $zip).Hash.ToLowerInvariant()
    $packagePath = "releases/$version/OmniGhost.zip"

    $headers = @{ apikey = $serviceKey; Authorization = "Bearer $serviceKey"; 'x-upsert' = 'true' }
    Invoke-RestMethod -Method Put -Uri "$supabaseUrl/storage/v1/object/launcher-development/$packagePath" -Headers $headers -ContentType 'application/zip' -InFile $zip | Out-Null

    $restHeaders = @{ apikey = $serviceKey; Authorization = "Bearer $serviceKey"; Prefer = 'resolution=merge-duplicates,return=minimal'; 'Content-Type' = 'application/json' }
    Invoke-RestMethod -Method Patch -Uri "$supabaseUrl/rest/v1/launcher_development_releases?active=eq.true" -Headers $restHeaders -Body '{"active":false}' | Out-Null
    $payload = [ordered]@{
        version = $version
        release_notes = 'Private Development build.'
        package_path = $packagePath
        file_name = 'OmniGhost.zip'
        file_size_bytes = [int64]$zipInfo.Length
        sha256_hash = $zipHash
        minimum_supported_version = $version
        mandatory = $true
        mandatory_reason = 'É necessária a versão Development mais recente.'
        active = $true
        published_at = [DateTime]::UtcNow.ToString('o')
    } | ConvertTo-Json -Compress
    Invoke-RestMethod -Method Post -Uri "$supabaseUrl/rest/v1/launcher_development_releases?on_conflict=version" -Headers $restHeaders -Body $payload | Out-Null
    $nextVersion = [regex]::Replace($version, '-dev\.(\d+)$', {
        param($match)
        '-dev.' + ([int]$match.Groups[1].Value + 1)
    })
    [IO.File]::WriteAllText($versionPath, "$nextVersion`n", [Text.UTF8Encoding]::new($false))
    Write-Host "[OK] Development $version enviada para o armazenamento privado."
    Write-Host "[OK] Próxima compilação Development usará $nextVersion."
    Write-Host "[OK] Pacote local: $zip"
}
finally {
    Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
}
