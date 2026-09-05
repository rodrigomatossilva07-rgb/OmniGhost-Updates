[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ProjectDir,

    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$ReleaseDir,

    [string]$WebhookUrl
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$ProjectDir = [System.IO.Path]::GetFullPath((Join-Path $ProjectDir '.'))
if ([string]::IsNullOrWhiteSpace($ReleaseDir)) {
    $ReleaseDir = Join-Path $ProjectDir 'artifacts\release'
}
$ReleaseDir = [System.IO.Path]::GetFullPath((Join-Path $ReleaseDir '.'))

function Write-DiscordLog {
    param([string]$Message)
    Write-Host "[OmniGhost Discord] $Message"
}

# Prefer explicit param, else environment (GitHub Actions secret / local env). Never log the URL.
if ([string]::IsNullOrWhiteSpace($WebhookUrl)) {
    $WebhookUrl = [string]$env:DISCORD_WEBHOOK_URL
}
if ([string]::IsNullOrWhiteSpace($WebhookUrl)) {
    Write-DiscordLog 'DISCORD_WEBHOOK_URL not set — Discord notification skipped.'
    return
}

$ChangelogPath = Join-Path $ReleaseDir 'changelog.json'
$MetaPath = Join-Path $ReleaseDir 'release-meta.json'
$NotesPath = Join-Path $ReleaseDir 'release-notes.md'

if (-not (Test-Path -LiteralPath $ChangelogPath -PathType Leaf)) {
    Write-DiscordLog "changelog.json missing at $ChangelogPath — Discord notification skipped."
    return
}

$Changelog = Get-Content -LiteralPath $ChangelogPath -Raw -Encoding UTF8 | ConvertFrom-Json
$Release = @($Changelog.releases | Where-Object { [string]$_.version -eq $Version } | Select-Object -First 1)
if (-not $Release) {
    $Release = @($Changelog.releases | Select-Object -First 1)[0]
}
if (-not $Release) {
    Write-DiscordLog 'No release entry found in changelog.json — skipped.'
    return
}

$Meta = $null
if (Test-Path -LiteralPath $MetaPath -PathType Leaf) {
    try { $Meta = Get-Content -LiteralPath $MetaPath -Raw -Encoding UTF8 | ConvertFrom-Json } catch { $Meta = $null }
}

$ReleasedLocal = if ($Meta -and $Meta.releasedLocal) { [string]$Meta.releasedLocal } else { (Get-Date).ToString('dd/MM/yyyy') }
$Tag = if ($Release.tag) { [string]$Release.tag } else { "v$Version" }

$DiscordSections = @(
    [pscustomobject]@{ types = @('added'); label = 'What''s New'; icon = [char]0x2728 },
    [pscustomobject]@{ types = @('improved', 'performance'); label = 'Improvements'; icon = [char]0x1F680 },
    [pscustomobject]@{ types = @('fixed'); label = 'Fixes'; icon = [char]0x1F527 },
    [pscustomobject]@{ types = @('compatibility'); label = 'Compatibility'; icon = [char]0x1F3AE },
    [pscustomobject]@{ types = @('security'); label = 'Security'; icon = [char]0x1F512 },
    [pscustomobject]@{ types = @('removed'); label = 'Removed'; icon = [char]0x1F5D1 },
    [pscustomobject]@{ types = @('breaking'); label = 'Breaking'; icon = '!' },
    [pscustomobject]@{ types = @('maintenance'); label = 'Maintenance'; icon = [char]0x2699 }
)

$Modules = @($Release.modules)
$MultiProduct = ($Modules | Where-Object { [string]$_.id -ne 'other' }).Count -gt 1

$DescriptionLines = New-Object System.Collections.Generic.List[string]
if (-not $MultiProduct -and $Modules.Count -eq 1) {
    $Only = $Modules[0]
    $DescriptionLines.Add(("$( [char]0x1F3AE ) Product: {0}" -f [string]$Only.name))
}
$DescriptionLines.Add(("$( [char]0x1F4E6 ) Version: {0}" -f $Tag))
$DescriptionLines.Add(("$( [char]0x1F7E2 ) Status: Updated"))
$DescriptionLines.Add('')

foreach ($Module in $Modules) {
    $ModuleName = [string]$Module.name
    if ($MultiProduct -or $Modules.Count -gt 1) {
        $DescriptionLines.Add(("**$( [char]0x1F3AE ) {0}**" -f $ModuleName))
    }
    foreach ($Section in $DiscordSections) {
        $Items = @($Module.changes | Where-Object { $Section.types -contains [string]$_.type })
        if ($Items.Count -eq 0) { continue }
        $DescriptionLines.Add(("$($Section.icon) **{0}**" -f $Section.label))
        foreach ($Item in $Items) {
            $DescriptionLines.Add(('• {0}' -f [string]$Item.text))
        }
        $DescriptionLines.Add('')
    }
}

$DescriptionLines.Add(("$( [char]0x1F4C5 ) Released: {0}" -f $ReleasedLocal))
$Description = ($DescriptionLines -join "`n").Trim()
if ($Description.Length -gt 3900) {
    $Description = $Description.Substring(0, 3900) + "`n…"
}

$Embed = [ordered]@{
    title = "$( [char]0x1F680 ) OmniGhost • Product Update"
    description = $Description
    color = 0xC9A227
    footer = [ordered]@{ text = "$( [char]0x1F47B ) OmniGhost • Updates" }
    timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
}

$Payload = @{ embeds = @($Embed) } | ConvertTo-Json -Depth 8 -Compress

try {
    # Do not print WebhookUrl. Do not include it in exceptions if avoidable.
    $Response = Invoke-RestMethod -Uri $WebhookUrl -Method Post -Body $Payload -ContentType 'application/json; charset=utf-8'
    Write-DiscordLog "Notification sent for $Tag."
}
catch {
    $Safe = $_.Exception.Message -replace 'https://discord(?:app)?\.com/api/webhooks/\S+', '[webhook redacted]'
    Write-DiscordLog "Failed to send Discord notification: $Safe"
    # Non-fatal: release already published
}
return
