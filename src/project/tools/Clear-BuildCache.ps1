[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [ValidateSet('All', 'Configuration')]
    [string]$Scope = 'All',

    [string]$Configuration = '',

    [switch]$RemoveArtifacts,

    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = [IO.Path]::GetFullPath($ProjectDir)
$cacheRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot '.cache'))
$artifactsRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'artifacts'))
$target = $cacheRoot

function Stop-ProjectBuildProcesses {
    param([string]$Root, [string]$Cache)

    # Only terminate build tools that were started for this exact repository.
    # This deliberately does not touch Visual Studio, Explorer or builds from
    # other projects.
    $all = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue)
    $buildNames = @('cmake.exe', 'msbuild.exe', 'cl.exe', 'link.exe', 'rc.exe')
    $ids = [Collections.Generic.HashSet[int]]::new()
    foreach ($process in $all) {
        if ($buildNames -notcontains $process.Name.ToLowerInvariant()) { continue }
        $command = [string]$process.CommandLine
        if ($command.IndexOf($Root, [StringComparison]::OrdinalIgnoreCase) -ge 0 -or
            $command.IndexOf($Cache, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
            [void]$ids.Add([int]$process.ProcessId)
        }
    }

    # Compiler/linker children do not always retain the project path in their
    # command line, so include descendants of an already identified CMake or
    # MSBuild process as well.
    $added = $true
    while ($added) {
        $added = $false
        foreach ($process in $all) {
            if ($buildNames -notcontains $process.Name.ToLowerInvariant()) { continue }
            if ($ids.Contains([int]$process.ParentProcessId) -and $ids.Add([int]$process.ProcessId)) {
                $added = $true
            }
        }
    }

    foreach ($id in $ids) {
        try {
            Stop-Process -Id $id -Force -ErrorAction Stop
            Write-Host "[INFO] Processo de compilacao do projeto terminado: PID $id"
        }
        catch {
            # A process can finish naturally between discovery and termination.
        }
    }
}

if ($Scope -eq 'Configuration') {
    if ([string]::IsNullOrWhiteSpace($Configuration) -or
        $Configuration -notmatch '^(Debug|Release|Development|Publish)$') {
        throw 'Uma configuracao CMake valida e obrigatoria para limpeza parcial.'
    }
    $target = [IO.Path]::GetFullPath((Join-Path $cacheRoot (Join-Path 'cmake' $Configuration)))
}

$cachePrefix = $cacheRoot.TrimEnd('\') + '\'
if (-not $target.StartsWith($cachePrefix, [StringComparison]::OrdinalIgnoreCase) -and
    -not $target.Equals($cacheRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Destino de limpeza inválido: $target"
}

$locksDirectory = Join-Path $cacheRoot 'locks'
$locks = @()
if (Test-Path -LiteralPath $locksDirectory -PathType Container) {
    $locks = @(Get-ChildItem -LiteralPath $locksDirectory -Directory -Filter 'build-*.lock' -ErrorAction SilentlyContinue)
}
if ($Scope -eq 'All' -and $locks.Count -gt 0 -and -not $Force) {
    $names = $locks.Name -join ', '
    throw "Existe uma compilacao possivelmente ativa ($names). Fecha-a ou executa 'build.bat clean force' apenas se tiver ficado bloqueada."
}
if ($Force) {
    Stop-ProjectBuildProcesses -Root $projectRoot -Cache $cacheRoot
    Start-Sleep -Milliseconds 300
}

if (-not (Test-Path -LiteralPath $target)) {
    Write-Host "[OK] Nao existe cache para limpar: $target"
}
else {
    $cacheRemoved = $false
    for ($attempt = 1; $attempt -le 5; ++$attempt) {
        try {
            # Antivirus e ferramentas de indexacao podem deixar atributos de so
            # leitura nos .tlog/.obj. Normaliza-los antes de remover reduz falhas
            # de limpeza depois de builds interrompidas.
            Get-ChildItem -LiteralPath $target -Force -Recurse -ErrorAction SilentlyContinue |
                ForEach-Object { $_.Attributes = [IO.FileAttributes]::Normal }
            (Get-Item -LiteralPath $target -Force).Attributes = [IO.FileAttributes]::Normal
            Remove-Item -LiteralPath $target -Force -Recurse -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $target)) {
                Write-Host "[OK] Cache removida: $target"
                $cacheRemoved = $true
                break
            }
        }
        catch {
            if ($attempt -eq 5) {
                throw "Nao foi possivel remover '$target' apos 5 tentativas. Fecha Visual Studio, Explorer aberto nessa pasta e qualquer build em execucao. Detalhe: $($_.Exception.Message)"
            }
            Start-Sleep -Milliseconds (350 * $attempt)
        }
    }
    if (-not $cacheRemoved) {
        throw "A limpeza nao foi concluida: $target"
    }
}

if ($RemoveArtifacts -and (Test-Path -LiteralPath $artifactsRoot)) {
    for ($attempt = 1; $attempt -le 5; ++$attempt) {
        try {
            Get-ChildItem -LiteralPath $artifactsRoot -Force -Recurse -ErrorAction SilentlyContinue |
                ForEach-Object { $_.Attributes = [IO.FileAttributes]::Normal }
            (Get-Item -LiteralPath $artifactsRoot -Force).Attributes = [IO.FileAttributes]::Normal
            Remove-Item -LiteralPath $artifactsRoot -Force -Recurse -ErrorAction Stop
            if (-not (Test-Path -LiteralPath $artifactsRoot)) {
                Write-Host "[OK] Artifacts removidos: $artifactsRoot"
                break
            }
        }
        catch {
            if ($attempt -eq 5) {
                throw "Nao foi possivel remover '$artifactsRoot' apos 5 tentativas. Fecha qualquer processo que esteja a usar ficheiros de release. Detalhe: $($_.Exception.Message)"
            }
            Start-Sleep -Milliseconds (350 * $attempt)
        }
    }
}
