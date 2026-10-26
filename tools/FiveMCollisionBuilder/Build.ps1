[CmdletBinding()]
param(
    [string]$RageKitRoot = "C:\Users\Rodrigo Silva\Downloads\ragekit",
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$toolRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildRoot = Join-Path $toolRoot 'build'

$cmakeCandidates = @(
    (Get-Command cmake -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    'C:\Program Files\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1

if (-not $cmakeCandidates) {
    throw 'CMake não foi encontrado. Instala o componente CMake do Visual Studio ou adiciona cmake.exe ao PATH.'
}
if (-not (Test-Path -LiteralPath (Join-Path $RageKitRoot 'CMakeLists.txt'))) {
    throw "RageKit não encontrado em: $RageKitRoot"
}

& $cmakeCandidates -S $toolRoot -B $buildRoot -A x64 "-DRAGEKIT_ROOT=$RageKitRoot"
if ($LASTEXITCODE -ne 0) { throw 'A configuração CMake falhou.' }
& $cmakeCandidates --build $buildRoot --config $Configuration
if ($LASTEXITCODE -ne 0) { throw 'A compilação falhou.' }

$exe = Join-Path $buildRoot "$Configuration\FiveMCollisionBuilder.exe"
Write-Host "FiveMCollisionBuilder: $exe"

