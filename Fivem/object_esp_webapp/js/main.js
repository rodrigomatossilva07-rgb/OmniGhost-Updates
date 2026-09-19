/* main.js - FiveM Object ESP Core Application */

const ObjectESPApp = {
	// State
	state: {
		connected: false,
		connecting: false,
		scanning: false,
		scanProgress: 0,
		scanResults: [],
		whitelist: [],
		trackedObjects: [],
		selectedObjectId: null,
		followingObjectId: null,
		mapType: 'los_santos',
		inCayo: false,
		zoom: 1.5,
		minZoom: 1.5,
		maxZoom: 8,
		centerX: 0,
		centerY: 0,
		rotation: 0,
		followRotation: false,
		autoZoom: true,
		lastUpdate: 0,
		updateInterval: null,
		apiBase: '',
		accessToken: '',
		settings: {},
		dmaStatus: 'offline'
	},
	
	// DOM elements
	elements: {},
	
	// Constants
	CONFIG: {
		API_POLL_INTERVAL: 100,
		STALE_THRESHOLD: 5000,
		ZOOM_SPEED: 0.15,
		PAN_SPEED: 0.5,
		SMOOTHING: 0.15,
		MAP_BOUNDS: {
			los_santos: { x: [-4000, 4000], y: [-4000, 8000] },
			cayo_perico: { x: [4500, 7500], y: [-3500, -500] }
		}
	},
	
	// Initialize
	init() {
		this.cacheElements();
		this.loadSettings();
		this.setupEventListeners();
		this.detectAccessToken();
		this.startPolling();
		this.initMap();
		console.log('[FiveM Object ESP] Initialized');
	},
	
	cacheElements() {
		this.elements = {
			connStatus: document.getElementById('conn-status'),
			radar: document.getElementById('radar'),
			radarCanvas: document.getElementById('radar-canvas'),
			entities: document.getElementById('entities'),
			objectTableBody: document.getElementById('object-tbody'),
			objectCount: document.getElementById('object-count'),
			objectSearch: document.getElementById('object-search'),
			objectSort: document.getElementById('object-sort'),
			settingsPanel: document.getElementById('settings-panel'),
			settingsToggle: document.getElementById('settings-toggle'),
			rotateToggle: document.getElementById('rotate90-toggle'),
			settingsReset: document.getElementById('settings-reset'),
			objectPanel: document.getElementById('object-list'),
			unknownMap: document.getElementById('unknownMap')
		};
	},
	
	detectAccessToken() {
		const hash = window.location.hash.slice(1);
		if (hash) {
			this.state.accessToken = decodeURIComponent(hash);
			console.log('[Object ESP] Access token detected from URL');
		}
	},
	
	loadSettings() {
		const saved = localStorage.getItem('object_esp_settings');
		if (saved) {
			try {
				this.state.settings = JSON.parse(saved);
				this.applySettings(this.state.settings);
			} catch (e) {
				console.warn('[Object ESP] Failed to parse settings:', e);
			}
		}
		// Apply defaults
		this.state.settings = Object.assign({
			showNames: true,
			showDistance: true,
			showCategory: false,
			showBox: false,
			showMarker: true,
			followRotation: false,
			playerDotScale: 0.7,
			showName: 'always',
			maxNameLength: 12,
			mapStyle: 'satellite',
			autoZoom: true,
			minZoom: 1.5,
			filterPlayers: true,
			filterNPCs: false,
			filterVehicles: false,
			maxDistance: 3000,
			theme: 'dark'
		}, this.state.settings);
		
		this.applySettings(this.state.settings);
	},
	
	saveSettings() {
		localStorage.setItem('object_esp_settings', JSON.stringify(this.state.settings));
	},
	
	applySettings(settings) {
		// Apply theme
		document.documentElement.setAttribute('data-theme', settings.theme);
		
		// Apply to inputs
		Object.keys(settings).forEach(key => {
			const el = document.querySelector(`[data-config="radar.${key}"]`);
			if (el) {
				if (el.type === 'checkbox') el.checked = settings[key];
				else if (el.type === 'range') {
					el.value = settings[key];
					const valEl = document.querySelector(`[data-value-for="radar.${key}"]`);
					if (valEl) valEl.textContent = settings[key];
				} else {
					el.value = settings[key];
				}
			}
		});
		
		this.updateMapStyle();
	},
	
	setupEventListeners() {
		// Settings panel
		this.elements.settingsToggle.addEventListener('click', () => this.toggleSettings());
		this.elements.rotateToggle.addEventListener('click', () => this.toggleRotation());
		this.elements.settingsReset.addEventListener('click', () => this.resetSettings());
		
		// Settings inputs
		document.querySelectorAll('[data-config]').forEach(el => {
			const key = el.getAttribute('data-config').replace('radar.', '');
			if (el.type === 'checkbox') {
				el.addEventListener('change', (e) => this.updateSetting(key, e.target.checked));
			} else if (el.type === 'range') {
				el.addEventListener('input', (e) => {
					this.updateSetting(key, parseFloat(e.target.value));
					const valEl = document.querySelector(`[data-value-for="radar.${key}"]`);
					if (valEl) valEl.textContent = e.target.value;
				});
			} else {
				el.addEventListener('change', (e) => this.updateSetting(key, e.target.value));
			}
		});
		
		// Object list
		this.elements.objectSearch.addEventListener('input', (e) => this.filterObjects(e.target.value));
		this.elements.objectSort.addEventListener('change', (e) => this.sortObjects(e.target.value));
		
		// Map controls
		const zoomIn = document.getElementById('zoom-in');
		const zoomOut = document.getElementById('zoom-out');
		const resetView = document.getElementById('reset-view');
		
		if (zoomIn) zoomIn.addEventListener('click', () => this.zoom(1));
		if (zoomOut) zoomOut.addEventListener('click', () => this.zoom(-1));
		if (resetView) resetView.addEventListener('click', () => this.resetView());
		
		// Map interactions
		this.elements.radar.addEventListener('wheel', (e) => this.handleWheel(e));
		this.elements.radar.addEventListener('mousedown', (e) => this.startPan(e));
		this.elements.radar.addEventListener('dblclick', (e) => this.handleDoubleClick(e));
		
		// Keyboard
		document.addEventListener('keydown', (e) => this.handleKeyDown(e));
		
		// Window
		window.addEventListener('resize', () => this.handleResize());
		window.addEventListener('beforeunload', () => this.cleanup());
	},
	
	updateSetting(key, value) {
		this.state.settings[key] = value;
		this.saveSettings();
		
		switch (key) {
			case 'theme':
				document.documentElement.setAttribute('data-theme', value);
				break;
			case 'followRotation':
				this.state.followRotation = value;
				break;
			case 'autoZoom':
				this.state.autoZoom = value;
				break;
			case 'minZoom':
				this.state.minZoom = value;
				if (this.state.zoom < value) this.setZoom(value);
				break;
			case 'mapStyle':
				this.updateMapStyle();
				break;
			case 'playerDotScale':
				this.updateAllMarkerSizes();
				break;
		}
	},
	
	toggleSettings() {
		const hidden = this.elements.settingsPanel.hasAttribute('hidden');
		if (hidden) {
			this.elements.settingsPanel.removeAttribute('hidden');
		} else {
			this.elements.settingsPanel.setAttribute('hidden', '');
		}
	},
	
	toggleRotation() {
		this.state.followRotation = !this.state.followRotation;
		this.updateSetting('followRotation', this.state.followRotation);
		const el = document.querySelector('[data-config="radar.followRotation"]');
		if (el) el.checked = this.state.followRotation;
	},
	
	resetSettings() {
		if (!confirm('Reset all settings to defaults?')) return;
		
		localStorage.removeItem('object_esp_settings');
		this.state.settings = {};
		this.loadSettings();
	},
	
	// Connection & Polling
	startPolling() {
		if (this.state.updateInterval) clearInterval(this.state.updateInterval);
		this.state.updateInterval = setInterval(() => this.pollState(), this.CONFIG.API_POLL_INTERVAL);
		this.pollState(); // Initial poll
	},
	
	async pollState() {
		if (this.state.connecting) return;
		
		try {
			const headers = this.state.accessToken ? { 'Authorization': `Bearer ${this.state.accessToken}` } : {};
			const response = await fetch('/api/state', { 
				cache: 'no-store',
				headers 
			});
			
			if (!response.ok) throw new Error(`HTTP ${response.status}`);
			
			const data = await response.json();
			this.handleStateUpdate(data);
			this.updateConnectionStatus(true);
			
		} catch (error) {
			console.warn('[Object ESP] Poll failed:', error.message);
			this.updateConnectionStatus(false);
		}
	},
	
	updateConnectionStatus(connected) {
		const wasConnected = this.state.connected;
		this.state.connected = connected;
		
		if (connected && !wasConnected) {
			this.elements.connStatus.textContent = 'Radar online';
			this.elements.connStatus.className = 'conn-status conn-status--online';
			this.elements.unknownMap.hidden = true;
			this.elements.radar.hidden = false;
		} else if (!connected && wasConnected) {
			this.elements.connStatus.textContent = 'DMA Offline';
			this.elements.connStatus.className = 'conn-status conn-status--error';
			this.elements.unknownMap.hidden = false;
			this.elements.radar.hidden = true;
		}
	},
	
	handleStateUpdate(data) {
		if (!data || !data.objects) return;
		
		this.state.trackedObjects = data.objects;
		this.state.lastUpdate = Date.now();
		
		// Find local player
		this.state.localPlayer = data.objects.find(p => p.is_local) || null;
		
		// Update UI
		this.updateObjectList();
		this.updateMapMarkers();
		this.updateObjectCount();
		
		// Auto-zoom to fit all objects
		if (this.state.autoZoom && !this.state.followingObjectId) {
			this.autoZoomToObjects();
		}
		
		// Follow selected object
		if (this.state.followingObjectId) {
			this.followObject(this.state.followingObjectId);
		}
		
		// Check for Cayo Perico
		this.checkCayoTransition();
	},
	
	updateObjectCount() {
		const count = this.state.trackedObjects.filter(p => !p.is_local).length;
		if (this.elements.objectCount) {
			this.elements.objectCount.textContent = count;
		}
	},
	
	updateObjectList() {
		if (!this.elements.objectTableBody) return;
		
		// For web version, we'll show tracked objects in the table
		const objects = this.state.trackedObjects.filter(p => !p.is_local);
		const sortBy = this.elements.objectSort?.value || 'distance';
		
		objects.sort((a, b) => {
			if (sortBy === 'name') return (a.name || '').localeCompare(b.name || '');
			if (sortBy === 'category') return (a.category || '').localeCompare(b.category || '');
			return (a.distance || 0) - (b.distance || 0);
		});
		
		this.elements.objectTableBody.innerHTML = objects.map(obj => this.renderObjectRow(obj)).join('');
		
		// Bind click events
		this.elements.objectTableBody.querySelectorAll('tr').forEach((row, idx) => {
			const obj = objects[idx];
			row.addEventListener('click', () => this.selectObject(obj.id));
			row.addEventListener('dblclick', () => this.followObject(obj.id));
		});
	},
	
	renderObjectRow(object) {
		const isSelected = object.id === this.state.selectedObjectId;
		const isFollowing = object.id === this.state.followingObjectId;
		const hp = object.health || 100;
		const hpClass = hp > 75 ? 'high' : hp > 35 ? 'med' : 'low';
		
		return `
			<tr class="${isSelected ? 'selected' : ''} ${isFollowing ? 'following' : ''}" data-id="${object.id}">
				<td class="object-name">${object.name || 'Unknown'}${object.is_local ? ' <span class="local-badge">You</span>' : ''}</td>
				<td class="object-category">${object.category || 'Unknown'}</td>
				<td class="object-dist">${object.distance ? object.distance.toFixed(0) + 'm' : '—'}</td>
				<td class="object-enabled">
					<input type="checkbox" ${object.enabled ? 'checked' : ''} onchange="window.ObjectESPApp.toggleObjectEnabled('${object.id}', this.checked)">
				</td>
			</tr>
		`;
	},
	
	toggleObjectEnabled(id, enabled) {
		const obj = this.state.trackedObjects.find(o => o.id === id);
		if (obj) {
			obj.enabled = enabled;
			// Would send to backend
		}
	},
	
	filterObjects(query) {
		const rows = this.elements.objectTableBody.querySelectorAll('tr');
		const q = query.toLowerCase();
		rows.forEach(row => {
			const name = row.querySelector('.object-name').textContent.toLowerCase();
			row.style.display = name.includes(q) ? '' : 'none';
		});
	},
	
	sortObjects(sortBy) {
		this.updateObjectList();
	},
	
	selectObject(objectId) {
		this.state.selectedObjectId = objectId;
		
		// Update row highlight
		this.elements.objectTableBody.querySelectorAll('tr').forEach(row => {
			row.classList.toggle('selected', row.dataset.id == objectId);
		});
		
		// Center on object (single click)
		const obj = this.state.trackedObjects.find(p => p.id == objectId);
		if (obj) this.centerOnObject(obj, false);
	},
	
	followObject(objectId) {
		this.state.followingObjectId = objectId;
		this.state.selectedObjectId = objectId;
		
		// Update row highlight
		this.elements.objectTableBody.querySelectorAll('tr').forEach(row => {
			row.classList.toggle('following', row.dataset.id == objectId);
			row.classList.toggle('selected', row.dataset.id == objectId);
		});
		
		const obj = this.state.trackedObjects.find(p => p.id == objectId);
		if (obj) this.centerOnObject(obj, true);
	},
	
	stopFollowing() {
		this.state.followingObjectId = null;
		this.elements.objectTableBody.querySelectorAll('tr').forEach(row => {
			row.classList.remove('following');
		});
	},
	
	centerOnObject(obj, follow) {
		if (!obj) return;
		
		const targetX = obj.x;
		const targetY = -obj.y; // Invert Y for screen coords
		
		if (follow) {
			this.state.followingObjectId = obj.id;
		}
		
		this.smoothPanTo(targetX, targetY);
	},
	
	// Map Functions
	initMap() {
		this.state.zoom = this.state.settings.minZoom || 1.5;
		this.state.minZoom = this.state.settings.minZoom || 1.5;
		this.applyTransform();
		this.updateMapStyle();
	},
	
	updateMapStyle() {
		const style = this.state.settings.mapStyle || 'satellite';
		// Could switch map tiles here
		console.log('[Object ESP] Map style:', style);
	},
	
	applyTransform() {
		const { zoom, centerX, centerY, rotation } = this.state;
		const transform = `translate(${centerX}px, ${centerY}px) scale(${zoom}) rotate(${rotation}deg)`;
		
		this.elements.radarCanvas.style.transform = transform;
		this.elements.entities.style.transform = transform;
	},
	
	setZoom(level) {
		this.state.zoom = Math.max(this.state.minZoom, Math.min(this.state.maxZoom, level));
		this.applyTransform();
	},
	
	zoom(direction) {
		const factor = direction > 0 ? 1.2 : 0.833;
		this.setZoom(this.state.zoom * factor);
	},
	
	handleWheel(e) {
		e.preventDefault();
		const direction = e.deltaY < 0 ? 1 : -1;
		this.zoom(direction);
	},
	
	startPan(e) {
		if (e.target.closest('.object-marker')) return;
		
		const startX = e.clientX;
		const startY = e.clientY;
		const startCenterX = this.state.centerX;
		const startCenterY = this.state.centerY;
		
		const onMove = (e) => {
			const dx = (e.clientX - startX) / this.state.zoom;
			const dy = (e.clientY - startY) / this.state.zoom;
			this.state.centerX = startCenterX + dx;
			this.state.centerY = startCenterY + dy;
			this.applyTransform();
		};
		
		const onUp = () => {
			document.removeEventListener('mousemove', onMove);
			document.removeEventListener('mouseup', onUp);
		};
		
		document.addEventListener('mousemove', onMove);
		document.addEventListener('mouseup', onUp);
	},
	
	handleDoubleClick(e) {
		if (e.target.closest('.object-marker')) return;
		this.resetView();
	},
	
	smoothPanTo(targetX, targetY) {
		const animate = () => {
			const dx = targetX - this.state.centerX;
			const dy = targetY - this.state.centerY;
			
			if (Math.abs(dx) < 0.5 && Math.abs(dy) < 0.5) {
				this.state.centerX = targetX;
				this.state.centerY = targetY;
				this.applyTransform();
				return;
			}
			
			this.state.centerX += dx * 0.15;
			this.state.centerY += dy * 0.15;
			this.applyTransform();
			requestAnimationFrame(animate);
		};
		
		requestAnimationFrame(animate);
	},
	
	resetView() {
		this.stopFollowing();
		this.state.centerX = 0;
		this.state.centerY = 0;
		this.setZoom(this.state.minZoom);
	},
	
	autoZoomToObjects() {
		const objects = this.state.trackedObjects.filter(p => !p.is_local);
		if (objects.length === 0) return;
		
		let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
		objects.forEach(p => {
			minX = Math.min(minX, p.x);
			maxX = Math.max(maxX, p.x);
			minY = Math.min(minY, p.y);
			maxY = Math.max(maxY, p.y);
		});
		
		const padding = 200;
		const width = (maxX - minX) + padding * 2;
		const height = (maxY - minY) + padding * 2;
		
		const radarRect = this.elements.radar.getBoundingClientRect();
		const zoomX = radarRect.width / width;
		const zoomY = radarRect.height / height;
		const zoom = Math.min(zoomX, zoomY, this.state.maxZoom);
		
		this.state.centerX = -(minX + maxX) / 2 * zoom;
		this.state.centerY = (minY + maxY) / 2 * zoom; // Y inverted
		this.setZoom(Math.max(zoom, this.state.minZoom));
	},
	
	checkCayoTransition() {
		if (!this.state.localPlayer) return;
		
		const { x, y } = this.state.localPlayer;
		const inCayo = x > 4500 && x < 7500 && y < -500 && y > -3500;
		
		if (inCayo !== this.state.inCayo) {
			this.state.inCayo = inCayo;
			this.state.mapType = inCayo ? 'cayo_perico' : 'los_santos';
			
			// Switch map images
			const radarBg = document.getElementById('radarBackground');
			const radarCayo = document.getElementById('radarCayo');
			if (radarBg && radarCayo) {
				radarBg.hidden = inCayo;
				radarCayo.hidden = !inCayo;
			}
			
			// Show indicator
			if (inCayo) {
				this.showCayoIndicator();
			}
			
			// Reset view for new map
			this.resetView();
		}
	},
	
	showCayoIndicator() {
		let indicator = document.getElementById('cayo-indicator');
		if (!indicator) {
			indicator = document.createElement('div');
			indicator.id = 'cayo-indicator';
			indicator.className = 'cayo-indicator';
			indicator.textContent = 'Cayo Perico';
			this.elements.radar.appendChild(indicator);
		}
		indicator.hidden = false;
		setTimeout(() => { if (indicator) indicator.hidden = true; }, 5000);
	},
	
	// Rendering
	renderMarkers() {
		if (!this.elements.entities) return;
		
		const objects = this.state.trackedObjects.filter(p => !p.is_local || this.state.settings.show_local);
		const existingMarkers = new Map();
		
		// Cache existing markers
		this.elements.entities.querySelectorAll('.object-marker').forEach(el => {
			existingMarkers.set(el.dataset.id, el);
		});
		
		// Update or create markers
		objects.forEach(obj => {
			let marker = existingMarkers.get(obj.id);
			const isNew = !marker;
			
			if (isNew) {
				marker = this.createMarker(obj);
				this.elements.entities.appendChild(marker);
			}
			
			this.updateMarker(marker, obj);
			existingMarkers.delete(obj.id);
		});
		
		// Remove stale markers
		existingMarkers.forEach(marker => marker.remove());
	},
	
	createMarker(obj) {
		const marker = document.createElement('div');
		marker.className = `object-marker ${obj.is_local ? 'local' : (obj.is_player ? 'player' : 'npc')} ${obj.in_vehicle ? 'in-vehicle' : ''} ${obj.category ? obj.category.toLowerCase() : ''}`;
		marker.dataset.id = obj.id;
		marker.style.left = `${obj.x}px`;
		marker.style.top = `${-obj.y}px`; // Invert Y
		
		marker.innerHTML = `
			<div class="marker-dot"></div>
			<div class="marker-arrow"></div>
			<div class="marker-label">${obj.name || 'Unknown'}</div>
			<div class="vehicle-ring"></div>
		`;
		
		marker.addEventListener('click', (e) => {
			e.stopPropagation();
			this.selectObject(obj.id);
		});
		
		marker.addEventListener('dblclick', (e) => {
			e.stopPropagation();
			this.followObject(obj.id);
		});
		
		return marker;
	},
	
	updateMarker(marker, obj) {
		const dot = marker.querySelector('.marker-dot');
		const arrow = marker.querySelector('.marker-arrow');
		const label = marker.querySelector('.marker-label');
		const ring = marker.querySelector('.vehicle-ring');
		
		// Position
		marker.style.left = `${obj.x}px`;
		marker.style.top = `${-obj.y}px`;
		
		// Rotation
		if (this.state.settings.showMarker && obj.yaw !== undefined && arrow) {
			arrow.style.transform = `translate(-50%, -50%) rotate(${obj.yaw}deg)`;
			arrow.style.display = 'block';
		} else if (arrow) {
			arrow.style.display = 'none';
		}
		
		// Label
		if (label) {
			label.textContent = obj.name || 'Unknown';
			label.style.display = this.state.settings.showNames ? 'block' : 'none';
		}
		
		// Vehicle ring
		if (ring) {
			ring.style.display = obj.in_vehicle ? 'block' : 'none';
		}
		
		// Dot size
		const scale = this.state.settings.playerDotScale || 0.7;
		if (dot) {
			const baseSize = 12;
			dot.style.width = `${baseSize * scale}px`;
			dot.style.height = `${baseSize * scale}px`;
		}
		
		// Classes
		marker.classList.toggle('local', obj.is_local);
		marker.classList.toggle('player', obj.is_player && !obj.is_local);
		marker.classList.toggle('npc', !obj.is_player && !obj.is_local);
		marker.classList.toggle('custom', !obj.is_player && obj.is_custom);
		marker.classList.toggle('in-vehicle', obj.in_vehicle);
	},
	
	updateAllMarkerSizes() {
		const scale = this.state.settings.playerDotScale || 0.7;
		const baseSize = 12;
		document.querySelectorAll('.marker-dot').forEach(dot => {
			dot.style.width = `${baseSize * scale}px`;
			dot.style.height = `${baseSize * scale}px`;
		});
	},
	
	handleKeyDown(e) {
		switch (e.key) {
			case 'Escape':
				this.stopFollowing();
				this.toggleSettings();
				break;
			case '+':
			case '=':
				this.zoom(1);
				break;
			case '-':
				this.zoom(-1);
				break;
			case '0':
				this.resetView();
				break;
			case 'r':
				this.toggleRotation();
				break;
			case 'f':
				if (this.state.selectedObjectId) this.followObject(this.state.selectedObjectId);
				break;
		}
	},
	
	handleResize() {
		if (this.state.autoZoom && !this.state.followingObjectId) {
			this.autoZoomToObjects();
		}
	},
	
	cleanup() {
		if (this.state.updateInterval) clearInterval(this.state.updateInterval);
	}
};

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', () => ObjectESPApp.init());

// Export for debugging
window.ObjectESPApp = ObjectESPApp;