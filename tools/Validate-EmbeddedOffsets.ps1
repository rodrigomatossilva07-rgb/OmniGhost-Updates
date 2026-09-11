[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$ProjectDir,
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$Configuration = 'Release',
    [string]$Platform = 'x64'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [IO.Path]::GetFullPath($ProjectDir)
$Executable = [IO.Path]::GetFullPath($Executable)
if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable"
}

$generatedRc = Join-Path $ProjectDir (".cache\generated\{0}\{1}\embedded_offsets.rc2" -f $Configuration, $Platform)
if (-not (Test-Path -LiteralPath $generatedRc -PathType Leaf)) {
    throw "Generated embedded-offset resource file is missing: $generatedRc"
}
$rc = Get-Content -LiteralPath $generatedRc -Raw
$required = @(
    @{ id = 201; name = 'FORTNITE' }, @{ id = 202; name = 'WARZONE' },
    @{ id = 203; name = 'CS2' }, @{ id = 204; name = 'RUST' },
    @{ id = 205; name = 'FIVEM' }, @{ id = 206; name = 'APEX' }
)
foreach ($entry in $required) {
    if ($rc -notmatch ("IDR_OFFSETS_{0}\s+RCDATA" -f $entry.name)) {
        throw "Generated resource list does not contain IDR_OFFSETS_$($entry.name)."
    }
}

if (-not ('OmniGhost.NativeResourceValidation' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class NativeResourceValidation {
    [DllImport("kernel32.dll", SetLastError=true, CharSet=CharSet.Unicode)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint SizeofResource(IntPtr module, IntPtr resource);
}
'@
}

$module = [NativeResourceValidation]::LoadLibraryEx($Executable, [IntPtr]::Zero, 0x00000002)
if ($module -eq [IntPtr]::Zero) {
    throw "Could not load executable resources (Win32 $([Runtime.InteropServices.Marshal]::GetLastWin32Error()))."
}
try {
    $rcData = [IntPtr]10
    foreach ($entry in $required) {
        $resource = [NativeResourceValidation]::FindResource($module, [IntPtr]$entry.id, $rcData)
        if ($resource -eq [IntPtr]::Zero) {
            throw "Executable is missing IDR_OFFSETS_$($entry.name) (resource id $($entry.id))."
        }
        if ([NativeResourceValidation]::SizeofResource($module, $resource) -lt 52) {
            throw "IDR_OFFSETS_$($entry.name) is truncated."
        }
    }
}
finally {
    [void][NativeResourceValidation]::FreeLibrary($module)
}

Write-Host "[EmbeddedOffsetsValidate] PASS executable contains all required embedded offset snapshots."
