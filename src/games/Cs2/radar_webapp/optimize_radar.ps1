


param(
    [Parameter(Mandatory=$true)][string]$RadarDir,
    [switch]$ForceReprocess
)

$ErrorActionPreference = 'Stop'
$RadarDir = [IO.Path]::GetFullPath($RadarDir)
$MapsDir = Join-Path $RadarDir 'maps'
$ImgDir = Join-Path $RadarDir 'img'
$RenderersDir = Join-Path $RadarDir 'renderers'
$CssDir = Join-Path $RadarDir 'css'

function Log([string]$Message) {
    Write-Host "[Radar Optimizer] $Message"
}

function Optimize-MapAssets {
    # Os metadados dos mapas são dados de runtime. A antiga substituição por
    # regex inseria propriedades em todos os objetos e podia corromper JSON5
    # válido. As imagens já são distribuídas no formato consumido pelo radar.
    Log "Map assets kept byte-for-byte (safe mode)."
}

function Add-LazyLoadingToIndex {
    Log "Adding lazy loading to index.html..."

    $indexPath = Join-Path $RadarDir 'index.html'
    if (-not (Test-Path $indexPath)) {
        Write-Warning "index.html not found"
        return
    }

    $html = Get-Content -Path $indexPath -Raw -Encoding UTF8


    if ($html -notmatch 'rel="preload"') {
        $preload = @'
    <link rel="preload" as="image" href="img/icon-64x64.png">
    <link rel="preload" as="style" href="css/main.css">
    <link rel="preload" as="script" href="renderers/_init.js">
'@
        $html = $html -replace '(</head>)', "$preload`n$1"
    }


    if ($html -notmatch 'loading="lazy"') {
        $html = $html -replace '<img([^>]*src="img/[^"]*")', '<img$1 loading="lazy"'
    }


    if ($html -notmatch 'defer') {
        $html = $html -replace '<script src="(renderers/[^"]+\.js)"', '<script src="$1" defer'
    }

    Set-Content -Path $indexPath -Value $html -Encoding UTF8
    Log "Updated index.html with lazy loading optimizations"
}

function Optimize-Renderers {
    # Não envolver nem minificar JavaScript através de regex. O antigo wrapper
    # alterava a ordem de inicialização e podia impedir o arranque do frontend.
    Log "Renderer scripts kept byte-for-byte (safe mode)."
}

function Optimize-CSS {
    Log "Optimizing CSS..."

    $files = Get-ChildItem -Path $CssDir -Filter '*.css'
    foreach ($file in $files) {
        $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8


        $minified = $content -replace '(?s)/\*.*?\*/', '' -replace '\s+', ' ' -replace '\s*([{};:,])\s*', '$1' -replace '\s+', ' '
        $minified = $minified.Trim()

        if ($minified.Length -lt $content.Length) {
            Set-Content -Path $file.FullName -Value $minified -Encoding UTF8 -NoNewline
            Log "Minified $($file.Name): $($content.Length) -> $($minified.Length) bytes"
        }
    }
}

function Create-LazyLoadModule {
    Log "Creating lazy load module..."

    $lazyModule = @'
// Lazy Loading Module for CS2 Radar
// Loads non-critical assets on demand

(function() {
    'use strict';

    const loadedModules = new Set();
    const loadPromises = new Map();

    window.radarLoadModule = async function(moduleName) {
        if (loadedModules.has(moduleName)) return;
        if (loadPromises.has(moduleName)) return loadPromises.get(moduleName);

        const promise = (async () => {
            try {
                const script = document.createElement('script');
                script.src = 'renderers/' + moduleName + '.js';
                script.type = 'module';
                document.head.appendChild(script);
                await new Promise((resolve, reject) => {
                    script.onload = resolve;
                    script.onerror = reject;
                });
                loadedModules.add(moduleName);
            } catch (e) {
                loadPromises.delete(moduleName);
                throw e;
            }
        })();

        loadPromises.set(moduleName, promise);
        return promise;
    };

    // Preload critical modules
    const criticalModules = ['_init', '_global', '_map'];
    criticalModules.forEach(m => radarLoadModule(m));

    // Lazy load on user interaction
    let interactionLoaded = false;
    function loadOnInteraction() {
        if (interactionLoaded) return;
        interactionLoaded = true;

        ['_settings', '_socket', 'playerPosition', 'droppedWeapons', 'bomb', 'bombTimer',
         'advisory', 'smokes', 'projectiles', 'teamPanel', 'rttMonitor'].forEach(m => {
            radarLoadModule(m).catch(console.error);
        });
    }

    ['click', 'keydown', 'mousemove', 'touchstart'].forEach(evt => {
        document.addEventListener(evt, loadOnInteraction, { once: true, passive: true });
    });
})();
'@

    $outputPath = Join-Path $RenderersDir 'lazy-load.js'
    Set-Content -Path $outputPath -Value $lazyModule -Encoding UTF8 -NoNewline
    Log "Created lazy-load.js module"
}

function Update-ResourceManifest {
    Log "Updating resource manifest for lazy loading..."

    $manifestPath = Join-Path $RadarDir 'manifest.json'
    $manifest = @{
        version = "1.0"
        lazyLoad = $true
        critical = @('img/icon-64x64.png', 'css/main.css', 'renderers/_init.js')
        lazy = @(
            'renderers/_settings.js'
            'renderers/_socket.js'
            'renderers/playerPosition.js'
            'renderers/droppedWeapons.js'
            'renderers/bomb.js'
            'renderers/bombTimer.js'
            'renderers/advisory.js'
            'renderers/smokes.js'
            'renderers/projectiles.js'
            'renderers/teamPanel.js'
            'renderers/rttMonitor.js'
            'img/icons/*.svg'
            'maps/*/radar.png'
            'maps/*/overlay_buyzones.png'
        )
    }

    $json = $manifest | ConvertTo-Json -Depth 5 -Compress
    Set-Content -Path $manifestPath -Value $json -Encoding UTF8
    Log "Updated manifest.json"
}


Write-Host "=== CS2 Radar Memory Optimization ==="
Write-Host "Target directory: $RadarDir"

Optimize-MapAssets
Add-LazyLoadingToIndex
Optimize-Renderers
Optimize-CSS
Create-LazyLoadModule
Update-ResourceManifest

Write-Host "=== Optimization Complete ==="
