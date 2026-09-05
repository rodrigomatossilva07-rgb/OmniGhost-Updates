[CmdletBinding()]
param(
    [Parameter(Mandatory=$true, Position=0)]
    [ValidateSet('validate','build','package-release','publish-release','diagnostics','doctor','package-source','package-runtime','dependencies')]
    [string]$Action,
    [Parameter(Mandatory=$false)]
    [string]$ProjectDir = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)),
    [string]$OutputPath
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
function Invoke-Tool([string]$Name, [hashtable]$Arguments = @{}) {
    $path = Join-Path $ProjectDir ('tools\' + $Name)
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Tool missing: $Name" }
    & $path @Arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
switch ($Action) {
    'validate' { Invoke-Tool 'Validate-Project.ps1' @{ ProjectDir = $ProjectDir } }
    'build' { Invoke-Tool 'Build-Release.ps1' @{ ProjectDir = $ProjectDir } }
    'package-release' { Invoke-Tool 'Package-Release.ps1' @{ ProjectDir = $ProjectDir; Mode = 'Development' } }
    'publish-release' { throw 'Use tools\Publish-Release.ps1 directly and provide its explicit -Confirm phrase.' }
    'diagnostics' {
        $args = @{ ProjectDir = $ProjectDir }
        if ($OutputPath) { $args.OutputPath = $OutputPath }
        Invoke-Tool 'Export-Diagnostics.ps1' $args
    }
    'doctor' { Invoke-Tool 'Doctor.ps1' @{ ProjectDir = $ProjectDir } }
    'package-source' {
        $args = @{ ProjectDir = $ProjectDir }
        if ($OutputPath) { $args.OutputPath = $OutputPath }
        Invoke-Tool 'Create-SourcePackage.ps1' $args
    }
    'package-runtime' {
        $args = @{ ProjectDir = $ProjectDir }
        if ($OutputPath) { $args.Destination = $OutputPath }
        Invoke-Tool 'Create-RuntimePackage.ps1' $args
    }
    'dependencies' { Invoke-Tool 'Update-DmaDependencies.ps1' @{ Validate = $true } }
}
