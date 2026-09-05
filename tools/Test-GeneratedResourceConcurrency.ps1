[CmdletBinding()]
param(
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $PSCommandPath))
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$artifactDir = Join-Path $ProjectDir 'artifacts\validation\resource-concurrency'
New-Item -ItemType Directory -Path $artifactDir -Force | Out-Null

function Invoke-ConcurrentGenerator([string]$Name, [string]$Script, [string[]]$ExtraArguments, [string[]]$Outputs) {
    $processes = @()
    foreach ($index in 1..2) {
        $stdout = Join-Path $artifactDir "$Name-$index.stdout.txt"
        $stderr = Join-Path $artifactDir "$Name-$index.stderr.txt"
        $arguments = @('-NoProfile','-ExecutionPolicy','Bypass','-File',$Script,'-ProjectDir',$ProjectDir,'-Configuration','ConcurrencyTest','-Architecture','x64') + $ExtraArguments
        $processes += Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -PassThru -WindowStyle Hidden `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    }
    $processes | Wait-Process
    foreach ($process in $processes) {
        if ($process.ExitCode -ne 0) {
            throw "$Name concurrent generator failed with exit code $($process.ExitCode)."
        }
    }
    $concurrentHashes = @{}
    foreach ($relative in $Outputs) {
        $path = Join-Path $ProjectDir $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "$Name did not create $relative." }
        $concurrentHashes[$relative] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    }

    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Script -ProjectDir $ProjectDir -Configuration ConcurrencyTest @ExtraArguments
    if ($LASTEXITCODE -ne 0) { throw "$Name deterministic verification run failed." }
    foreach ($relative in $Outputs) {
        $actual = (Get-FileHash -LiteralPath (Join-Path $ProjectDir $relative) -Algorithm SHA256).Hash
        if ($actual -cne $concurrentHashes[$relative]) {
            throw "$Name output is not deterministic: $relative"
        }
    }
    Write-Host "[PASS] $Name generators serialize safely and produce deterministic output."
}

Invoke-ConcurrentGenerator 'offsets' (Join-Path $ProjectDir 'tools\Build-EmbeddedOffsets.ps1') @() @(
'.cache\generated\ConcurrencyTest\x64\embedded_offsets.rc2'
    )
    Invoke-ConcurrentGenerator 'resources' (Join-Path $ProjectDir 'tools\Build-EmbeddedResources.ps1') @() @(
        '.cache\generated\ConcurrencyTest\x64\embedded_resource_catalog.h',
    '.cache\generated\ConcurrencyTest\x64\embedded_resources.rc2'
)
Invoke-ConcurrentGenerator 'runtime' (Join-Path $ProjectDir 'tools\Build-EmbeddedRuntime.ps1') @() @(
    '.cache\generated\ConcurrencyTest\x64\embedded_runtime_manifest.h',
    '.cache\generated\ConcurrencyTest\x64\embedded_runtime.rc2'
)

Write-Host '[PASS] Embedded resource concurrency validation completed.'
