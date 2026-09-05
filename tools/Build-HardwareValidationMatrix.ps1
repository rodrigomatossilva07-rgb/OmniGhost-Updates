[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)][string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [ValidateSet('A','B','C','D','All')][string]$Case = 'All',
    [string]$PlatformToolset = 'v145'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild) { throw 'MSBuild.exe not found.' }

$cases = [ordered]@{
    A = @{ InfoDb='false'; Symbols='false' }
    B = @{ InfoDb='true';  Symbols='false' }
    C = @{ InfoDb='false'; Symbols='true'  }
    D = @{ InfoDb='true';  Symbols='true'  }
}
$selected = if ($Case -eq 'All') { @($cases.Keys) } else { @($Case) }
foreach ($name in $selected) {
    $settings = $cases[$name]
    $outDir = Join-Path $ProjectDir ".cache\hardware-matrix\$name\"
    $intDir = Join-Path $ProjectDir ".cache\hardware-matrix\obj\$name\"
    Write-Host "[HardwareMatrix] build=$name disable_infodb=$($settings.InfoDb) disable_symbols=$($settings.Symbols)"
    & $msbuild (Join-Path $ProjectDir 'OmiGhost.vcxproj') /m `
        /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=$PlatformToolset `
        /p:OutDir=$outDir /p:IntDir=$intDir /p:OmniGhostDisableVmmInfoDb=$($settings.InfoDb) `
        /p:OmniGhostDisableVmmSymbols=$($settings.Symbols)
    if ($LASTEXITCODE -ne 0) { throw "Hardware matrix build $name failed." }
    if (-not (Test-Path -LiteralPath (Join-Path $outDir 'OmniGhost.exe'))) {
        throw "Hardware matrix build $name did not produce OmniGhost.exe."
    }
}
Write-Host "[HardwareMatrix] BUILD VALIDATION PASS cases=$($selected -join ',')"
Write-Host '[HardwareMatrix] Hardware results remain NOT_TESTED until each EXE is operated with the same real adapter/game scenario.'
