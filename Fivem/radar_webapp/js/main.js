/* main.js - FiveM Radar Core Application */

const RadarApp = {
	// State
	state: {
		connected: false,
		connecting: false,
		players: [],
		localPlayer: null,
		selectedPlayerId: null,
		followingPlayerId: null,
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
		console.log('[FiveM Radar] Initialized');
	},
	
	cacheElements() {
		this.elements = {
			connStatus: document.getElementById('conn-status'),
			radar: document.getElementById('radar'),
			radarBackground: document.getElementById('radarBackground'),
			radarCayo: document.getElementById('radarCayo'),
			entities: document.getElementById('entities'),
			playerTableBody: document.getElementById('player-tbody'),
			playerCount: document.getElementById('player-count'),
			playerSearch: document.getElementById('player-search'),
			playerSort: document.getElementById('player-sort'),
			settingsPanel: document.getElementById('settings-panel'),
			settingsToggle: document.getElementById('settings-toggle'),
			rotateToggle: document.getElementById('rotate90-toggle'),
			settingsReset: document.getElementById('settings-reset'),
			playerPanel: document.getElementById('player-list'),
			unknownMap: document.getElementById('unknownMap'),
			zoomIn: document.getElementById('zoom-in'),
			zoomOut: document.getElementById('zoom-out'),
			resetView: document.getElementById('reset-view')
		};
	},
	
	detectAccessToken() {
		const hash = window.location.hash.slice(1);
		if (hash) {
			this.state.accessToken = decodeURIComponent(hash);
			console.log('[FiveM Radar] Access token detected from URL');
		}
	},
	
	loadSettings() {
		const saved = localStorage.getItem('fivem_radar_settings');
		if (saved) {
			try {
				this.state.settings = JSON.parse(saved);
				this.applySettings(this.state.settings);
			} catch (e) {
				console.warn('[FiveM Radar] Failed to parse settings:', e);
			}
		}
		// Apply defaults
		this.state.settings = Object.assign({
			showNames: true,
			showArrows: true,
			showHealth: true,
			showArmor: false,
			showVehicles: true,
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
			maxDistance: 5000,
			theme: 'dark'
		}, this.state.settings);
		
		this.applySettings(this.state.settings);
	},
	
	saveSettings() {
		localStorage.setItem('fivem_radar_settings', JSON.stringify(this.state.settings));
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
		
		// Player list
		this.elements.playerSearch.addEventListener('input', (e) => this.filterPlayers(e.target.value));
		this.elements.playerSort.addEventListener('change', (e) => this.sortPlayers(e.target.value));
		
		// Map controls
		if (this.elements.zoomIn) this.elements.zoomIn.addEventListener('click', () => this.zoom(1));
		if (this.elements.zoomOut) this.elements.zoomOut.addEventListener('click', () => this.zoom(-1));
		if (this.elements.resetView) this.elements.resetView.addEventListener('click', () => this.resetView());
		
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
		localStorage.removeItem('fivem_radar_settings');
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
			console.warn('[FiveM Radar] Poll failed:', error.message);
			this.updateConnectionStatus(false);
		}
	},
	
	updateConnectionStatus(connected) {
		const wasConnected = this.state.connected;
		this.state.connected = connected;
		
		if (connected && !wasConnected) {
			this.elements.connStatus.textContent = t('radar_online');
			this.elements.connStatus.className = 'conn-status conn-status--online';
			this.elements.unknownMap.hidden = true;
			this.elements.radar.hidden = false;
		} else if (!connected && wasConnected) {
			this.elements.connStatus.textContent = t('dma_offline');
			this.elements.connStatus.className = 'conn-status conn-status--error';
			this.elements.unknownMap.hidden = false;
			this.elements.radar.hidden = true;
		}
	},
	
	handleStateUpdate(data) {
		if (!data || !data.players) return;
		
		this.state.players = data.players;
		this.state.lastUpdate = Date.now();
		
		// Find local player
		this.state.localPlayer = data.players.find(p => p.is_local) || null;
		
		// Update DMA status
		if (data.dma_status) this.state.dmaStatus = data.dma_status;
		
		// Update UI
		this.updatePlayerList();
		this.updateMapMarkers();
		this.updatePlayerCount();
		
		// Auto-zoom to fit all players
		if (this.state.autoZoom && !this.state.followingPlayerId) {
			this.autoZoomToPlayers();
		}
		
		// Follow selected player
		if (this.state.followingPlayerId) {
			this.followPlayer(this.state.followingPlayerId);
		}
		
		// Check for Cayo Perico
		this.checkCayoTransition();
	},
	
	updatePlayerCount() {
		const count = this.state.players.filter(p => !p.is_local || this.state.settings.show_local).length;
		if (this.elements.playerCount) {
			this.elements.playerCount.textContent = count;
		}
	},
	
	updatePlayerList() {
		if (!this.elements.playerTableBody) return;
		
		const players = this.getFilteredPlayers();
		const sortBy = this.elements.playerSort?.value || 'distance';
		
		players.sort((a, b) => {
			if (sortBy === 'name') return (a.name || '').localeCompare(b.name || '');
			if (sortBy === 'health') return (b.health || 0) - (a.health || 0);
			return (a.distance || 0) - (b.distance || 0);
		});
		
		this.elements.playerTableBody.innerHTML = players.map(p => this.renderPlayerRow(p)).join('');
		
		// Add click handlers
		this.elements.playerTableBody.querySelectorAll('tr').forEach((row, idx) => {
			row.addEventListener('click', () => this.selectPlayer(players[idx].id));
			row.addEventListener('dblclick', () => this.followPlayer(players[idx].id));
		});
	},
	
	getFilteredPlayers() {
		return this.state.players.filter(p => {
			if (p.is_local && !this.state.settings.show_local) return false;
			if (this.state.settings.filterPlayers && !p.is_player) return false;
			if (!this.state.settings.filterNPCs && !p.is_player) return false;
			if (this.state.settings.filterVehicles && !p.in_vehicle) return false;
			if (this.state.settings.maxDistance && p.distance > this.state.settings.maxDistance) return false;
			return true;
		});
	},
	
	renderPlayerRow(player) {
		const isSelected = player.id === this.state.selectedPlayerId;
		const isFollowing = player.id === this.state.followingPlayerId;
		const hpClass = (player.health || 100) > 75 ? 'high' : (player.health || 100) > 35 ? 'med' : 'low';
		const nameClass = player.is_local ? 'local' : (player.is_player ? '' : 'npc');
		
		return `
			<tr class="${isSelected ? 'selected' : ''} ${isFollowing ? 'following' : ''}" data-id="${player.id}">
				<td class="player-name ${nameClass}">${this.truncateName(player.name || t('unknown_player'), this.state.settings.maxNameLength)}${player.is_local ? ` <span class="local-badge">${t('local_player')}</span>` : ''}</td>
				<td class="player-dist">${player.distance ? player.distance.toFixed(0) + 'm' : '—'}</td>
				<td class="player-hp ${hpClass}">${player.health ? Math.round(player.health) : '—'}</td>
				<td class="player-armor">${player.armor ? Math.round(player.armor) : '—'}</td>
				<td class="player-vehicle">${player.in_vehicle ? (player.vehicle || t('in_vehicle')) : t('on_foot')}</td>
				<td class="player-dir">${player.yaw !== undefined ? Math.round(player.yaw) + '°' : '—'}</td>
			</tr>
		`;
	},
	
	truncateName(name, maxLen) {
		if (name.length <= maxLen) return name;
		return name.substring(0, maxLen - 1) + '…';
	},
	
	filterPlayers(query) {
		const rows = this.elements.playerTableBody.querySelectorAll('tr');
		const q = query.toLowerCase();
		rows.forEach(row => {
			const name = row.querySelector('.player-name').textContent.toLowerCase();
			row.style.display = name.includes(q) ? '' : 'none';
		});
	},
	
	sortPlayers(sortBy) {
		this.updatePlayerList();
	},
	
	selectPlayer(playerId) {
		this.state.selectedPlayerId = playerId;
		
		// Update row highlight
		this.elements.playerTableBody.querySelectorAll('tr').forEach(row => {
			row.classList.toggle('selected', row.dataset.id == playerId);
		});
		
		// Highlight marker
		this.highlightMarker(playerId);
		
		// Center on player (single click)
		const player = this.state.players.find(p => p.id == playerId);
		if (player) this.centerOnPlayer(player, false);
	},
	
	followPlayer(playerId) {
		this.state.followingPlayerId = playerId;
		this.state.selectedPlayerId = playerId;
		
		// Update row highlight
		this.elements.playerTableBody.querySelectorAll('tr').forEach(row => {
			row.classList.toggle('following', row.dataset.id == playerId);
			row.classList.toggle('selected', row.dataset.id == playerId);
		});
		
		const player = this.state.players.find(p => p.id == playerId);
		if (player) this.centerOnPlayer(player, true);
	},
	
	stopFollowing() {
		this.state.followingPlayerId = null;
		this.elements.playerTableBody.querySelectorAll('tr').forEach(row => {
			row.classList.remove('following');
		});
	},
	
	centerOnPlayer(player, follow) {
		if (!player) return;
		
		const targetX = player.x;
		const targetY = -player.y; // Invert Y for screen coords
		
		if (follow) {
			this.state.followingPlayerId = player.id;
		}
		
		this.smoothPanTo(targetX, targetY);
	},
	
	highlightMarker(playerId) {
		// Remove previous highlights
		document.querySelectorAll('.player-marker.highlighted').forEach(m => m.classList.remove('highlighted'));
		
		// Add highlight
		const marker = document.querySelector(`.player-marker[data-id="${playerId}"]`);
		if (marker) {
			marker.classList.add('highlighted');
			setTimeout(() => marker.classList.remove('highlighted'), 2000);
		}
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
		console.log('[FiveM Radar] Map style:', style);
	},
	
	applyTransform() {
		const { zoom, centerX, centerY, rotation } = this.state;
		const transform = `translate(${centerX}px, ${centerY}px) scale(${zoom}) rotate(${rotation}deg)`;
		
		this.elements.radarBackground.style.transform = transform;
		this.elements.radarCayo.style.transform = transform;
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
		if (e.target.closest('.player-marker')) return;
		
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
		if (e.target.closest('.player-marker')) return;
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
			
			this.state.centerX += dx * this.CONFIG.SMOOTHING;
			this.state.centerY += dy * this.CONFIG.SMOOTHING;
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
	
	autoZoomToPlayers() {
		const players = this.getFilteredPlayers().filter(p => !p.is_local);
		if (players.length === 0) return;
		
		let minX = Infinity, maxX = -Infinity, minY = Infinity, maxY = -Infinity;
		players.forEach(p => {
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
			
			this.elements.radarBackground.hidden = inCayo;
			this.elements.radarCayo.hidden = !inCayo;
			
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
			indicator.textContent = t('cayo_perico');
			this.elements.radar.appendChild(indicator);
		}
		indicator.hidden = false;
		setTimeout(() => { if (indicator) indicator.hidden = true; }, 5000);
	},
	
	updateMapMarkers() {
		if (!this.elements.entities) return;
		
		const players = this.getFilteredPlayers();
		const existingMarkers = new Map();
		
		// Cache existing markers
		this.elements.entities.querySelectorAll('.player-marker').forEach(el => {
			existingMarkers.set(el.dataset.id, el);
		});
		
		// Update or create markers
		players.forEach(player => {
			let marker = existingMarkers.get(player.id);
			const isNew = !marker;
			
			if (isNew) {
				marker = this.createMarker(player);
				this.elements.entities.appendChild(marker);
			}
			
			this.updateMarker(marker, player);
			existingMarkers.delete(player.id);
		});
		
		// Remove stale markers
		existingMarkers.forEach(marker => marker.remove());
	},
	
	createMarker(player) {
		const marker = document.createElement('div');
		marker.className = `player-marker ${player.is_local ? 'local' : (player.is_player ? 'player' : 'npc')} ${player.in_vehicle ? 'in-vehicle' : ''}`;
		marker.dataset.id = player.id;
		marker.style.left = `${player.x}px`;
		marker.style.top = `${-player.y}px`; // Invert Y
		
		marker.innerHTML = `
			<div class="marker-dot"></div>
			<div class="marker-arrow"></div>
			<div class="marker-label">${this.truncateName(player.name || t('unknown_player'), this.state.settings.maxNameLength)}</div>
			<div class="vehicle-ring"></div>
		`;
		
		marker.addEventListener('click', (e) => {
			e.stopPropagation();
			this.selectPlayer(player.id);
		});
		
		marker.addEventListener('dblclick', (e) => {
			e.stopPropagation();
			this.followPlayer(player.id);
		});
		
		return marker;
	},
	
	updateMarker(marker, player) {
		const dot = marker.querySelector('.marker-dot');
		const arrow = marker.querySelector('.marker-arrow');
		const label = marker.querySelector('.marker-label');
		const ring = marker.querySelector('.vehicle-ring');
		
		// Position
		marker.style.left = `${player.x}px`;
		marker.style.top = `${-player.y}px`;
		
		// Rotation (yaw)
		if (this.state.settings.showArrows && player.yaw !== undefined && arrow) {
			arrow.style.transform = `translate(-50%, -50%) rotate(${player.yaw}deg)`;
			arrow.style.display = 'block';
		} else if (arrow) {
			arrow.style.display = 'none';
		}
		
		// Label
		if (label) {
			label.textContent = this.truncateName(player.name || t('unknown_player'), this.state.settings.maxNameLength);
			label.style.display = this.state.settings.showNames ? 'block' : 'none';
		}
		
		// Vehicle ring
		if (ring) {
			ring.style.display = player.in_vehicle ? 'block' : 'none';
		}
		
		// Dot size
		const scale = this.state.settings.playerDotScale || 0.7;
		if (dot) {
			const baseSize = 12;
			dot.style.width = `${baseSize * scale}px`;
			dot.style.height = `${baseSize * scale}px`;
		}
		
		// Classes
		marker.classList.toggle('local', player.is_local);
		marker.classList.toggle('player', player.is_player && !player.is_local);
		marker.classList.toggle('npc', !player.is_player && !player.is_local);
		marker.classList.toggle('in-vehicle', player.in_vehicle);
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
				if (this.state.selectedPlayerId) this.followPlayer(this.state.selectedPlayerId);
				break;
		}
	},
	
	handleResize() {
		if (this.state.autoZoom && !this.state.followingPlayerId) {
			this.autoZoomToPlayers();
		}
	},
	
	cleanup() {
		if (this.state.updateInterval) clearInterval(this.state.updateInterval);
	}
};

// Initialize on DOM ready
document.addEventListener('DOMContentLoaded', () => RadarApp.init());

// Export for debugging
window.RadarApp = RadarApp;