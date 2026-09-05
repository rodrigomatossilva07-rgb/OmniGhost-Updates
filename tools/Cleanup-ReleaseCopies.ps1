[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'
Set-StrictMode -Version Latest

function Get-DownloadsFolder {
    $key = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders'
    $name = '{374DE290-123F-4565-9164-39C4925E467B}'
    try {
        $properties = Get-ItemProperty -LiteralPath $key -ErrorAction Stop
        $property = $properties.PSObject.Properties[$name]
        if ($property -and -not [string]::IsNullOrWhiteSpace([string]$property.Value)) {
            return [IO.Path]::GetFullPath(
                [Environment]::ExpandEnvironmentVariables([string]$property.Value))
        }
    } catch {
        Write-Verbose "[OmniGhost Cleanup] Não foi possível ler a pasta Downloads do registo: $($_.Exception.Message)"
    }
    return [IO.Path]::GetFullPath((Join-Path ([Environment]::GetFolderPath('UserProfile')) 'Downloads'))
}

function Test-SafeReleasePath {
    param([string]$Path, [string]$Downloads)
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    $full = [IO.Path]::GetFullPath($Path)
    $parent = [IO.Path]::GetFullPath((Split-Path -Parent $full))
    if (-not $parent.TrimEnd('\').Equals($Downloads.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
        return $false
    }
    $leaf = Split-Path -Leaf $full
    return $leaf -eq 'OmniGhost.zip' -or
           $leaf -eq 'OmniGhost-Release' -or
           $leaf -like 'OmniGhost-Release_*' -or
           $leaf -like '.omnighost-release-cleanup-*'
}

function Remove-ReleasePath {
    param([string]$Path, [string]$Downloads)
    if (-not (Test-Path -LiteralPath $Path)) { return $true }
    if (-not (Test-SafeReleasePath -Path $Path -Downloads $Downloads)) {
        Write-Warning "[OmniGhost Cleanup] Caminho recusado por segurança: $Path"
        return $false
    }

    # A rename on the same volume is atomic and removes the public release name
    # immediately, even while Explorer/antivirus briefly scans the contents.
    $working = $Path
    $leaf = Split-Path -Leaf $Path
    if ($leaf -notlike '.omnighost-release-cleanup-*') {
        try {
            $renamedLeaf = '.omnighost-release-cleanup-' + [guid]::NewGuid().ToString('N')
            Rename-Item -LiteralPath $Path -NewName $renamedLeaf -Force -ErrorAction Stop
            $working = Join-Path $Downloads $renamedLeaf
        } catch {
            Write-Warning "[OmniGhost Cleanup] Não foi possível renomear '$Path'; será tentada remoção direta: $($_.Exception.Message)"
            $working = $Path
        }
    }

    $lastError = $null
    for ($attempt = 1; $attempt -le 20; $attempt++) {
        try {
            if (Test-Path -LiteralPath $working -PathType Container) {
                Get-ChildItem -LiteralPath $working -Recurse -Force -ErrorAction SilentlyContinue |
                    ForEach-Object { $_.Attributes = 'Normal' }
                $item = Get-Item -LiteralPath $working -Force -ErrorAction SilentlyContinue
                if ($item) { $item.Attributes = 'Normal' }
                Remove-Item -LiteralPath $working -Recurse -Force -ErrorAction Stop
            } else {
                Remove-Item -LiteralPath $working -Force -ErrorAction Stop
            }
            if (-not (Test-Path -LiteralPath $working)) { return $true }
        } catch {
            $lastError = $_.Exception.Message
            Write-Verbose "[OmniGhost Cleanup] Tentativa $attempt falhou: $lastError"
        }
        Start-Sleep -Milliseconds (200 + ($attempt * 100))
    }

    Write-Warning "[OmniGhost Cleanup] A remoção continua bloqueada: $working. Último erro: $lastError"
    return $false
}

$downloads = Get-DownloadsFolder
if (-not (Test-Path -LiteralPath $downloads -PathType Container)) { exit 0 }

$targets = @(
    (Join-Path $downloads 'OmniGhost.zip'),
    (Join-Path $downloads 'OmniGhost-Release')
)
$targets += @(Get-ChildItem -LiteralPath $downloads -Force -ErrorAction SilentlyContinue |
    Where-Object {
        $_.Name -like 'OmniGhost-Release_*' -or
        $_.Name -like '.omnighost-release-cleanup-*'
    } | ForEach-Object FullName)

$ok = $true
foreach ($target in ($targets | Select-Object -Unique)) {
    if (-not (Remove-ReleasePath -Path $target -Downloads $downloads)) { $ok = $false }
}

if ($ok) {
    Write-Host '[OmniGhost Cleanup] Cópias temporárias das Transferências removidas.'
    exit 0
}
exit 1
