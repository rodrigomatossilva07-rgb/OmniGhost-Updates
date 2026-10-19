[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectDir = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'vswhere.exe não foi encontrado.'
}

$installation = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if (-not $installation) {
    throw 'MSVC C++ não foi encontrado.'
}

$vcvars = Join-Path $installation 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars -PathType Leaf)) {
    throw "vcvars64.bat não foi encontrado: $vcvars"
}

$outputDir = Join-Path $projectDir '.cache\tests\auth-gateway'
$objectDir = Join-Path $outputDir 'obj'
New-Item -ItemType Directory -Force -Path $objectDir | Out-Null
$responseFile = Join-Path $outputDir 'compile.rsp'
$executable = Join-Path $outputDir 'auth_gateway_regression_tests.exe'
$arguments = @(
    '/nologo', '/std:c++20', '/EHsc', '/W4', '/WX',
    "/I`"$($projectDir)\src`"",
    "`"$($projectDir)\tests\auth_gateway_regression_tests.cpp`"",
    "`"$($projectDir)\src\auth\keyauth_gateway.cpp`"",
    "`"$($projectDir)\src\platform\file_integrity.cpp`"",
    "`"$($projectDir)\src\platform\text_encoding.cpp`"",
    'bcrypt.lib',
    "/Fe:`"$executable`""
)
[IO.File]::WriteAllLines($responseFile, $arguments, [Text.UTF8Encoding]::new($false))

$command = 'call "{0}" >nul && cd /d "{1}" && cl.exe @"{2}"' -f $vcvars, $objectDir, $responseFile
& $env:ComSpec /d /s /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Compilação do teste de autenticação falhou com exit code $LASTEXITCODE."
}

& $executable
if ($LASTEXITCODE -ne 0) {
    throw "Teste de autenticação falhou com exit code $LASTEXITCODE."
}

Write-Host '[Auth Gateway Test] Estado sem bloqueio e login remoto: PASS'
