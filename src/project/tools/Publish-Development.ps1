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

function Upload-SupabaseMultipart {
    param(
        [Parameter(Mandatory=$true)][string]$SupabaseUrl,
        [Parameter(Mandatory=$true)][string]$ServiceKey,
        [Parameter(Mandatory=$true)][string]$Bucket,
        [Parameter(Mandatory=$true)][string]$ObjectPath,
        [Parameter(Mandatory=$true)][string]$FilePath
    )


    $projectRef = $SupabaseUrl -replace '^https?://([^.]+)\..*$', '$1'
    $s3Endpoint = "https://$projectRef.supabase.co/storage/v1/s3"
    $authHeaders = @{ Authorization = "Bearer $ServiceKey" }
    $chunkSize = 50MB

    try {

        $createUrl = $s3Endpoint + '/' + $Bucket + '/' + $ObjectPath + '?uploads'
        $createHeaders = $authHeaders + @{ 'Content-Type' = 'application/xml' }
        $createBody = ''
        $createResp = Invoke-RestMethod -Method Post -Uri $createUrl -Headers $createHeaders -Body $createBody -ErrorAction Stop
        $xml = [xml]$createResp
        $uploadId = $xml.InitiateMultipartUploadResult.UploadId
        if (-not $uploadId) { throw "Não foi possível obter uploadId" }
        Write-Host "[OmniGhost] Multipart upload iniciado: $uploadId"


        $fileStream = [IO.File]::OpenRead($FilePath)
        $totalSize = $fileStream.Length
        $partNumber = 1
        $parts = @()

        while ($fileStream.Position -lt $totalSize) {
            $remaining = $totalSize - $fileStream.Position
            $readSize = [Math]::Min($chunkSize, $remaining)
            $buffer = New-Object byte[] $readSize
            $bytesRead = $fileStream.Read($buffer, 0, $readSize)
            if ($bytesRead -le 0) { break }


            $partUrl = $s3Endpoint + '/' + $Bucket + '/' + $ObjectPath + '?partNumber=' + $partNumber + '&uploadId=' + $uploadId
            $partHeaders = $authHeaders + @{ 'Content-Type' = 'application/octet-stream' }
            $partStream = New-Object IO.MemoryStream (,$buffer)
            $partResp = Invoke-RestMethod -Method Put -Uri $partUrl -Headers $partHeaders -InFile $partStream -ErrorAction Stop
            $etag = $partResp.ETag -replace '"', ''
            if (-not $etag) { throw "ETag não retornado para parte $partNumber" }
            $parts += @{ PartNumber = $partNumber; ETag = $etag }
            Write-Host "[OmniGhost] Parte $partNumber upload concluída (ETag: $etag)"

            $partNumber++
        }
        $fileStream.Close()


        $completeUrl = $s3Endpoint + '/' + $Bucket + '/' + $ObjectPath + '?uploadId=' + $uploadId
        $completeHeaders = $authHeaders + @{ 'Content-Type' = 'application/xml' }
        $completeXml = @"
<?xml version="1.0" encoding="UTF-8"?>
<CompleteMultipartUpload xmlns="http://s3.amazonaws.com/doc/2006-03-01/">
    <Part>
        <PartNumber>1</PartNumber>
        <ETag>$($parts[0].ETag)</ETag>
    </Part>
"@
        if ($parts.Count -gt 1) {
            for ($i = 1; $i -lt $parts.Count; $i++) {
                $completeXml += @"
    <Part>
        <PartNumber>$($parts[$i].PartNumber)</PartNumber>
        <ETag>$($parts[$i].ETag)</ETag>
    </Part>
"@
            }
        }
        $completeXml += "</CompleteMultipartUpload>"
        $completeResp = Invoke-RestMethod -Method Post -Uri $completeUrl -Headers $completeHeaders -Body $completeXml -ErrorAction Stop

        return @{ Success = $true; Result = $completeResp }
    }
    catch {
        return @{ Success = $false; Error = $_.Exception.Message }
    }
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

    Write-Host "[OmniGhost] A fazer upload multipart para Supabase Storage..."
    $uploadResult = Upload-SupabaseMultipart -SupabaseUrl $supabaseUrl -ServiceKey $serviceKey -Bucket 'launcher-development' -ObjectPath $packagePath -FilePath $zip
    if (-not $uploadResult.Success) {
        throw "Falha no upload multipart: $($uploadResult.Error)"
    }
    Write-Host "[OK] Upload multipart concluído."

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
