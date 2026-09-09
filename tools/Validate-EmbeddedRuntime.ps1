[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$ProjectDir,
    [Parameter(Mandatory=$true)][string]$Configuration,
    [Parameter(Mandatory=$false)][string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$generated = Join-Path $ProjectDir (".cache\generated\$Configuration\$Platform")
$header = Join-Path $generated 'embedded_runtime_manifest.h'
$rc = Join-Path $generated 'embedded_runtime.rc2'

if ($Configuration -ieq 'PrivateStatic') {
    Write-Host "[EmbeddedRuntimeValidate] PrivateStatic: vmm/leechcore are linked statically; DLL embedding check skipped."
    exit 0
}

foreach ($path in @($header, $rc)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Embedded runtime output is missing: $path"
    }
}

$headerText = Get-Content -LiteralPath $header -Raw
$rcText = Get-Content -LiteralPath $rc -Raw

$required = @('libs/vmm.dll','libs/leechcore.dll')
foreach ($name in $required) {
    if ($headerText -notmatch [regex]::Escape($name)) {
        throw "Generated embedded runtime manifest does not contain $name. The build would produce an EXE that cannot validate its DMA runtime."
    }
    if ($rcText -notmatch [regex]::Escape($name)) {
        # RC stores the absolute source path, so verify the filename rather than the
        # generated relative logical path.
        $leaf = Split-Path $name -Leaf
        if ($rcText -notmatch [regex]::Escape($leaf)) {
            throw "Generated embedded runtime resource list does not contain $leaf."
        }
    }
}

if ($headerText -match 'kEntryCount\s*=\s*0') {
    throw "Generated embedded runtime manifest is empty for $Configuration|$Platform."
}

Write-Host "[EmbeddedRuntimeValidate] PASS configuration=$Configuration platform=$Platform required DMA DLLs embedded."
