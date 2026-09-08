/* settings.js - FiveM Radar Settings Management */

const SettingsManager = {
	state: {
		settings: {},
		panelOpen: false
	},
	
	elements: {},
	
	// Default settings
	DEFAULTS: {
		// Display
		showNames: true,
		showArrows: true,
		showHealth: true,
		showArmor: false,
		showVehicles: true,
		followRotation: false,
		playerDotScale: 0.7,
		
		// Names
		showName: 'always', // never, always, hover
		maxNameLength: 12,
		
		// Map
		mapStyle: 'satellite', // satellite, roadmap, hybrid
		autoZoom: true,
		minZoom: 1.5,
		
		// Filters
		filterPlayers: true,
		filterNPCs: false,
		filterVehicles: false,
		maxDistance: 5000,
		
		// Theme
		theme: 'dark' // dark, light, tactical
	},
	
	init() {
		this.cacheElements();
		this.loadSettings();
		this.bindEvents();
		this.applySettings();
	},
	
	cacheElements() {
		this.elements = {
			panel: document.getElementById('settings-panel'),
			toggle: document.getElementById('settings-toggle'),
			reset: document.getElementById('settings-reset'),
			languageSelect: document.getElementById('cfg-language')
		};
		
		// Cache all setting inputs
		this.elements.inputs = document.querySelectorAll('[data-config]');
	},
	
	bindEvents() {
		if (this.elements.toggle) {
			this.elements.toggle.addEventListener('click', () => this.togglePanel());
		}
		
		if (this.elements.reset) {
			this.elements.reset.addEventListener('click', () => this.resetSettings());
		}
		
		if (this.elements.languageSelect) {
			this.elements.languageSelect.addEventListener('change', (e) => {
				if (window.setLanguage) window.setLanguage(e.target.value);
			});
		}
		
		// Bind all setting inputs
		this.elements.inputs.forEach(input => {
			const key = input.getAttribute('data-config').replace('radar.', '');
			
			if (input.type === 'checkbox') {
				input.addEventListener('change', (e) => this.updateSetting(key, e.target.checked));
				// Set initial state
				input.checked = this.state.settings[key] !== false;
			} else if (input.type === 'range') {
				input.addEventListener('input', (e) => {
					const val = parseFloat(e.target.value);
					this.updateSetting(key, val);
					const valEl = document.querySelector(`[data-value-for="radar.${key}"]`);
					if (valEl) valEl.textContent = e.target.value;
				});
				input.value = this.state.settings[key] || this.DEFAULTS[key];
			} else {
				input.addEventListener('change', (e) => this.updateSetting(key, e.target.value));
				input.value = this.state.settings[key] || this.DEFAULTS[key];
			}
		});
		
		// Close panel on outside click
		document.addEventListener('click', (e) => {
			if (this.state.panelOpen && 
				!this.elements.panel.contains(e.target) && 
				!this.elements.toggle.contains(e.target)) {
				this.closePanel();
			}
		});
		
		// Keyboard shortcuts
		document.addEventListener('keydown', (e) => {
			if (e.key === 'Escape' && this.state.panelOpen) {
				this.closePanel();
			}
		});
	},
	
	loadSettings() {
		const saved = localStorage.getItem('fivem_radar_settings');
		if (saved) {
			try {
				this.state.settings = { ...this.DEFAULTS, ...JSON.parse(saved) };
			} catch (e) {
				console.warn('[Settings] Failed to parse:', e);
				this.state.settings = { ...this.DEFAULTS };
			}
		} else {
			this.state.settings = { ...this.DEFAULTS };
		}
	},
	
	saveSettings() {
		localStorage.setItem('fivem_radar_settings', JSON.stringify(this.state.settings));
	},
	
	applySettings() {
		const s = this.state.settings;
		
		// Apply theme
		document.documentElement.setAttribute('data-theme', s.theme);
		
		// Apply to all inputs
		this.elements.inputs.forEach(input => {
			const key = input.getAttribute('data-config').replace('radar.', '');
			const value = s[key];
			
			if (value === undefined) return;
			
			if (input.type === 'checkbox') {
				input.checked = value;
			} else if (input.type === 'range') {
				input.value = value;
				const valEl = document.querySelector(`[data-value-for="radar.${key}"]`);
				if (valEl) valEl.textContent = value;
			} else {
				input.value = value;
			}
		});
		
		// Update language select
		if (this.elements.languageSelect) {
			this.elements.languageSelect.value = localStorage.getItem('fivem_radar_lang') || 'pt';
		}
		
		// Emit settings change event
		window.dispatchEvent(new CustomEvent('settingsChanged', { detail: { settings: s } }));
	},
	
	updateSetting(key, value) {
		this.state.settings[key] = value;
		this.saveSettings();
		this.applySingleSetting(key, value);
	},
	
	applySingleSetting(key, value) {
		switch (key) {
			case 'theme':
				document.documentElement.setAttribute('data-theme', value);
				break;
			case 'followRotation':
				// Handled by RadarApp
				break;
			case 'autoZoom':
				// Handled by RadarApp
				break;
			case 'minZoom':
				if (window.RadarApp && window.RadarApp.state.zoom < value) {
					window.RadarApp.setZoom(value);
				}
				break;
			case 'mapStyle':
				// Could switch map tiles here
				break;
			case 'playerDotScale':
				if (window.RadarApp) window.RadarApp.updateAllMarkerSizes();
				break;
			case 'showNames':
			case 'showArrows':
			case 'showHealth':
			case 'showArmor':
			case 'showVehicles':
				// Handled by marker updates
				break;
		}
	},
	
	togglePanel() {
		if (this.state.panelOpen) {
			this.closePanel();
		} else {
			this.openPanel();
		}
	},
	
	openPanel() {
		this.elements.panel.removeAttribute('hidden');
		this.state.panelOpen = true;
		this.elements.toggle.setAttribute('aria-expanded', 'true');
	},
	
	closePanel() {
		this.elements.panel.setAttribute('hidden', '');
		this.state.panelOpen = false;
		this.elements.toggle.setAttribute('aria-expanded', 'false');
	},
	
	resetSettings() {
		if (!confirm(window.t ? window.t('settings_reset_confirm') || 'Reset all settings to defaults?' : 'Reset all settings to defaults?')) return;
		
		localStorage.removeItem('fivem_radar_settings');
		this.state.settings = { ...this.DEFAULTS };
		this.applySettings();
		
		// Show notification
		this.showNotification(window.t ? window.t('config_reset') : 'Settings reset');
	},
	
	showNotification(message) {
		const existing = document.querySelector('.settings-notification');
		if (existing) existing.remove();
		
		const notif = document.createElement('div');
		notif.className = 'settings-notification';
		notif.textContent = message;
		notif.style.cssText = `
			position: fixed;
			bottom: 80px;
			left: 50%;
			transform: translateX(-50%);
			background: var(--panel-bg);
			border: 1px solid var(--accent-gold);
			border-radius: 6px;
			padding: 10px 20px;
			color: var(--accent-gold);
			font-size: 13px;
			z-index: 1000;
			animation: slideUp 0.3s ease-out;
		`;
		
		document.body.appendChild(notif);
		
		setTimeout(() => {
			notif.style.animation = 'slideDown 0.3s ease-in forwards';
			setTimeout(() => notif.remove(), 300);
		}, 2000);
	},
	
	// Export/Import settings
	exportSettings() {
		const data = {
			settings: this.state.settings,
			version: 1,
			exported: new Date().toISOString()
		};
		
		const blob = new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' });
		const url = URL.createObjectURL(blob);
		const a = document.createElement('a');
		a.href = url;
		a.download = `fivem-radar-settings-${Date.now()}.json`;
		a.click();
		URL.revokeObjectURL(url);
	},
	
	importSettings(file) {
		const reader = new FileReader();
		reader.onload = (e) => {
			try {
				const data = JSON.parse(e.target.result);
				if (data.settings) {
					this.state.settings = { ...this.DEFAULTS, ...data.settings };
					this.saveSettings();
					this.applySettings();
					this.showNotification(window.t ? window.t('config_loaded') : 'Settings imported');
				}
			} catch (err) {
				console.error('[Settings] Import failed:', err);
				this.showNotification('Import failed: Invalid file');
			}
		};
		reader.readAsText(file);
	}
};

// Auto-init
document.addEventListener('DOMContentLoaded', () => SettingsManager.init());

window.SettingsManager = SettingsManager;

// Add notification animations
const style = document.createElement('style');
style.textContent = `
@keyframes slideUp {
	from { opacity: 0; transform: translateX(-50%) translateY(20px); }
	to { opacity: 1; transform: translateX(-50%) translateY(0); }
}
@keyframes slideDown {
	from { opacity: 1; transform: translateX(-50%) translateY(0); }
	to { opacity: 0; transform: translateX(-50%) translateY(20px); }
}
`;
document.head.appendChild(style);