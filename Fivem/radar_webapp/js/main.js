/* main.js - FiveM Web Radar (professional / CS2-parity features) */
const RadarApp = {
	state: {
		connected: false,
		connecting: false,
		players: [],
		objects: [],
		localPlayer: null,
		selectedPlayerId: null,
		followingPlayerId: null,
		watchlist: [], // up to 3 ids
		stickyId: null,
		stickyUntil: 0,
		mapType: 'los_santos',
		inCayo: false,
		zoom: 1.2,
		minZoom: 0.6,
		maxZoom: 12,
		centerX: 0,
		centerY: 0,
		rotation: 0,
		followRotation: false,
		autoZoom: true,
		minimapMode: false,
		operatorMode: false,
		lastUpdate: 0,
		updateInterval: null,
		accessToken: '',
		settings: {},
		dmaStatus: 'offline',
		etag: '',
		trails: new Map(), // id -> [{x,y,t}]
		heatmap: [],
		combatIds: new Set(),
		lastPositions: new Map()
	},

	elements: {},
	canvas: null,
	ctx: null,
	raf: 0,

	CONFIG: {
		API_POLL_INTERVAL: 120,
		STALE_THRESHOLD: 5000,
		TRAIL_MS: 8000,
		TRAIL_MAX: 48,
		STICKY_MS: 1500,
		COMBAT_SPEED: 8.5, // m/s approx threshold via world units/s
		CANVAS_ENTITY_THRESHOLD: 50
	},

	init() {
		this.cacheElements();
		this.loadSettings();
		this.setupCanvas();
		this.setupEventListeners();
		this.detectAccessToken();
		this.applyTheme();
		if (window.MapRenderer) MapRenderer.bindImageLoadHandlers();
		this.startPolling();
		this.loop();
		console.log('[FiveM Radar] Initialized (multi-res + pro features)');
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
			unknownMap: document.getElementById('unknownMap'),
			offlineBanner: document.getElementById('offline-banner'),
			objectTbody: document.getElementById('object-tbody'),
			objectCount: document.getElementById('object-count'),
			objectSearch: document.getElementById('object-search'),
			minimapBtn: document.getElementById('minimap-toggle'),
			operatorBtn: document.getElementById('operator-toggle')
		};
	},

	setupCanvas() {
		let c = document.getElementById('radar-canvas');
		if (!c && this.elements.radar) {
			c = document.createElement('canvas');
			c.id = 'radar-canvas';
			c.className = 'radar-canvas';
			this.elements.radar.appendChild(c);
		}
		this.canvas = c;
		this.ctx = c ? c.getContext('2d') : null;
		this.resizeCanvas();
		window.addEventListener('resize', () => this.resizeCanvas());
	},

	resizeCanvas() {
		if (!this.canvas || !this.elements.radar) return;
		const rect = this.elements.radar.getBoundingClientRect();
		const dpr = Math.min(window.devicePixelRatio || 1, 3);
		this.canvas.width = Math.max(1, Math.floor(rect.width * dpr));
		this.canvas.height = Math.max(1, Math.floor(rect.height * dpr));
		this.canvas.style.width = rect.width + 'px';
		this.canvas.style.height = rect.height + 'px';
		if (this.ctx) this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
	},

	loadSettings() {
		try {
			this.state.settings = JSON.parse(localStorage.getItem('fivem_radar_settings') || '{}');
		} catch { this.state.settings = {}; }
		const s = this.state.settings;
		this.state.followRotation = !!s.followRotation;
		this.state.autoZoom = s.autoZoom !== false;
		this.state.minimapMode = !!s.minimapMode;
		this.state.operatorMode = !!s.operatorMode;
		this.state.watchlist = Array.isArray(s.watchlist) ? s.watchlist.slice(0, 3) : [];
		document.body.classList.toggle('minimap-mode', this.state.minimapMode);
		document.body.classList.toggle('operator-mode', this.state.operatorMode);
	},

	saveSettings() {
		this.state.settings.followRotation = this.state.followRotation;
		this.state.settings.autoZoom = this.state.autoZoom;
		this.state.settings.minimapMode = this.state.minimapMode;
		this.state.settings.operatorMode = this.state.operatorMode;
		this.state.settings.watchlist = this.state.watchlist;
		localStorage.setItem('fivem_radar_settings', JSON.stringify(this.state.settings));
	},

	updateSetting(key, value) {
		this.state.settings[key] = value;
		this.saveSettings();
	},

	applyTheme() {
		document.documentElement.setAttribute('data-theme', this.state.settings.theme || 'omnighost-dark');
	},

	detectAccessToken() {
		const hash = (location.hash || '').replace(/^#/, '');
		const q = new URLSearchParams(location.search);
		this.state.accessToken = q.get('t') || q.get('token') || hash || '';
	},

	setupEventListeners() {
		const r = this.elements.radar;
		if (r) {
			r.addEventListener('wheel', (e) => {
				e.preventDefault();
				const dir = e.deltaY > 0 ? -1 : 1;
				this.state.zoom = Math.min(this.state.maxZoom, Math.max(this.state.minZoom,
					this.state.zoom * (1 + dir * 0.12)));
				this.state.autoZoom = false;
				this.syncMapTransform();
			}, { passive: false });
			let dragging = false, lx = 0, ly = 0;
			r.addEventListener('mousedown', (e) => { dragging = true; lx = e.clientX; ly = e.clientY; });
			window.addEventListener('mouseup', () => { dragging = false; });
			window.addEventListener('mousemove', (e) => {
				if (!dragging) return;
				this.state.centerX += e.clientX - lx;
				this.state.centerY += e.clientY - ly;
				lx = e.clientX; ly = e.clientY;
				this.state.autoZoom = false;
				this.syncMapTransform();
			});
		}
		if (this.elements.playerSearch)
			this.elements.playerSearch.addEventListener('input', () => this.updatePlayerList());
		if (this.elements.playerSort)
			this.elements.playerSort.addEventListener('change', () => this.updatePlayerList());
		if (this.elements.objectSearch)
			this.elements.objectSearch.addEventListener('input', () => this.updateObjectList());
		if (this.elements.settingsToggle)
			this.elements.settingsToggle.addEventListener('click', () => this.toggleSettings());
		if (this.elements.minimapBtn)
			this.elements.minimapBtn.addEventListener('click', () => this.toggleMinimap());
		if (this.elements.operatorBtn)
			this.elements.operatorBtn.addEventListener('click', () => this.toggleOperator());

		document.addEventListener('keydown', (e) => {
			if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA')) return;
			if (e.key === '/' || (e.key === 'f' && e.ctrlKey)) {
				e.preventDefault();
				this.elements.playerSearch?.focus();
			}
			if (e.key === 'm') this.toggleMinimap();
			if (e.key === 'r') {
				this.state.followRotation = !this.state.followRotation;
				this.saveSettings();
			}
			if (e.key === 'Escape') {
				this.state.selectedPlayerId = null;
				this.state.followingPlayerId = null;
			}
			if (e.key === 'f' && this.state.selectedPlayerId)
				this.state.followingPlayerId = this.state.selectedPlayerId;
		});
	},

	toggleSettings() {
		const p = this.elements.settingsPanel;
		if (!p) return;
		if (p.hasAttribute('hidden')) p.removeAttribute('hidden');
		else p.setAttribute('hidden', '');
	},

	toggleMinimap() {
		this.state.minimapMode = !this.state.minimapMode;
		document.body.classList.toggle('minimap-mode', this.state.minimapMode);
		this.saveSettings();
		this.resizeCanvas();
		this.syncMapTransform();
	},

	toggleOperator() {
		this.state.operatorMode = !this.state.operatorMode;
		document.body.classList.toggle('operator-mode', this.state.operatorMode);
		this.saveSettings();
	},

	syncMapTransform() {
		if (window.MapRenderer)
			MapRenderer.applyMapImageTransform(this.state, this.state.mapType);
	},

	startPolling() {
		if (this.state.updateInterval) clearInterval(this.state.updateInterval);
		this.state.updateInterval = setInterval(() => this.pollState(), this.CONFIG.API_POLL_INTERVAL);
		this.pollState();
	},

	async pollState() {
		if (this.state.connecting) return;
		this.state.connecting = true;
		try {
			const headers = { 'Accept': 'application/json' };
			if (this.state.accessToken) headers['Authorization'] = `Bearer ${this.state.accessToken}`;
			if (this.state.etag) headers['If-None-Match'] = this.state.etag;
			const response = await fetch('/api/state', { cache: 'no-store', headers });
			if (response.status === 304) {
				this.updateConnectionStatus(true);
				return;
			}
			if (!response.ok) throw new Error(`HTTP ${response.status}`);
			const et = response.headers.get('ETag');
			if (et) this.state.etag = et;
			const data = await response.json();
			this.handleStateUpdate(data);
			this.updateConnectionStatus(true);
		} catch (err) {
			console.warn('[FiveM Radar] Poll failed:', err.message);
			this.updateConnectionStatus(false);
		} finally {
			this.state.connecting = false;
		}
	},

	updateConnectionStatus(connected) {
		const was = this.state.connected;
		this.state.connected = connected;
		if (this.elements.connStatus) {
			if (connected) {
				this.elements.connStatus.textContent = (window.t && t('radar_online')) || 'Online';
				this.elements.connStatus.className = 'conn-status conn-status--online';
			} else {
				this.elements.connStatus.textContent = (window.t && t('dma_offline')) || 'DMA offline';
				this.elements.connStatus.className = 'conn-status conn-status--error';
			}
		}
		if (this.elements.offlineBanner)
			this.elements.offlineBanner.hidden = connected;
		if (this.elements.unknownMap)
			this.elements.unknownMap.hidden = connected;
		if (this.elements.radar)
			this.elements.radar.hidden = false;
	},

	handleStateUpdate(data) {
		if (!data) return;
		this.state.players = Array.isArray(data.players) ? data.players : [];
		this.state.objects = Array.isArray(data.objects) ? data.objects : [];
		this.state.lastUpdate = Date.now();
		if (data.dma_status) this.state.dmaStatus = data.dma_status;
		this.state.localPlayer = this.state.players.find(p => p.is_local || p.local) || null;

		this.updateTrailsAndCombat();
		this.updateHeatmapSample();

		// Sticky selection
		if (this.state.stickyId && Date.now() < this.state.stickyUntil) {
			const still = this.state.players.find(p => String(p.id) === String(this.state.stickyId));
			if (still) this.state.selectedPlayerId = still.id;
		}

		if (this.state.localPlayer && window.MapRenderer) {
			const mt = MapRenderer.getMapTypeForPosition(this.state.localPlayer);
			this.setMapType(mt);
			if (this.state.followRotation && typeof this.state.localPlayer.yaw === 'number')
				this.state.rotation = this.state.localPlayer.yaw;
		}

		if (this.state.followingPlayerId) this.followPlayer(this.state.followingPlayerId);
		else if (this.state.autoZoom) this.autoZoomToPlayers();

		this.updatePlayerList();
		this.updateObjectList();
		this.syncMapTransform();
	},

	updateTrailsAndCombat() {
		const now = Date.now();
		const nextCombat = new Set();
		for (const p of this.state.players) {
			const id = String(p.id);
			const x = p.x, y = p.y;
			if (typeof x !== 'number' || typeof y !== 'number') continue;
			let trail = this.state.trails.get(id);
			if (!trail) { trail = []; this.state.trails.set(id, trail); }
			const last = this.state.lastPositions.get(id);
			if (last) {
				const dt = (now - last.t) / 1000;
				if (dt > 0.05 && dt < 2) {
					const dist = Math.hypot(x - last.x, y - last.y);
					const speed = dist / dt;
					if (speed > this.CONFIG.COMBAT_SPEED) nextCombat.add(id);
				}
			}
			this.state.lastPositions.set(id, { x, y, t: now });
			trail.push({ x, y, t: now });
			while (trail.length > this.CONFIG.TRAIL_MAX) trail.shift();
			while (trail.length && now - trail[0].t > this.CONFIG.TRAIL_MS) trail.shift();
		}
		this.state.combatIds = nextCombat;
	},

	updateHeatmapSample() {
		if (!this.state.settings.heatmap) return;
		for (const p of this.state.players) {
			if (typeof p.x === 'number' && typeof p.y === 'number')
				this.state.heatmap.push({ x: p.x, y: p.y, t: Date.now() });
		}
		const cut = Date.now() - 120000;
		this.state.heatmap = this.state.heatmap.filter(h => h.t > cut).slice(-400);
	},

	setMapType(type) {
		if (this.state.mapType === type) return;
		this.state.mapType = type;
		this.state.inCayo = type === 'cayo_perico';
		const bg = this.elements.radarBackground;
		const cayo = this.elements.radarCayo;
		const radar = this.elements.radar;
		if (radar) radar.classList.toggle('cayo-active', this.state.inCayo);
		// Crossfade via CSS classes
		if (bg && cayo) {
			if (this.state.inCayo) {
				cayo.hidden = false;
				requestAnimationFrame(() => {
					cayo.classList.add('map-visible');
					bg.classList.remove('map-visible');
				});
			} else {
				bg.classList.add('map-visible');
				cayo.classList.remove('map-visible');
				setTimeout(() => { if (!this.state.inCayo) cayo.hidden = true; }, 400);
			}
		}
	},

	fuzzyMatch(query, text) {
		if (!query) return true;
		text = (text || '').toLowerCase();
		query = query.toLowerCase();
		if (text.includes(query)) return true;
		// subsequence fuzzy
		let i = 0;
		for (const ch of text) {
			if (ch === query[i]) i++;
			if (i >= query.length) return true;
		}
		return false;
	},

	getFilteredPlayers() {
		const q = (this.elements.playerSearch?.value || '').trim();
		return this.state.players.filter(p => {
			if (p.is_local && this.state.settings.show_local === false) return false;
			if (!q) return true;
			return this.fuzzyMatch(q, p.name) || this.fuzzyMatch(q, p.vehicle) ||
				String(p.id).includes(q);
		});
	},

	updatePlayerList() {
		if (!this.elements.playerTableBody) return;
		const sortBy = this.elements.playerSort?.value || 'distance';
		const players = this.getFilteredPlayers().slice();
		players.sort((a, b) => {
			// watchlist first
			const aw = this.state.watchlist.includes(String(a.id)) ? 0 : 1;
			const bw = this.state.watchlist.includes(String(b.id)) ? 0 : 1;
			if (aw !== bw) return aw - bw;
			if (sortBy === 'name') return (a.name || '').localeCompare(b.name || '');
			if (sortBy === 'health') return (b.health || 0) - (a.health || 0);
			return (a.distance || 0) - (b.distance || 0);
		});
		if (this.elements.playerCount)
			this.elements.playerCount.textContent = String(players.length);

		const useDom = players.length < this.CONFIG.CANVAS_ENTITY_THRESHOLD;
		this.elements.playerTableBody.innerHTML = players.map(p => {
			const id = String(p.id);
			const sel = String(this.state.selectedPlayerId) === id ? ' selected' : '';
			const pin = this.state.watchlist.includes(id) ? '📌 ' : '';
			const combat = this.state.combatIds.has(id) ? ' combat' : '';
			return `<tr class="player-row${sel}${combat}" data-id="${id}">
				<td>${pin}${this.escape(p.name || 'Unknown')}</td>
				<td>${(p.distance != null ? Math.round(p.distance) + 'm' : '—')}</td>
				<td>${p.health != null ? Math.round(p.health) : '—'}</td>
				<td>${p.armor != null ? Math.round(p.armor) : '—'}</td>
				<td>${this.escape(p.vehicle || '')}</td>
				<td>${p.yaw != null ? Math.round(p.yaw) + '°' : '—'}</td>
			</tr>`;
		}).join('');

		this.elements.playerTableBody.querySelectorAll('tr.player-row').forEach(row => {
			row.addEventListener('click', () => this.selectPlayer(row.getAttribute('data-id'), true));
			row.addEventListener('dblclick', () => {
				const id = row.getAttribute('data-id');
				this.toggleWatchlist(id);
				this.state.followingPlayerId = id;
				this.selectPlayer(id, true);
			});
		});
	},

	updateObjectList() {
		if (!this.elements.objectTbody) return;
		const q = (this.elements.objectSearch?.value || '').trim().toLowerCase();
		let objs = this.state.objects.slice();
		if (q) {
			objs = objs.filter(o =>
				this.fuzzyMatch(q, o.display || o.name) ||
				this.fuzzyMatch(q, o.category) ||
				String(o.hash || '').includes(q));
		}
		objs.sort((a, b) => (a.dist || 0) - (b.dist || 0));
		if (this.elements.objectCount)
			this.elements.objectCount.textContent = String(objs.length);
		this.elements.objectTbody.innerHTML = objs.map(o => {
			const id = String(o.id || o.hash);
			return `<tr class="object-row" data-id="${this.escape(id)}">
				<td>${this.escape(o.display || o.name || o.hash || '?')}</td>
				<td>${this.escape(o.category || '')}</td>
				<td>${o.dist != null ? Math.round(o.dist) + 'm' : '—'}</td>
			</tr>`;
		}).join('') || `<tr><td colspan="3" class="muted">Sem objetos</td></tr>`;
		this.elements.objectTbody.querySelectorAll('tr.object-row').forEach(row => {
			row.addEventListener('click', () => {
				const o = this.state.objects.find(x => String(x.id || x.hash) === row.getAttribute('data-id'));
				if (o && window.MapRenderer)
					MapRenderer.animateToWorld(this.state, o, this.state.mapType, { zoom: 4 });
			});
		});
	},

	escape(s) {
		return String(s ?? '').replace(/[&<>"']/g, c => ({
			'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
		})[c]);
	},

	selectPlayer(id, zoom) {
		this.state.selectedPlayerId = id;
		this.state.stickyId = id;
		this.state.stickyUntil = Date.now() + this.CONFIG.STICKY_MS;
		this.updatePlayerList();
		const p = this.state.players.find(x => String(x.id) === String(id));
		if (zoom && p && window.MapRenderer)
			MapRenderer.animateToWorld(this.state, p, this.state.mapType, { zoom: Math.max(this.state.zoom, 3.5) });
	},

	toggleWatchlist(id) {
		id = String(id);
		const i = this.state.watchlist.indexOf(id);
		if (i >= 0) this.state.watchlist.splice(i, 1);
		else {
			if (this.state.watchlist.length >= 3) this.state.watchlist.shift();
			this.state.watchlist.push(id);
		}
		this.saveSettings();
		this.updatePlayerList();
	},

	followPlayer(id) {
		const p = this.state.players.find(x => String(x.id) === String(id));
		if (!p || !window.MapRenderer) return;
		const screen = MapRenderer.worldToRadar(p, { ...this.state, centerX: 0, centerY: 0 }, this.state.mapType);
		const m = MapRenderer.getRadarMetrics();
		this.state.centerX += (m.centerX - screen.x) * 0.2;
		this.state.centerY += (m.centerY - screen.y) * 0.2;
	},

	autoZoomToPlayers() {
		if (!this.state.players.length || !window.MapRenderer) return;
		// Lightweight: keep current zoom; only soft-center on local
		const lp = this.state.localPlayer;
		if (lp) {
			const screen = MapRenderer.worldToRadar(lp, { ...this.state, centerX: 0, centerY: 0 }, this.state.mapType);
			const m = MapRenderer.getRadarMetrics();
			this.state.centerX += (m.centerX - screen.x) * 0.08;
			this.state.centerY += (m.centerY - screen.y) * 0.08;
		}
	},

	loop() {
		this.drawMarkers();
		this.raf = requestAnimationFrame(() => this.loop());
	},

	drawMarkers() {
		if (!this.ctx || !this.canvas) return;
		const ctx = this.ctx;
		const rect = this.elements.radar.getBoundingClientRect();
		ctx.clearRect(0, 0, rect.width, rect.height);
		if (!this.state.connected) return;

		// Heatmap
		if (this.state.settings.heatmap && this.state.heatmap.length) {
			for (const h of this.state.heatmap) {
				const s = MapRenderer.worldToRadar(h, this.state, this.state.mapType);
				ctx.fillStyle = 'rgba(232,192,64,0.04)';
				ctx.beginPath();
				ctx.arc(s.x, s.y, 18, 0, Math.PI * 2);
				ctx.fill();
			}
		}

		// Trails
		if (this.state.settings.showTrails !== false) {
			for (const [id, trail] of this.state.trails) {
				if (trail.length < 2) continue;
				ctx.beginPath();
				let first = true;
				for (const pt of trail) {
					const s = MapRenderer.worldToRadar(pt, this.state, this.state.mapType);
					if (first) { ctx.moveTo(s.x, s.y); first = false; }
					else ctx.lineTo(s.x, s.y);
				}
				ctx.strokeStyle = this.state.watchlist.includes(id) ? 'rgba(232,192,64,0.55)' : 'rgba(100,180,255,0.35)';
				ctx.lineWidth = 2;
				ctx.stroke();
			}
		}

		// Objects
		if (this.state.settings.showObjects !== false) {
			for (const o of this.state.objects) {
				const s = MapRenderer.worldToRadar(o, this.state, this.state.mapType);
				const col = this.categoryColor(o.category);
				ctx.fillStyle = col;
				ctx.fillRect(s.x - 3, s.y - 3, 6, 6);
				if (this.state.settings.showObjectNames && (o.display || o.name)) {
					ctx.fillStyle = '#ddd';
					ctx.font = '10px Segoe UI, sans-serif';
					ctx.fillText(o.display || o.name, s.x + 6, s.y + 3);
				}
			}
		}

		// Players
		for (const p of this.state.players) {
			if (typeof p.x !== 'number') continue;
			const s = MapRenderer.worldToRadar(p, this.state, this.state.mapType);
			const id = String(p.id);
			const isLocal = !!(p.is_local || p.local);
			const selected = String(this.state.selectedPlayerId) === id;
			const watched = this.state.watchlist.includes(id);
			const combat = this.state.combatIds.has(id);

			// look cone
			if (typeof p.yaw === 'number' && this.state.settings.showLookCone !== false) {
				const rad = ((p.yaw - (this.state.followRotation ? this.state.rotation : 0)) - 90) * Math.PI / 180;
				ctx.beginPath();
				ctx.moveTo(s.x, s.y);
				ctx.arc(s.x, s.y, 22, rad - 0.35, rad + 0.35);
				ctx.closePath();
				ctx.fillStyle = isLocal ? 'rgba(80,200,120,0.2)' : 'rgba(220,80,80,0.18)';
				ctx.fill();
			}

			// armor ring
			const hp = Math.max(0, Math.min(100, p.health ?? 100));
			const ar = Math.max(0, Math.min(100, p.armor ?? 0));
			ctx.beginPath();
			ctx.arc(s.x, s.y, 11, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * (ar / 100));
			ctx.strokeStyle = 'rgba(100,160,255,0.9)';
			ctx.lineWidth = 2;
			ctx.stroke();
			// hp ring
			ctx.beginPath();
			ctx.arc(s.x, s.y, 8, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * (hp / 100));
			ctx.strokeStyle = hp > 50 ? '#3dce6a' : hp > 25 ? '#e0c040' : '#e05050';
			ctx.lineWidth = 2;
			ctx.stroke();

			// body
			ctx.beginPath();
			ctx.arc(s.x, s.y, selected ? 6.5 : 5, 0, Math.PI * 2);
			ctx.fillStyle = isLocal ? '#4ade80' : watched ? '#e8c040' : combat ? '#ff6b4a' : '#f07178';
			ctx.fill();
			if (selected) {
				ctx.strokeStyle = '#fff';
				ctx.lineWidth = 1.5;
				ctx.stroke();
			}

			// yaw arrow
			if (typeof p.yaw === 'number') {
				const rad = ((p.yaw - (this.state.followRotation ? this.state.rotation : 0)) - 90) * Math.PI / 180;
				const len = 14;
				ctx.beginPath();
				ctx.moveTo(s.x, s.y);
				ctx.lineTo(s.x + Math.cos(rad) * len, s.y + Math.sin(rad) * len);
				ctx.strokeStyle = '#fff';
				ctx.lineWidth = 2;
				ctx.stroke();
			}

			if (this.state.settings.showNames !== false && p.name) {
				ctx.font = '11px Segoe UI, sans-serif';
				ctx.fillStyle = 'rgba(0,0,0,0.55)';
				const label = p.name.length > 18 ? p.name.slice(0, 16) + '…' : p.name;
				const tw = ctx.measureText(label).width;
				ctx.fillRect(s.x - tw / 2 - 3, s.y + 10, tw + 6, 14);
				ctx.fillStyle = '#f2f2f2';
				ctx.textAlign = 'center';
				ctx.fillText(label, s.x, s.y + 21);
				ctx.textAlign = 'left';
			}
		}
	},

	categoryColor(cat) {
		const c = (cat || '').toLowerCase();
		if (c.includes('loot')) return '#e8c040';
		if (c.includes('mission')) return '#60a5fa';
		if (c.includes('police')) return '#3b82f6';
		if (c.includes('medical')) return '#f87171';
		if (c.includes('vehicle')) return '#a78bfa';
		if (c.includes('container')) return '#34d399';
		return '#94a3b8';
	}
};

document.addEventListener('DOMContentLoaded', () => RadarApp.init());
window.RadarApp = RadarApp;
