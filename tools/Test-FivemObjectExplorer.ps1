[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vcvarsCandidates = @(
    'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
$vcvars = $vcvarsCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1
if (-not $vcvars) { throw 'vcvars64.bat não foi encontrado.' }

$outputDir = Join-Path $projectDir '.cache\tests\fivem-object-explorer'
$objectDir = Join-Path $outputDir 'obj'
New-Item -ItemType Directory -Force -Path $objectDir | Out-Null
$responseFile = Join-Path $outputDir 'compile.rsp'
$executable = Join-Path $outputDir 'fivem_object_explorer_tests.exe'
$arguments = @(
    '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX',
    "/Fo`"$objectDir/`"",
    "/I`"$projectDir\src`"",
    "`"$projectDir\tests\fivem_object_explorer_tests.cpp`"",
    "`"$projectDir\src\games\Fivem\objects\object_explorer.cpp`"",
    "/Fe:`"$executable`""
)
[IO.File]::WriteAllLines($responseFile, $arguments, [Text.UTF8Encoding]::new($false))

$command = 'call "{0}" >nul && cd /d "{1}" && cl.exe @"{2}"' -f $vcvars, $objectDir, $responseFile
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) { throw "Compilação dos testes falhou com exit code $LASTEXITCODE." }

& $executable
if ($LASTEXITCODE -ne 0) { throw "Testes falharam com exit code $LASTEXITCODE." }

Write-Host '[FiveM Object Explorer Tests] Favoritos, visibilidade, ignorados, parsing e pesquisa: PASS'
