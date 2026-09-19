[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,
    [switch]$Retry
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
$targets = @('.cache', 'artifacts', 'build')
$failed = $false

if ($Retry) {
    # MSBuild itself owns the intermediate log until its final target exits.
    # This detached retry deliberately runs after that handle is released.
    Start-Sleep -Seconds 4
}

foreach ($name in $targets) {
    $target = [IO.Path]::GetFullPath((Join-Path $ProjectDir $name))
    $parent = [IO.Path]::GetDirectoryName($target)
    if (-not $parent.Equals($ProjectDir, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($target) -ne $name) {
        throw "Recusada limpeza fora da raiz do projeto: $target"
    }
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "[OmniGhost Build] Nada para limpar: $name"
        continue
    }
    Write-Host "[OmniGhost Build] A limpar ficheiros temporários: $name"
    try {
        Remove-Item -LiteralPath $target -Recurse -Force
    } catch {
        $failed = $true
        Write-Warning "Ainda não foi possível limpar ${name}: $($_.Exception.Message)"
    }
}

if ($failed -and -not $Retry) {
    $powerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $arguments = @(
        '-NoLogo', '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
        '-File', $PSCommandPath, '-ProjectDir', $ProjectDir, '-Retry'
    )
    Start-Process -FilePath $powerShell -ArgumentList $arguments -WindowStyle Hidden
    Write-Host '[OmniGhost Build] Alguns ficheiros ainda estavam bloqueados; limpeza repetida em segundo plano.'
    exit 0
}

if ($failed) {
    Write-Warning '[OmniGhost Build] Restaram ficheiros bloqueados após a repetição; serão removidos na próxima build.'
} else {
    Write-Host '[OmniGhost Build] Limpeza de workspace concluída.'
}
