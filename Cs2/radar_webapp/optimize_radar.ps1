# CS2 Radar WebApp Memory Optimization Script
# Optimizes assets and adds lazy loading for reduced memory footprint

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
    Log "Optimizing map assets..."
    
    if (-not (Test-Path $MapsDir)) {
        Write-Warning "Maps directory not found: $MapsDir"
        return
    }
    
    $maps = Get-ChildItem -Path $MapsDir -Directory
    foreach ($map in $maps) {
        $radarPath = Join-Path $map.FullName 'radar.png'
        $overlayPath = Join-Path $map.FullName 'overlay_buyzones.png'
        $overlayLogosPath = Join-Path $map.FullName 'overlay_logos.png'
        $metaPath = Join-Path $map.FullName 'meta.json5'
        
        # Add lazy loading hints to meta.json5
        if (Test-Path $metaPath) {
            $meta = Get-Content -Path $metaPath -Raw -Encoding UTF8
            if ($meta -notmatch '"lazyLoad"') {
                $meta = $meta -replace '(?s)(\})', ', "lazyLoad": true$1'
                Set-Content -Path $metaPath -Value $meta -Encoding UTF8
                Log "Added lazyLoad flag to $($map.Name)/meta.json5"
            }
        }
        
        # Note: Image compression happens in Compress-Assets.ps1
        # This script focuses on structural optimizations
    }
}

function Add-LazyLoadingToIndex {
    Log "Adding lazy loading to index.html..."
    
    $indexPath = Join-Path $RadarDir 'index.html'
    if (-not (Test-Path $indexPath)) {
        Write-Warning "index.html not found"
        return
    }
    
    $html = Get-Content -Path $indexPath -Raw -Encoding UTF8
    
    # Add preload hints for critical assets
    if ($html -notmatch 'rel="preload"') {
        $preload = @'
    <link rel="preload" as="image" href="img/icon-64x64.png">
    <link rel="preload" as="style" href="css/main.css">
    <link rel="preload" as="script" href="renderers/_init.js">
'@
        $html = $html -replace '(</head>)', "$preload`n$1"
    }
    
    # Add loading="lazy" to non-critical images
    if ($html -notmatch 'loading="lazy"') {
        $html = $html -replace '<img([^>]*src="img/[^"]*")', '<img$1 loading="lazy"'
    }
    
    # Add defer to non-critical scripts
    if ($html -notmatch 'defer') {
        $html = $html -replace '<script src="(renderers/[^"]+\.js)"', '<script src="$1" defer'
    }
    
    Set-Content -Path $indexPath -Value $html -Encoding UTF8
    Log "Updated index.html with lazy loading optimizations"
}

function Optimize-Renderers {
    Log "Optimizing renderer modules for lazy loading..."
    
    if (-not (Test-Path $RenderersDir)) {
        Write-Warning "Renderers directory not found"
        return
    }
    
    $files = Get-ChildItem -Path $RenderersDir -Filter '*.js'
    foreach ($file in $files) {
        $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8
        $modified = $false
        
        # Add lazy initialization pattern
        if ($content -notmatch 'lazyInit' -and $content -match 'function\s+\w+\s*\(') {
            # Add lazy init wrapper
            $header = @'
// Lazy initialization wrapper
(function() {
    if (typeof window.radarLazyInit === 'undefined') {
        window.radarLazyInit = [];
    }
    window.radarLazyInit.push(function() {
'@
            $footer = @'
    });
})();
'@
            $content = $header + "`n" + $content + "`n" + $footer
            Set-Content -Path $file.FullName -Value $content -Encoding UTF8
            $modified = $true
        }
        
        # Minify (basic)
        if ($content.Length -gt 5000) {
            $minified = $content -replace '(?s)/\*.*?\*/', '' -replace '(?m)^\s*//.*$', '' -replace '\s+', ' ' -replace '\s*([{}();,])\s*', '$1'
            if ($minified.Length -lt $content.Length) {
                Set-Content -Path $file.FullName -Value $minified -Encoding UTF8 -NoNewline
                $modified = $true
            }
        }
        
        if ($modified) {
            Log "Optimized $($file.Name)"
        }
    }
}

function Optimize-CSS {
    Log "Optimizing CSS..."
    
    $files = Get-ChildItem -Path $CssDir -Filter '*.css'
    foreach ($file in $files) {
        $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8
        
        # Basic minification
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

# Main
Write-Host "=== CS2 Radar Memory Optimization ==="
Write-Host "Target directory: $RadarDir"

Optimize-MapAssets
Add-LazyLoadingToIndex
Optimize-Renderers
Optimize-CSS
Create-LazyLoadModule
Update-ResourceManifest

Write-Host "=== Optimization Complete ==="