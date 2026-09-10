/* Transport negotiation for the FiveM Radar.
 * Preference: WebSocket -> SSE -> authenticated HTTP.  Every fallback has a
 * bounded retry path so a missing optional endpoint never stalls the radar.
 */
class RadarTransport {
	constructor({ token, onSnapshot, onStatus, onMetrics }) {
		this.token = token || '';
		this.onSnapshot = onSnapshot;
		this.onStatus = onStatus;
		this.onMetrics = onMetrics || (() => {});
		this.ws = null; this.sse = null; this.pollTimer = null; this.pingTimer = null;
		this.stopped = false; this.mode = 'offline'; this.etag = '';
		this.attempt = 0; this.lastSeq = null; this.lastReceive = 0;
		this.stats = { reconnects: 0, received: 0, dropped: 0, bytes: 0, rtt: 0 };
	}

	start() { this.stopped = false; this.tryWebSocket(); }
	stop() {
		this.stopped = true;
		clearTimeout(this.pollTimer); this.pollTimer = null;
		clearTimeout(this.pingTimer); this.pingTimer = null;
		this.ws?.close(); this.ws = null;
		this.sse?.close(); this.sse = null;
	}
	endpoint(path, webSocket = false) {
		const url = new URL(path, location.href);
		if (webSocket) url.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
		return url.toString();
	}
	status(state, reason = '') { this.onStatus({ state, mode: this.mode, reason, age: Date.now() - this.lastReceive, ...this.stats }); }
	handle(raw) {
		let snapshot;
		try { snapshot = typeof raw === 'string' ? JSON.parse(raw) : raw; } catch { return; }
		if (!snapshot || (!Array.isArray(snapshot.players) && !Array.isArray(snapshot.upserts))) return;
		const seq = Number(snapshot.seq);
		if (Number.isFinite(seq) && this.lastSeq !== null && seq > this.lastSeq + 1)
			this.stats.dropped += seq - this.lastSeq - 1;
		if (Number.isFinite(seq)) this.lastSeq = seq;
		this.lastReceive = Date.now(); this.stats.received++;
		this.stats.bytes += typeof raw === 'string' ? raw.length : JSON.stringify(raw).length;
		this.onSnapshot(snapshot);
		this.status('connected'); this.onMetrics(this.stats);
	}
	backoff(next) {
		if (this.stopped) return;
		this.stats.reconnects++;
		const delay = Math.min(10000, 300 * Math.pow(2, Math.min(this.attempt++, 5))) + Math.random() * 180;
		this.status('reconnecting');
		this.pollTimer = setTimeout(() => !this.stopped && next.call(this), delay);
	}
	probePing() {
		if (this.stopped || this.mode === 'http') return;
		const started = performance.now();
		const headers = this.token ? { Authorization: `Bearer ${this.token}` } : {};
		fetch('/api/ping', { cache: 'no-store', headers })
			.then(response => { if (!response.ok) throw new Error('ping'); this.stats.rtt = Math.round(performance.now() - started); this.onMetrics(this.stats); })
			.catch(() => {})
			.finally(() => { this.pingTimer = setTimeout(() => this.probePing(), 5000); });
	}
	tryWebSocket() {
		if (this.stopped || !('WebSocket' in window)) return this.trySse();
		this.mode = 'ws'; this.status('reconnecting');
		let opened = false;
		try {
			const protocols = this.token ? [`omnighost-radar.${this.token}`] : undefined;
			this.ws = new WebSocket(this.endpoint('/fivem_webradar', true), protocols);
			const openingTimeout = setTimeout(() => { if (!opened) this.ws?.close(); }, 1800);
			this.ws.onopen = () => { opened = true; clearTimeout(openingTimeout); this.attempt = 0; this.status('connected'); this.probePing(); };
			this.ws.onmessage = event => this.handle(event.data);
			this.ws.onerror = () => this.ws?.close();
			this.ws.onclose = () => { clearTimeout(openingTimeout); clearTimeout(this.pingTimer); this.pingTimer = null; if (!this.stopped) this.trySse(); };
		} catch { this.trySse(); }
	}
	trySse() {
		if (this.stopped || !('EventSource' in window)) return this.startPolling();
		this.mode = 'sse'; this.status('reconnecting');
		try {
			const url = new URL('/api/stream', location.href);
			if (this.token) url.searchParams.set('t', this.token);
			this.sse = new EventSource(url.toString());
			let opened = false;
			const openingTimeout = setTimeout(() => { if (!opened) { this.sse?.close(); this.startPolling(); } }, 1800);
			this.sse.onopen = () => { opened = true; clearTimeout(openingTimeout); this.attempt = 0; this.status('connected'); this.probePing(); };
			this.sse.onmessage = event => this.handle(event.data);
			this.sse.onerror = () => { clearTimeout(openingTimeout); clearTimeout(this.pingTimer); this.pingTimer = null; this.sse?.close(); if (!this.stopped) this.startPolling(); };
		} catch { this.startPolling(); }
	}
	startPolling() {
		this.mode = 'http'; this.status('reconnecting');
		const tick = async () => {
			if (this.stopped || this.mode !== 'http') return;
			const started = performance.now();
			try {
				const headers = { Accept: 'application/json' };
				if (this.token) headers.Authorization = `Bearer ${this.token}`;
				if (this.etag) headers['If-None-Match'] = this.etag;
				const response = await fetch('/api/state', { cache: 'no-store', headers });
				if (response.status !== 304) {
					if (!response.ok) throw new Error(`HTTP ${response.status}`);
					this.etag = response.headers.get('ETag') || this.etag;
					this.handle(await response.text());
				} else { this.lastReceive = Date.now(); this.status('connected'); }
				this.stats.rtt = Math.round(performance.now() - started); this.attempt = 0;
				this.pollTimer = setTimeout(tick, 120);
			} catch {
				this.backoff(this.startPolling);
			}
		};
		tick();
	}
}

window.RadarTransport = RadarTransport;
