/* main.js - FiveM Web Radar (professional / CS2-parity features) */
const RadarApp = {
	state: {
		connected: false,
		connecting: false,
		players: [],
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
		// Full-map mode: map imagery stays stable while markers move, like the
		// CS2 radar. Users can still choose a player to intentionally focus it.
		autoZoom: false,
		minimapMode: false,
		operatorMode: false,
		cameraMode: 'free',
		activeFilter: 'all', distanceLimit: 0,
		measure: { active: false, start: null, end: null },
		replay: { live: true, snapshots: [], maxAge: 5 * 60 * 1000, index: 0 },
		annotations: { active: false, points: [] },
		session: { startedAt: Date.now(), distance: 0, maxSpeed: 0, positions: new Map() },
		transport: 'http',
		connectionState: 'offline',
		lastSnapshotAt: 0,
		previousSnapshot: null,
		currentSnapshot: null,
		lastUpdate: 0,
		updateInterval: null,
		accessToken: '',
		settings: {},
		dmaStatus: 'offline',
		etag: '',
		trails: new Map(), // id -> [{x,y,t}]
		heatmap: new Map(),
		lastPositions: new Map(),
		playerRows: new Map(),
		events: [], previousPlayers: new Map()
	},

	elements: {},
	canvas: null,
	ctx: null,
	raf: 0,
	transportClient: null,

	CONFIG: {
		API_POLL_INTERVAL: 120,
		STALE_THRESHOLD: 5000,
		STICKY_MS: 1500,
		TRAIL_MIN_DISTANCE: 2.0,
		TRAIL_MAX_AGE_MS: 5 * 60 * 1000,
		HEATMAP_CELL_SIZE: 80,
		HEATMAP_MAX_AGE_MS: 2 * 60 * 1000,
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
			minimapBtn: document.getElementById('minimap-toggle'),
			operatorBtn: document.getElementById('operator-toggle'),
			playerInspector: document.getElementById('player-inspector'),
			centerLocal: document.getElementById('center-local'),
			fitAll: document.getElementById('fit-all'),
			resetView: document.getElementById('reset-view'),
			eventList: document.getElementById('event-list'),
			quickFilters: document.getElementById('quick-filters'),
			distanceFilter: document.getElementById('distance-filter'),
			diagnostics: document.getElementById('diagnostics'),
			measureToggle: document.getElementById('measure-toggle'),
			measureReadout: document.getElementById('measure-readout'),
			replayLive: document.getElementById('replay-live'),
			replayTimeline: document.getElementById('replay-timeline'),
			replayLabel: document.getElementById('replay-label'),
			drawToggle: document.getElementById('draw-toggle'),
			clearAnnotations: document.getElementById('clear-annotations'),
			statObserved: document.getElementById('stat-observed'),
			statDistance: document.getElementById('stat-distance'),
			statSpeed: document.getElementById('stat-speed'),
			preset: document.getElementById('set-preset')
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
		// Do not restore the old follow-the-local-player behaviour. It made the
		// entire map drift on every state update and obscured movement of markers.
		this.state.autoZoom = false;
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

	applyPreset(name) {
		const presets = {
			clean: { showNames: true, showTrails: false, showLookCone: false, heatmap: false, followRotation: false },
			tactical: { showNames: true, showTrails: true, showLookCone: true, heatmap: false, followRotation: false },
			analysis: { showNames: true, showTrails: true, showLookCone: true, heatmap: true, followRotation: false },
			operator: { showNames: true, showTrails: true, showLookCone: true, heatmap: false, followRotation: true }
		};
		const preset = presets[name];
		if (!preset) return;
		Object.assign(this.state.settings, preset);
		this.state.followRotation = preset.followRotation;
		for (const [id, key] of Object.entries({ 'set-names': 'showNames', 'set-trails': 'showTrails', 'set-cone': 'showLookCone', 'set-heatmap': 'heatmap', 'set-rotate': 'followRotation' })) {
			const control = document.getElementById(id); if (control) control.checked = !!preset[key];
		}
		if (name === 'operator' && !this.state.operatorMode) this.toggleOperator();
		this.state.settings.preset = name;
		this.saveSettings();
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
				const box = r.getBoundingClientRect();
				MapRenderer.zoomAt(this.state, { x: e.clientX - box.left, y: e.clientY - box.top },
					e.deltaY > 0 ? 0.88 : 1.14, this.state.mapType);
				this.state.cameraMode = 'free';
				this.syncMapTransform();
			}, { passive: false });
			let drag = null;
			const pointers = new Map();
			let pinchDistance = 0;
			r.addEventListener('pointerdown', (e) => {
				r.setPointerCapture?.(e.pointerId);
				pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
				if (pointers.size === 1) drag = { x: e.clientX, y: e.clientY, moved: false };
				if (pointers.size === 2) {
					const p = [...pointers.values()];
					pinchDistance = Math.hypot(p[1].x - p[0].x, p[1].y - p[0].y);
					drag = null;
				}
			});
			r.addEventListener('pointermove', (e) => {
				if (!pointers.has(e.pointerId)) return;
				pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
				if (pointers.size === 2) {
					const p = [...pointers.values()];
					const next = Math.hypot(p[1].x - p[0].x, p[1].y - p[0].y);
					if (pinchDistance > 0 && next > 0) {
						const box = r.getBoundingClientRect();
						MapRenderer.zoomAt(this.state, { x: (p[0].x + p[1].x) / 2 - box.left, y: (p[0].y + p[1].y) / 2 - box.top }, next / pinchDistance, this.state.mapType);
						this.state.cameraMode = 'free';
						this.syncMapTransform();
					}
					pinchDistance = next;
					return;
				}
				if (!drag) return;
				const dx = e.clientX - drag.x, dy = e.clientY - drag.y;
				if (dx || dy) {
					MapRenderer.panBy(this.state, dx, dy);
					this.state.cameraMode = 'free';
					drag.x = e.clientX; drag.y = e.clientY; drag.moved = true;
					this.syncMapTransform();
				}
			});
			const endPointer = (e) => { pointers.delete(e.pointerId); if (!pointers.size) drag = null; };
			r.addEventListener('pointerup', endPointer);
			r.addEventListener('pointercancel', endPointer);
			r.addEventListener('dblclick', (e) => {
				const box = r.getBoundingClientRect();
				MapRenderer.zoomAt(this.state, { x: e.clientX - box.left, y: e.clientY - box.top }, 1.5, this.state.mapType);
				this.state.cameraMode = 'free'; this.syncMapTransform();
			});
			r.addEventListener('click', e => {
				if (!this.state.measure.active && !this.state.annotations.active) return;
				const box = r.getBoundingClientRect();
				const point = MapRenderer.screenToWorld({ x:e.clientX-box.left, y:e.clientY-box.top }, this.state, this.state.mapType);
				if (this.state.annotations.active) {
					this.state.annotations.points.push({ ...point, label: String(this.state.annotations.points.length + 1) });
					return;
				}
				if (!this.state.measure.start || this.state.measure.end) { this.state.measure.start = point; this.state.measure.end = null; }
				else this.state.measure.end = point;
				this.updateMeasureReadout();
			});
		}
		if (this.elements.playerSearch)
			this.elements.playerSearch.addEventListener('input', () => this.updatePlayerList());
		if (this.elements.playerSort)
			this.elements.playerSort.addEventListener('change', () => this.updatePlayerList());
		if (this.elements.settingsToggle)
			this.elements.settingsToggle.addEventListener('click', () => this.toggleSettings());
		if (this.elements.minimapBtn)
			this.elements.minimapBtn.addEventListener('click', () => this.toggleMinimap());
		if (this.elements.operatorBtn)
			this.elements.operatorBtn.addEventListener('click', () => this.toggleOperator());
		this.elements.centerLocal?.addEventListener('click', () => this.centerLocal());
		this.elements.fitAll?.addEventListener('click', () => this.fitAllPlayers());
		this.elements.resetView?.addEventListener('click', () => { MapRenderer.resetView(this.state); this.syncMapTransform(); });
		this.elements.quickFilters?.addEventListener('click', e => {
			const button = e.target.closest('[data-filter]'); if (!button) return;
			this.state.activeFilter = button.dataset.filter;
			this.elements.quickFilters.querySelectorAll('[data-filter]').forEach(b => b.classList.toggle('active', b === button));
			this.updatePlayerList();
		});
		this.elements.distanceFilter?.addEventListener('change', e => { this.state.distanceLimit = Number(e.target.value) || 0; this.updatePlayerList(); });
		this.elements.measureToggle?.addEventListener('click', () => { this.state.measure.active = !this.state.measure.active; this.state.measure.start = this.state.measure.end = null; this.updateMeasureReadout(); });
		this.elements.replayLive?.addEventListener('click', () => { this.state.replay.live = true; this.elements.replayLive.textContent = 'LIVE'; });
		this.elements.replayLive?.addEventListener('click', () => this.showLiveSnapshot());
		this.elements.replayTimeline?.addEventListener('input', e => this.selectReplaySnapshot(Number(e.target.value)));
		this.elements.drawToggle?.addEventListener('click', () => {
			this.state.annotations.active = !this.state.annotations.active;
			this.elements.drawToggle.classList.toggle('active', this.state.annotations.active);
		});
		this.elements.clearAnnotations?.addEventListener('click', () => { this.state.annotations.points = []; });
		this.elements.preset?.addEventListener('change', e => this.applyPreset(e.target.value));

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
				this.follow(this.state.selectedPlayerId);
			if (e.key === '0') { MapRenderer.resetView(this.state); this.syncMapTransform(); }
		});
	},

	follow(id) {
		this.state.followingPlayerId = id;
		this.state.cameraMode = 'follow';
	},

	centerLocal() {
		if (this.state.localPlayer) this.follow(this.state.localPlayer.id);
	},

	fitAllPlayers() {
		const valid = this.state.players.filter(p => Number.isFinite(p.x) && Number.isFinite(p.y));
		if (!valid.length || !window.MapRenderer) return;
		const m = MapRenderer.getRadarMetrics();
		const xs = valid.map(p => p.x), ys = valid.map(p => p.y);
		const span = Math.max(Math.max(...xs) - Math.min(...xs), Math.max(...ys) - Math.min(...ys), 100);
		const cal = MapRenderer.ensureCalibrated(this.state.mapType);
		const fit = Math.max(m.width / cal.imageSize.width, m.height / cal.imageSize.height);
		this.state.zoom = Math.max(this.state.minZoom, Math.min(this.state.maxZoom, Math.min(m.width, m.height) / (span / Math.max(cal.scaleX, cal.scaleY)) / fit * 0.65));
		const center = { x: (Math.min(...xs) + Math.max(...xs)) / 2, y: (Math.min(...ys) + Math.max(...ys)) / 2 };
		const projected = MapRenderer.worldToRadar(center, { ...this.state, centerX: 0, centerY: 0 }, this.state.mapType);
		this.state.centerX = m.centerX - projected.x; this.state.centerY = m.centerY - projected.y;
		this.state.cameraMode = 'free'; this.syncMapTransform();
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
		this.transportClient?.stop();
		if (!window.RadarTransport) { this.pollState(); return; }
		this.transportClient = new RadarTransport({
			token: this.state.accessToken,
			onSnapshot: data => this.handleStateUpdate(data),
			onStatus: info => this.updateConnectionStatus(info),
			onMetrics: metrics => { this.state.transportMetrics = metrics; this.updateDiagnostics(); }
		});
		this.transportClient.start();
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

	updateConnectionStatus(info) {
		const detail = typeof info === 'boolean' ? { state: info ? 'connected' : 'offline', mode: 'http' } : info;
		const connected = detail.state === 'connected';
		this.state.connected = connected;
		this.state.connectionState = detail.state;
		this.state.transport = detail.mode || this.state.transport;
		if (this.elements.connStatus) {
			if (connected) {
				this.elements.connStatus.textContent = `${detail.mode || 'HTTP'} · ${(window.t && t('radar_online')) || 'Online'}`;
				this.elements.connStatus.className = 'conn-status conn-status--online';
			} else if (detail.state === 'reconnecting' || detail.state === 'stale') {
				this.elements.connStatus.textContent = detail.state === 'stale' ? 'Dados desatualizados' : 'A reconectar…';
				this.elements.connStatus.className = 'conn-status conn-status--connecting';
			} else {
				this.elements.connStatus.textContent = (window.t && t('dma_offline')) || 'DMA offline';
				this.elements.connStatus.className = 'conn-status conn-status--error';
			}
		}
		if (this.elements.offlineBanner)
			this.elements.offlineBanner.hidden = connected || detail.state === 'reconnecting';
		if (this.elements.unknownMap)
			this.elements.unknownMap.hidden = connected;
		if (this.elements.radar)
			this.elements.radar.hidden = false;
	},

	handleStateUpdate(data) {
		if (!data) return;
		// Accept the future compact protocol as well as today's full snapshots.
		// Missing fields retain their last known value; removals are explicit.
		if (Array.isArray(data.upserts) && this.state.currentSnapshot) {
			const byId = new Map((this.state.currentSnapshot.players || []).map(player => [String(player.id), player]));
			for (const update of data.upserts) {
				const id = String(update.id);
				byId.set(id, { ...(byId.get(id) || {}), ...update });
			}
			for (const id of data.removedIds || []) byId.delete(String(id));
			data = { ...this.state.currentSnapshot, ...data, players: [...byId.values()] };
		}
		const capturedAt = Date.now();
		this.state.replay.snapshots.push({ t: capturedAt, data });
		while (this.state.replay.snapshots.length && Date.now() - this.state.replay.snapshots[0].t > this.state.replay.maxAge)
			this.state.replay.snapshots.shift();
		this.updateReplayTimeline();
		if (!this.state.replay.live) return;
		this.applySnapshot(data, capturedAt);
	},

	applySnapshot(data, capturedAt = Date.now()) {
		this.state.previousSnapshot = this.state.currentSnapshot;
		this.state.currentSnapshot = data;
		this.state.players = Array.isArray(data.players) ? data.players : [];
		this.state.seq = data.seq || this.state.seq || 0;
		this.state.snapshotTimestamp = data.timestamp_ms || 0;
		this.state.lastUpdate = capturedAt;
		this.state.lastSnapshotAt = performance.now();
		if (data.dma_status) this.state.dmaStatus = data.dma_status;
		this.state.localPlayer = this.state.players.find(p => p.is_local || p.local) || null;

		this.updateEventStream();
		this.updateTrails();
		this.updateHeatmapSample();
		this.updateSessionStats();

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

		this.updatePlayerList();
		this.updateInspector();
		this.syncMapTransform();
	},

	updateReplayTimeline() {
		const replay = this.state.replay, input = this.elements.replayTimeline;
		if (!input) return;
		const max = Math.max(0, replay.snapshots.length - 1);
		input.max = String(max); input.disabled = max === 0;
		if (replay.live) { replay.index = max; input.value = String(max); }
		if (this.elements.replayLabel) this.elements.replayLabel.textContent = replay.live ? 'LIVE' : 'REPLAY';
	},

	selectReplaySnapshot(index) {
		const replay = this.state.replay;
		const snapshot = replay.snapshots[index];
		if (!snapshot) return;
		replay.live = index === replay.snapshots.length - 1;
		replay.index = index;
		this.applySnapshot(snapshot.data, snapshot.t);
		this.updateReplayTimeline();
	},

	showLiveSnapshot() {
		const replay = this.state.replay;
		if (!replay.snapshots.length) return;
		replay.live = true;
		this.applySnapshot(replay.snapshots[replay.snapshots.length - 1].data, Date.now());
		this.updateReplayTimeline();
	},

	updateSessionStats() {
		const session = this.state.session, now = Date.now();
		for (const player of this.state.players) {
			const id = String(player.id);
			if (Number.isFinite(player.speed)) session.maxSpeed = Math.max(session.maxSpeed, player.speed);
			if (!Number.isFinite(player.x) || !Number.isFinite(player.y)) continue;
			const previous = session.positions.get(id);
			if (previous) {
				const delta = Math.hypot(player.x - previous.x, player.y - previous.y);
				if (delta < 1000) session.distance += delta;
			}
			session.positions.set(id, { x: player.x, y: player.y, t: now });
		}
		const elapsed = Math.floor((now - session.startedAt) / 1000);
		if (this.elements.statObserved) this.elements.statObserved.textContent = `${Math.floor(elapsed / 60)}m ${elapsed % 60}s`;
		if (this.elements.statDistance) this.elements.statDistance.textContent = session.distance >= 1000 ? `${(session.distance / 1000).toFixed(1)} km` : `${Math.round(session.distance)} m`;
		if (this.elements.statSpeed) this.elements.statSpeed.textContent = session.maxSpeed ? session.maxSpeed.toFixed(1) : '—';
	},


	updateDiagnostics() {
		const el = this.elements.diagnostics, m = this.state.transportMetrics;
		if (!el || !m) return;
		el.textContent = `${this.state.transport.toUpperCase()} · ${m.rtt || 0} ms · ${this.state.players.length} jogadores · ${m.dropped || 0} perdidos`;
	},

	updateMeasureReadout() {
		const el = this.elements.measureReadout, m = this.state.measure;
		if (!el) return;
		if (!m.active || !m.start) { el.hidden = true; return; }
		if (!m.end) { el.textContent = 'Seleciona o segundo ponto'; el.hidden = false; return; }
		const d = Math.hypot(m.end.x - m.start.x, m.end.y - m.start.y);
		el.textContent = d >= 1000 ? `${(d / 1000).toFixed(2)} km` : `${Math.round(d)} m`; el.hidden = false;
	},

	updateTrails() {
		const now = Date.now();
		const seen = new Set();
		for (const p of this.getRenderPlayers()) {
			const id = String(p.id);
			seen.add(id);
			const x = p.x, y = p.y;
			if (typeof x !== 'number' || typeof y !== 'number') continue;
			let trail = this.state.trails.get(id);
			if (!trail) { trail = []; this.state.trails.set(id, trail); }
			const last = this.state.lastPositions.get(id);
			const moved = !last || Math.hypot(x - last.x, y - last.y) >= this.CONFIG.TRAIL_MIN_DISTANCE;
			this.state.lastPositions.set(id, { x, y, t: now });
			if (moved) trail.push({ x, y, t: now });
			while (trail.length && now - trail[0].t > this.CONFIG.TRAIL_MAX_AGE_MS) trail.shift();
		}
		for (const id of this.state.trails.keys()) if (!seen.has(id)) this.state.trails.delete(id);
		for (const id of this.state.lastPositions.keys()) if (!seen.has(id)) this.state.lastPositions.delete(id);
	},

	updateEventStream() {
		const previous = this.state.previousPlayers, next = new Map(), now = new Date();
		const add = (player, message) => {
			this.state.events.unshift({ id: `${Date.now()}-${Math.random()}`, time: now, message: `${player.name || player.id} ${message}` });
		};
		for (const p of this.state.players) {
			const id = String(p.id), before = previous.get(id); next.set(id, p);
			if (!before) add(p, 'apareceu');
			else {
				if (!!before.in_vehicle !== !!p.in_vehicle) add(p, p.in_vehicle ? `entrou em ${p.vehicle || 'veículo'}` : 'saiu do veículo');
				if (Number.isFinite(before.health) && Number.isFinite(p.health) && p.health < before.health) add(p, `perdeu ${Math.round(before.health - p.health)} HP`);
				if (Number.isFinite(before.armor) && Number.isFinite(p.armor) && p.armor < before.armor) add(p, `perdeu ${Math.round(before.armor - p.armor)} AP`);
				if (before.vehicle !== p.vehicle && p.in_vehicle) add(p, `mudou para ${p.vehicle || 'veículo'}`);
			}
		}
		for (const [id, p] of previous) if (!next.has(id)) add(p, 'desapareceu');
		this.state.previousPlayers = next;
		this.state.events.length = Math.min(this.state.events.length, 250);
		if (this.elements.eventList) this.elements.eventList.replaceChildren(...this.state.events.slice(0, 8).map(e => {
			const li = document.createElement('li'); li.textContent = `${e.time.toLocaleTimeString()} · ${e.message}`; return li;
		}));
	},

	getRenderPlayers() {
		const current = this.state.players;
		const previous = this.state.previousSnapshot?.players;
		if (!Array.isArray(previous) || !this.state.lastSnapshotAt) return current;
		const alpha = Math.max(0, Math.min(1, (performance.now() - this.state.lastSnapshotAt) / this.CONFIG.API_POLL_INTERVAL));
		const before = new Map(previous.map(p => [String(p.id), p]));
		return current.map(p => {
			const old = before.get(String(p.id));
			if (!old || !Number.isFinite(old.x) || !Number.isFinite(old.y) || !Number.isFinite(p.x) || !Number.isFinite(p.y)) return p;
			const heading = (a, b) => {
				if (!Number.isFinite(a) || !Number.isFinite(b)) return b ?? a;
				let d = ((b - a + 540) % 360) - 180;
				return a + d * alpha;
			};
			return { ...p, x: old.x + (p.x - old.x) * alpha, y: old.y + (p.y - old.y) * alpha,
				z: Number.isFinite(old.z) && Number.isFinite(p.z) ? old.z + (p.z - old.z) * alpha : p.z,
				yaw: heading(old.yaw, p.yaw), heading: heading(old.heading, p.heading) };
		});
	},

	updateHeatmapSample() {
		if (!this.state.settings.heatmap) return;
		const now = Date.now();
		const cellSize = this.CONFIG.HEATMAP_CELL_SIZE;
		const bins = this.state.heatmap;
		for (const p of this.getRenderPlayers()) {
			if (!Number.isFinite(p.x) || !Number.isFinite(p.y)) continue;
			const key = `${Math.floor(p.x / cellSize)}:${Math.floor(p.y / cellSize)}`;
			const bin = bins.get(key) || { x: (Math.floor(p.x / cellSize) + .5) * cellSize, y: (Math.floor(p.y / cellSize) + .5) * cellSize, hits: 0, t: now };
			bin.hits += 1; bin.t = now; bins.set(key, bin);
		}
		for (const [key, bin] of bins) if (now - bin.t > this.CONFIG.HEATMAP_MAX_AGE_MS) bins.delete(key);
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
			const filter = this.state.activeFilter;
			if (filter === 'players' && !p.is_player) return false;
			if (filter === 'on-foot' && p.in_vehicle) return false;
			if (filter === 'vehicles' && !p.in_vehicle) return false;
			if (filter === 'pinned' && !this.state.watchlist.includes(String(p.id))) return false;
			if (filter === 'nearby' && (!Number.isFinite(p.distance) || p.distance > 250)) return false;
			if (this.state.distanceLimit && (!Number.isFinite(p.distance) || p.distance > this.state.distanceLimit)) return false;
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

		const active = new Set();
		for (const p of players) {
			const id = String(p.id); active.add(id);
			let row = this.state.playerRows.get(id);
			if (!row) {
				row = document.createElement('tr'); row.className = 'player-row'; row.dataset.id = id;
				for (let i = 0; i < 6; i++) row.appendChild(document.createElement('td'));
				row.addEventListener('click', () => this.selectPlayer(row.dataset.id, true));
				row.addEventListener('dblclick', () => { this.toggleWatchlist(row.dataset.id); this.follow(row.dataset.id); this.selectPlayer(row.dataset.id, true); });
				this.state.playerRows.set(id, row);
			}
			row.className = `player-row${String(this.state.selectedPlayerId) === id ? ' selected' : ''}${Number.isFinite(p.speed) && p.speed > 8.5 ? ' fast-movement' : ''}`;
			const cells = row.cells;
			cells[0].textContent = `${this.state.watchlist.includes(id) ? '📌 ' : ''}${p.name || 'Unknown'}`;
			cells[1].textContent = p.distance != null ? `${Math.round(p.distance)}m` : '—';
			cells[2].textContent = p.health != null ? String(Math.round(p.health)) : '—';
			cells[3].textContent = p.armor != null ? String(Math.round(p.armor)) : '—';
			cells[4].textContent = p.vehicle || '';
			cells[5].textContent = p.yaw != null ? `${Math.round(p.yaw)}°` : '—';
			this.elements.playerTableBody.appendChild(row); // reorders without recreating
		}
		for (const [id, row] of this.state.playerRows) {
			if (!active.has(id)) { row.remove(); this.state.playerRows.delete(id); }
		}
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
		this.updateInspector();
		const p = this.state.players.find(x => String(x.id) === String(id));
		if (zoom && p && window.MapRenderer)
			MapRenderer.animateToWorld(this.state, p, this.state.mapType, { zoom: Math.max(this.state.zoom, 3.5) });
	},

	updateInspector() {
		const host = this.elements.playerInspector;
		if (!host) return;
		const p = this.state.players.find(x => String(x.id) === String(this.state.selectedPlayerId));
		if (!p) { host.hidden = true; host.replaceChildren(); return; }
		const number = v => Number.isFinite(v) ? Math.round(v) : '—';
		const pos = p.position || p;
		const zone = p.zone || MapRenderer.zoneForPosition(pos) || '—';
		const vehicle = p.vehicle || 'A pé';
		const weapon = p.weapon?.name || p.weapon || '—';
		const isPinned = this.state.watchlist.includes(String(p.id));
		host.hidden = false;
		host.innerHTML = `<header><strong>${this.escape(p.name || 'Desconhecido')}</strong><span>ID ${this.escape(p.id)}</span></header>
			<div class="inspector-vitals"><span>HP <b>${number(p.health)}</b></span><span>AP <b>${number(p.armor)}</b></span><span>${p.in_vehicle ? 'VEÍCULO' : 'A PÉ'}</span></div>
			<dl><div><dt>Posição</dt><dd>${number(pos.x)}, ${number(pos.y)}, ${Number.isFinite(pos.z) ? pos.z.toFixed(1) : '—'}</dd></div>
			<div><dt>Distância</dt><dd>${number(p.distance)} m</dd></div>
			<div><dt>Direção</dt><dd>${number(p.heading ?? p.yaw)}°</dd></div>
			<div><dt>Velocidade</dt><dd>${Number.isFinite(p.speed) ? p.speed.toFixed(1) : '—'}</dd></div>
			<div><dt>Zona</dt><dd>${this.escape(zone)}</dd></div><div><dt>Veículo</dt><dd>${this.escape(vehicle)}</dd></div><div><dt>Arma</dt><dd>${this.escape(weapon)}</dd></div></dl>
			<div class="inspector-actions"><button data-action="follow">SEGUIR</button><button data-action="center">CENTRAR</button><button data-action="pin">${isPinned ? 'REMOVER PIN' : 'PIN'}</button><button data-action="trail">TRAIL</button><button data-action="copy">COPIAR</button></div>`;
		host.querySelector('[data-action="follow"]')?.addEventListener('click', () => this.follow(p.id));
		host.querySelector('[data-action="center"]')?.addEventListener('click', () => MapRenderer.animateToWorld(this.state, p, this.state.mapType, { zoom: Math.max(this.state.zoom, 3) }));
		host.querySelector('[data-action="pin"]')?.addEventListener('click', () => { this.toggleWatchlist(p.id); this.updateInspector(); });
		host.querySelector('[data-action="trail"]')?.addEventListener('click', () => { this.state.settings.showTrails = true; this.saveSettings(); });
		host.querySelector('[data-action="copy"]')?.addEventListener('click', () => navigator.clipboard?.writeText(`${p.id} · ${pos.x}, ${pos.y}, ${pos.z ?? ''}`));
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
		this.checkConnectionHealth();
		this.drawMarkers();
		this.raf = requestAnimationFrame(() => this.loop());
	},

	checkConnectionHealth() {
		if (!this.state.lastUpdate || this.state.connectionState === 'offline') return;
		const age = Date.now() - this.state.lastUpdate;
		if (age > this.CONFIG.STALE_THRESHOLD && this.state.connectionState !== 'stale')
			this.updateConnectionStatus({ state: 'stale', mode: this.state.transport, age });
	},

	drawMarkers() {
		if (!this.ctx || !this.canvas) return;
		const ctx = this.ctx;
		const rect = this.elements.radar.getBoundingClientRect();
		ctx.clearRect(0, 0, rect.width, rect.height);
		if (!this.state.connected) return;

		// Heatmap
		if (this.state.settings.heatmap && this.state.heatmap.size) {
			const now = Date.now();
			for (const h of this.state.heatmap.values()) {
				const s = MapRenderer.worldToRadar(h, this.state, this.state.mapType);
				const age = Math.max(0, 1 - (now - h.t) / this.CONFIG.HEATMAP_MAX_AGE_MS);
				ctx.fillStyle = `rgba(232,192,64,${Math.min(.24, .025 + h.hits * .012) * age})`;
				ctx.beginPath();
				ctx.arc(s.x, s.y, 14 + Math.min(24, h.hits * 1.8), 0, Math.PI * 2);
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

		// Local annotations deliberately remain client-side: they never imply that
		// the backend observed a game-world entity.
		if (this.state.annotations.points.length) {
			ctx.strokeStyle = 'rgba(232,192,64,.9)'; ctx.fillStyle = 'rgba(232,192,64,.96)';
			ctx.lineWidth = 1.5; ctx.setLineDash([4, 4]);
			ctx.beginPath();
			this.state.annotations.points.forEach((point, index) => {
				const s = MapRenderer.worldToRadar(point, this.state, this.state.mapType);
				if (index) ctx.lineTo(s.x, s.y); else ctx.moveTo(s.x, s.y);
			});
			ctx.stroke(); ctx.setLineDash([]);
			for (const point of this.state.annotations.points) {
				const s = MapRenderer.worldToRadar(point, this.state, this.state.mapType);
				ctx.beginPath(); ctx.arc(s.x, s.y, 4, 0, Math.PI * 2); ctx.fill();
				ctx.fillText(point.label, s.x + 7, s.y - 6);
			}
		}

		// Players. Labels use semantic zoom and a tiny collision pass so a busy
		// server remains readable instead of drawing hundreds of overlapping names.
		const occupiedLabels = [];
		for (const p of this.getRenderPlayers()) {
			if (typeof p.x !== 'number') continue;
			const s = MapRenderer.worldToRadar(p, this.state, this.state.mapType);
			if (s.x < -32 || s.y < -32 || s.x > rect.width + 32 || s.y > rect.height + 32) continue;
			const id = String(p.id);
			const isLocal = !!(p.is_local || p.local);
			const selected = String(this.state.selectedPlayerId) === id;
			const watched = this.state.watchlist.includes(id);
			const fastMovement = Number.isFinite(p.speed) && p.speed > 8.5;

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

			// Category-specific blip: player, vehicle or a clearly non-live state.
			const dead = Number.isFinite(p.health) && p.health <= 0;
			ctx.fillStyle = isLocal ? '#4ade80' : watched ? '#e8c040' : fastMovement ? '#ff9f43' : dead ? '#8d9297' : '#f07178';
			if (dead) {
				ctx.lineWidth = 2; ctx.strokeStyle = ctx.fillStyle;
				ctx.beginPath(); ctx.moveTo(s.x - 5, s.y - 5); ctx.lineTo(s.x + 5, s.y + 5); ctx.moveTo(s.x + 5, s.y - 5); ctx.lineTo(s.x - 5, s.y + 5); ctx.stroke();
			} else if (p.in_vehicle) {
				ctx.beginPath(); ctx.moveTo(s.x, s.y - 7); ctx.lineTo(s.x + 6, s.y + 5); ctx.lineTo(s.x - 6, s.y + 5); ctx.closePath(); ctx.fill();
			} else {
				ctx.beginPath(); ctx.arc(s.x, s.y, selected ? 6.5 : 5, 0, Math.PI * 2); ctx.fill();
			}
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

			const detailZoom = this.state.zoom >= 2.2;
			const mediumZoom = this.state.zoom >= 1.15;
			if (this.state.settings.showNames !== false && p.name && (mediumZoom || selected || watched || isLocal)) {
				ctx.font = '11px Segoe UI, sans-serif';
				ctx.fillStyle = 'rgba(0,0,0,0.55)';
				const base = p.name.length > 18 ? p.name.slice(0, 16) + '…' : p.name;
				const label = detailZoom ? `${base} · ${id}` : base;
				const tw = ctx.measureText(label).width;
				const box = { x: s.x - tw / 2 - 3, y: s.y + 10, w: tw + 6, h: 14 };
				const overlaps = occupiedLabels.some(b => box.x < b.x + b.w && box.x + box.w > b.x && box.y < b.y + b.h && box.y + box.h > b.y);
				if (overlaps && !selected && !watched && !isLocal) continue;
				occupiedLabels.push(box);
				ctx.fillRect(box.x, box.y, box.w, box.h);
				ctx.fillStyle = '#f2f2f2';
				ctx.textAlign = 'center';
				ctx.fillText(label, s.x, s.y + 21);
				ctx.textAlign = 'left';
			}
		}
	}
};

document.addEventListener('DOMContentLoaded', () => RadarApp.init());
window.RadarApp = RadarApp;
