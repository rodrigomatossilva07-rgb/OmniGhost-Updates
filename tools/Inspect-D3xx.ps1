[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

Write-Host '=== OmniGhost FTDI D3XX diagnostics ==='
foreach ($name in @('FTD3XXWU.dll','FTD3XX.dll')) {
    $path = Join-Path $root "libs\$name"
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $item = Get-Item -LiteralPath $path
        Write-Host ("{0}: file={1} version={2} sha256={3}" -f $name, $item.FullName, $item.VersionInfo.FileVersion, (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant())
    } else {
        Write-Warning "$name not found under libs\"
    }
}

Write-Host ''
Write-Host 'Present FT60x/D3XX devices and installed driver:'
$matches = @()
try {
    $matches = Get-CimInstance Win32_PnPSignedDriver | Where-Object {
        $_.DeviceName -match 'FT60|D3XX' -or $_.DeviceID -match 'VID_0403&PID_601[EF]'
    }
} catch {
    Write-Warning "Win32_PnPSignedDriver query failed: $($_.Exception.Message)"
}

if (-not $matches) {
    Write-Warning 'No obvious FT600/FT601 D3XX device was found. Connect the FPGA and run this script again.'
} else {
    $matches | Select-Object DeviceName, DeviceID, DriverProviderName, DriverVersion, InfName, DriverDate | Format-Table -AutoSize
}

Write-Host ''
Write-Host 'Expected current FTDI Windows package: D3XX WinUSB 1.4.0.6.'
Write-Host 'Important: that package ships the application library FTD3XXWU.dll 1.4.0.1; the DLL and installed driver have different version numbers.'
