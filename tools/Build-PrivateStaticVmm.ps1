[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)]
    [string]$ProjectDir,
    [ValidatePattern('^v[0-9]+$')]
    [string]$PlatformToolset = 'v145'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$leechRoot = Join-Path $ProjectDir 'third_party\upstream\LeechCore-v2.23'
$vmmRoot = Join-Path $ProjectDir 'third_party\upstream\MemProcFS-v5.17'
$leechProject = Join-Path $leechRoot 'leechcore\leechcore.vcxproj'
$vmmProject = Join-Path $vmmRoot 'vmm\vmm.vcxproj'
$leechLicense = Join-Path $leechRoot 'LICENSE'
$vmmLicense = Join-Path $vmmRoot 'LICENSE'
$outputRoot = Join-Path $ProjectDir '.cache\private-static'
$stampPath = Join-Path $outputRoot 'build-fingerprint.json'

foreach ($required in @($leechProject, $vmmProject, $leechLicense, $vmmLicense)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Private static source/license input is missing: $required"
    }
}

$msbuild = $null
if (-not [string]::IsNullOrWhiteSpace($env:MSBuildToolsPath)) {
    $candidate = Join-Path $env:MSBuildToolsPath 'MSBuild.exe'
    if (Test-Path -LiteralPath $candidate -PathType Leaf) { $msbuild = $candidate }
}
if ($null -eq $msbuild) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
            Select-Object -First 1
    }
}
if ([string]::IsNullOrWhiteSpace($msbuild) -or -not (Test-Path -LiteralPath $msbuild -PathType Leaf)) {
    throw 'MSBuild.exe was not found for the private static VMM prototype.'
}

function Get-TreeFingerprint {
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $inputs = @(
            Get-ChildItem -LiteralPath $leechRoot,$vmmRoot -Recurse -File |
                Where-Object { $_.Extension -in @('.c','.cpp','.h','.hpp','.vcxproj','.props','.targets','.def','.rc') -or $_.Name -eq 'LICENSE' }
            Get-Item -LiteralPath (Join-Path $ProjectDir 'tools\LeechCoreStaticOverrides.props')
            Get-Item -LiteralPath (Join-Path $ProjectDir 'tools\VmmStaticOverrides.props')
        ) | Sort-Object FullName
        foreach ($input in $inputs) {
            $relative = $input.FullName.Substring($ProjectDir.Length).TrimStart('\').Replace('\','/').ToLowerInvariant()
            $nameBytes = [Text.Encoding]::UTF8.GetBytes($relative + "`n")
            [void]$sha.TransformBlock($nameBytes, 0, $nameBytes.Length, $nameBytes, 0)
            $bytes = [IO.File]::ReadAllBytes($input.FullName)
            [void]$sha.TransformBlock($bytes, 0, $bytes.Length, $bytes, 0)
        }
        $identity = [Text.Encoding]::UTF8.GetBytes("toolset=$PlatformToolset`nmsbuild=$msbuild`n")
        [void]$sha.TransformFinalBlock($identity, 0, $identity.Length)
        return ([BitConverter]::ToString($sha.Hash)).Replace('-','')
    }
    finally { $sha.Dispose() }
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$fingerprint = Get-TreeFingerprint
$cachedLeech = Join-Path $outputRoot 'leechcore\leechcore.lib'
$cachedVmm = Join-Path $outputRoot 'vmm\vmm.lib'
if ((Test-Path -LiteralPath $stampPath -PathType Leaf) -and
    (Test-Path -LiteralPath $cachedLeech -PathType Leaf) -and
    (Test-Path -LiteralPath $cachedVmm -PathType Leaf)) {
    try {
        $stamp = Get-Content -LiteralPath $stampPath -Raw | ConvertFrom-Json
        if ([string]$stamp.fingerprint -ceq $fingerprint -and
            [string]$stamp.toolset -ceq $PlatformToolset) {
            Write-Host "[PrivateStatic] cache=HIT fingerprint=$($fingerprint.Substring(0,16)) toolset=$PlatformToolset"
            Write-Host '[PrivateStatic] LeechCore/VMM source libraries: PASS (incremental cache)'
            exit 0
        }
    } catch {
        Write-Host '[PrivateStatic] cache stamp is invalid; rebuilding.'
    }
}
Write-Host "[PrivateStatic] cache=MISS fingerprint=$($fingerprint.Substring(0,16)) toolset=$PlatformToolset"

function Build-StaticLibrary(
    [string]$Project,
    [string]$Name,
    [string]$Overrides
) {
    $outDir = Join-Path $outputRoot "$Name\"
    $intDir = Join-Path $outputRoot "obj\$Name\"
    New-Item -ItemType Directory -Path $outDir,$intDir -Force | Out-Null
    $arguments = @(
        $Project,
        '/m',
        '/t:Build',
        '/p:Configuration=Release',
        '/p:Platform=x64',
        "/p:PlatformToolset=$PlatformToolset",
        '/p:ConfigurationType=StaticLibrary',
        "/p:OutDir=$outDir",
        "/p:IntDir=$intDir",
        "/p:ForceImportBeforeCppTargets=$Overrides",
        '/v:minimal'
    )
    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Name static source build failed with exit code $LASTEXITCODE."
    }
    $library = Join-Path $outDir "$Name.lib"
    if (-not (Test-Path -LiteralPath $library -PathType Leaf)) {
        throw "$Name static library was not produced: $library"
    }
    Write-Host "[PrivateStatic] $Name => $library"
}

Write-Host "[PrivateStatic] PRIVATE DEVELOPMENT BUILD - NOT FOR DISTRIBUTION (toolset=$PlatformToolset)"
Build-StaticLibrary $leechProject 'leechcore' (Join-Path $ProjectDir 'tools\LeechCoreStaticOverrides.props')
Build-StaticLibrary $vmmProject 'vmm' (Join-Path $ProjectDir 'tools\VmmStaticOverrides.props')
$stamp = [ordered]@{
    schemaVersion = 1
    fingerprint = $fingerprint
    toolset = $PlatformToolset
    msbuild = $msbuild
}
$temporaryStamp = "$stampPath.tmp-$PID"
$stamp | ConvertTo-Json | Set-Content -LiteralPath $temporaryStamp -Encoding UTF8
Move-Item -LiteralPath $temporaryStamp -Destination $stampPath -Force
Write-Host '[PrivateStatic] LeechCore/VMM source libraries: PASS'
