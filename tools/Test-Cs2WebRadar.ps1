[CmdletBinding()]
param([string]$ProjectDir = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($ProjectDir)
$source = Join-Path $root 'src\games\Cs2\data\webradar-src'
$dist = Join-Path $root 'src\games\Cs2\data\webradar'
$maps = 'de_mirage','de_inferno','de_dust2','de_nuke','de_ancient','de_anubis','de_vertigo','de_overpass','de_train'
foreach ($file in 'package.json','package-lock.json','LICENSE','README.md','src\app.jsx','src\app.css') {
    if (-not (Test-Path -LiteralPath (Join-Path $source $file) -PathType Leaf)) { throw "Missing source file: $file" }
}
foreach ($map in $maps) {
    $metadataPath = Join-Path $dist "data\$map\data.json"
    $radarPath = Join-Path $dist "data\$map\radar.png"
    if (-not (Test-Path -LiteralPath $radarPath -PathType Leaf)) { throw "Missing radar image: $map" }
    $metadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ($null -eq $metadata.x -or $null -eq $metadata.y -or $null -eq $metadata.scale -or [double]$metadata.scale -le 0) {
        throw "Invalid map metadata: $map"
    }
}
$index = Get-Content -LiteralPath (Join-Path $dist 'index.html') -Raw
if ($index -notmatch 'assets/index-.*\.js' -or $index -notmatch 'assets/index-.*\.css') { throw 'Vite bundle references are missing.' }
$server = Get-Content -LiteralPath (Join-Path $root 'src\games\Cs2\radar\cs2_web_radar.cpp') -Raw
foreach ($needle in 'AcquireRuntimeSnapshot','HasValidWebSocketProtocolToken','HasSafeSameOrigin','"connected":false') {
    if ($server -notmatch [regex]::Escape($needle)) { throw "Native server invariant missing: $needle" }
}
Write-Host "[CS2 Web Radar] PASS - source, production bundle, protocol and $($maps.Count) maps validated."
