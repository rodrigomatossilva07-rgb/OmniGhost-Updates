[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$false)][string]$OutputDirectory = '',
    [switch]$RequireSingleExeDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$Executable = [IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) { throw "Executable not found: $Executable" }
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path (Split-Path -Parent (Split-Path -Parent $Executable)) 'artifacts\validation\pe-runtime'
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { throw 'vswhere.exe not found.' }
$installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($installation)) { throw 'Visual C++ tools installation not found.' }
$toolsRoot = Join-Path $installation 'VC\Tools\MSVC'
$dumpbin = Get-ChildItem -LiteralPath $toolsRoot -Recurse -File -Filter dumpbin.exe |
    Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\dumpbin\.exe$' } |
    Sort-Object FullName -Descending | Select-Object -First 1
if ($null -eq $dumpbin) { throw 'dumpbin.exe not found.' }

$dependentsPath = Join-Path $OutputDirectory 'dependents.txt'
$importsPath = Join-Path $OutputDirectory 'imports.txt'
(& $dumpbin.FullName /nologo /dependents $Executable) | Set-Content -LiteralPath $dependentsPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /dependents failed.' }
(& $dumpbin.FullName /nologo /imports $Executable) | Set-Content -LiteralPath $importsPath -Encoding UTF8
if ($LASTEXITCODE -ne 0) { throw 'dumpbin /imports failed.' }

$dependentText = Get-Content -LiteralPath $dependentsPath -Raw
$dlls = @([regex]::Matches($dependentText, '(?im)^\s+([^\s]+\.dll)\s*$') | ForEach-Object {
    $_.Groups[1].Value.ToLowerInvariant()
} | Sort-Object -Unique)

if ($RequireSingleExeDirectory) {
    $directory = Split-Path -Parent $Executable
    $unexpected = @(Get-ChildItem -LiteralPath $directory -Force | Where-Object { $_.FullName -cne $Executable })
    if ($unexpected.Count -gt 0) {
        throw ('Single-EXE output contains unexpected paths: ' + (($unexpected | ForEach-Object Name) -join ', '))
    }
    foreach ($legacy in @('data','plugins','resources','libs')) {
        if (Test-Path -LiteralPath (Join-Path $directory $legacy)) { throw "Legacy output directory exists: $legacy" }
    }
}

$report = [ordered]@{
    schemaVersion = 1
    executable = $Executable
    sha256 = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    importedDlls = $dlls
    singleExeDirectory = [bool]$RequireSingleExeDirectory
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'pe-runtime.json') -Encoding UTF8
Write-Host ("[PASS] PE runtime inspected: {0} imported/delay-imported DLL name(s)." -f $dlls.Count)
