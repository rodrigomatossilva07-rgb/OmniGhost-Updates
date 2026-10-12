$script:OmniUtf8 = New-Object System.Text.UTF8Encoding -ArgumentList $false
[Console]::InputEncoding = $script:OmniUtf8
[Console]::OutputEncoding = $script:OmniUtf8
$global:OutputEncoding = $script:OmniUtf8

function Format-OmniBytes {
    param([Parameter(Mandatory = $true)][double]$Bytes)
    if ($Bytes -ge 1GB) { return ('{0:N1} GiB' -f ($Bytes / 1GB)) }
    if ($Bytes -ge 1MB) { return ('{0:N1} MiB' -f ($Bytes / 1MB)) }
    if ($Bytes -ge 1KB) { return ('{0:N1} KiB' -f ($Bytes / 1KB)) }
    return ('{0:N0} B' -f $Bytes)
}

function Format-OmniDuration {
    param([Parameter(Mandatory = $true)][TimeSpan]$Duration)
    if ($Duration.TotalMinutes -ge 1) {
        $minutes = [int][Math]::Floor($Duration.TotalMinutes)
        $remainingSeconds = $Duration.TotalSeconds - ($minutes * 60)
        return ('{0}m {1:N1}s' -f $minutes, $remainingSeconds)
    }
    return ('{0:N1}s' -f $Duration.TotalSeconds)
}

function Write-OmniPhase {
    param(
        [Parameter(Mandatory = $true)][int]$Number,
        [Parameter(Mandatory = $true)][string]$Name
    )
    Write-Host ('[OmniGhost {0:D2}/10] {1}' -f $Number, $Name)
}
