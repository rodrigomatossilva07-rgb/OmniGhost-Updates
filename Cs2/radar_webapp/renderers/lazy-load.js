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