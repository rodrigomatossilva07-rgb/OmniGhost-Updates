[CmdletBinding()]
param([string]$ProjectDir = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($ProjectDir)
$files = @(
  'src/games/Fivem/data/webradar/index.html',
  'src/games/Fivem/data/webradar/styles.css',
  'src/games/Fivem/data/webradar/app.js',
  'src/games/Fivem/data/webradar/manifest.json',
  'src/games/Fivem/data/webradar/sw.js',
  'src/games/Fivem/data/webradar/local_server.js',
  'src/games/Fivem/data/webradar/Abrir-Web-Radar-Local.bat',
  'src/games/Fivem/radar/fivem_web_radar.cpp',
  'src/games/Fivem/radar/fivem_web_radar.h'
)
foreach ($relative in $files) {
  $path = Join-Path $root $relative
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing Web Radar file: $relative" }
}
$js = Join-Path $root 'src/games/Fivem/data/webradar/app.js'
$node = Get-Command node -ErrorAction SilentlyContinue
if ($node) {
  & $node.Source --check $js
  if ($LASTEXITCODE -ne 0) { throw 'Web Radar JavaScript syntax validation failed.' }
}
$manifest = Get-Content -LiteralPath (Join-Path $root 'resources/embedded-resources.json') -Raw | ConvertFrom-Json
$names = @($manifest.resources.logicalName)
foreach ($name in @('fivem/webradar/index.html','fivem/webradar/styles.css','fivem/webradar/app.js',
    'fivem/webradar/manifest.json','fivem/webradar/sw.js','fivem/webradar/omnighost.png')) {
  if ($names -notcontains $name) { throw "Embedded resource is absent: $name" }
}
$source = (Get-Content -LiteralPath $js -Raw) + (Get-Content -LiteralPath (Join-Path $root 'src/games/Fivem/data/webradar/index.html') -Raw)
foreach ($required in @('new WebSocket','omnighost-radar.','heatHistory','state.heat','Satellite','Roadmap','clearLive',
    'schedule()','renderDetails','markerSig','focus-mode','commandPalette')) {
  if (-not $source.Contains($required)) { throw "Expected Web Radar behavior is absent: $required" }
}
$tileMin=-9655.77; $tileMax=12343.94; $range=20357.85; $size=32768.0
$x=215.0; $y=-810.0
$px=(($x-$tileMin)/$range)*$size; $py=(($tileMax-$y)/$range)*$size
$rx=$tileMin+($px/$size)*$range; $ry=$tileMax-($py/$size)*$range
if ([math]::Abs($rx-$x) -gt 0.0001 -or [math]::Abs($ry-$y) -gt 0.0001) { throw 'GTA coordinate round-trip failed.' }
Write-Host '[FiveM Web Radar] PASS - assets, manifest, JavaScript and coordinate conversion validated.'
