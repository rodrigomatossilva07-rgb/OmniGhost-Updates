/* map.js - FiveM Radar Map: resolution-independent calibration + rendering helpers
 * Monitor 1080/1440/4K: uses image natural size + CSS container + devicePixelRatio.
 * World↔map math does NOT depend on monitor resolution — only on map image + world bounds.
 */
const MapRenderer = {
	CALIBRATION: {
		// World bounds for each atlas; imageSize filled automatically from <img>.natural*
		// Approximate atlas bounds for the shipped GTA V / Cayo artwork.
		// rebuildCalibration() recomputes origin/scale from the image natural size
		// so 1080p/1440p/4K monitors only affect CSS layout, not world math.
		los_santos: {
			worldBounds: { minX: -4480, maxX: 4480, minY: -5600, maxY: 8400 },
			imageSize: { width: 1080, height: 1080 },
			origin: { x: 540, y: 540 },
			scaleX: 8.3,
			scaleY: 13.0
		},
		cayo_perico: {
			worldBounds: { minX: 3200, maxX: 6400, minY: -5600, maxY: -2400 },
			imageSize: { width: 1200, height: 1200 },
			origin: { x: 600, y: 600 },
			scaleX: 2.67,
			scaleY: 2.67
		}
	},

	/** Rebuild origin/scale from current image natural dimensions (any resolution asset). */
	rebuildCalibration(mapType, imgEl) {
		const cal = this.CALIBRATION[mapType];
		if (!cal) return cal;
		const w = (imgEl && imgEl.naturalWidth) ? imgEl.naturalWidth : cal.imageSize.width;
		const h = (imgEl && imgEl.naturalHeight) ? imgEl.naturalHeight : cal.imageSize.height;
		cal.imageSize = { width: w, height: h };
		const wb = cal.worldBounds;
		const worldW = Math.max(1, wb.maxX - wb.minX);
		const worldH = Math.max(1, wb.maxY - wb.minY);
		// Map image is axis-aligned: X east, Y north (image Y inverted)
		cal.scaleX = worldW / w;
		cal.scaleY = worldH / h;
		// Pixel of world (minX, maxY) = top-left (0,0)
		cal.origin = {
			x: -wb.minX / cal.scaleX,
			y: wb.maxY / cal.scaleY
		};
		return cal;
	},

	ensureCalibrated(mapType) {
		const img = mapType === 'cayo_perico'
			? document.getElementById('radarCayo')
			: document.getElementById('radarBackground');
		if (img && img.complete && img.naturalWidth > 0)
			return this.rebuildCalibration(mapType, img);
		return this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
	},

	worldToMap(worldPos, mapType = 'los_santos') {
		const cal = this.ensureCalibrated(mapType);
		return {
			x: (worldPos.x - cal.worldBounds.minX) / cal.scaleX,
			y: (cal.worldBounds.maxY - worldPos.y) / cal.scaleY
		};
	},

	mapToWorld(mapPos, mapType = 'los_santos') {
		const cal = this.ensureCalibrated(mapType);
		return {
			x: cal.worldBounds.minX + mapPos.x * cal.scaleX,
			y: cal.worldBounds.maxY - mapPos.y * cal.scaleY
		};
	},

	/** Radar container CSS pixels — independent of monitor native res via layout. */
	getRadarMetrics() {
		const el = document.getElementById('radar');
		const rect = el.getBoundingClientRect();
		const dpr = Math.min(window.devicePixelRatio || 1, 3);
		return {
			width: rect.width,
			height: rect.height,
			dpr,
			centerX: rect.width / 2,
			centerY: rect.height / 2
		};
	},

	/** Fit map image into radar with zoom/pan; returns CSS transform for the map layer. */
	computeMapTransform(radarState, mapType = 'los_santos') {
		const cal = this.ensureCalibrated(mapType);
		const m = this.getRadarMetrics();
		const zoom = radarState.zoom || 1;
		// Base scale: cover container with map, then apply user zoom
		const fit = Math.max(m.width / cal.imageSize.width, m.height / cal.imageSize.height);
		const scale = fit * zoom;
		const tx = m.centerX + (radarState.centerX || 0) - (cal.imageSize.width * scale) / 2;
		const ty = m.centerY + (radarState.centerY || 0) - (cal.imageSize.height * scale) / 2;
		const rot = radarState.followRotation ? (radarState.rotation || 0) : 0;
		return { scale, tx, ty, rot, fit, cal, metrics: m };
	},

	applyMapImageTransform(radarState, mapType = 'los_santos') {
		const { scale, tx, ty, rot } = this.computeMapTransform(radarState, mapType);
		const bg = document.getElementById('radarBackground');
		const cayo = document.getElementById('radarCayo');
		const transform = `translate(${tx}px, ${ty}px) rotate(${-rot}deg) scale(${scale})`;
		const transformOrigin = '0 0';
		if (bg) {
			bg.style.transformOrigin = transformOrigin;
			bg.style.transform = transform;
			bg.style.width = this.CALIBRATION.los_santos.imageSize.width + 'px';
			bg.style.height = this.CALIBRATION.los_santos.imageSize.height + 'px';
		}
		if (cayo) {
			const t2 = this.computeMapTransform(radarState, 'cayo_perico');
			const tr2 = `translate(${t2.tx}px, ${t2.ty}px) rotate(${-(radarState.followRotation ? (radarState.rotation || 0) : 0)}deg) scale(${t2.scale})`;
			cayo.style.transformOrigin = '0 0';
			cayo.style.transform = tr2;
			cayo.style.width = this.CALIBRATION.cayo_perico.imageSize.width + 'px';
			cayo.style.height = this.CALIBRATION.cayo_perico.imageSize.height + 'px';
		}
	},

	worldToRadar(worldPos, radarState, mapType = 'los_santos') {
		const { scale, tx, ty, rot, cal, metrics } = this.computeMapTransform(radarState, mapType);
		const mapPos = this.worldToMap(worldPos, mapType);
		// Position in map image pixels → container after translate/scale/rotate around map top-left
		// With transform-origin 0,0: p' = R * (scale * p) + T — rotation around top-left is awkward.
		// Use center-based layout for markers:
		const cx = cal.imageSize.width / 2;
		const cy = cal.imageSize.height / 2;
		let relX = (mapPos.x - cx) * scale;
		let relY = (mapPos.y - cy) * scale;
		if (rot !== 0) {
			const rad = (-rot) * Math.PI / 180;
			const cos = Math.cos(rad), sin = Math.sin(rad);
			const rx = relX * cos - relY * sin;
			const ry = relX * sin + relY * cos;
			relX = rx; relY = ry;
		}
		return {
			x: metrics.centerX + (radarState.centerX || 0) + relX,
			y: metrics.centerY + (radarState.centerY || 0) + relY
		};
	},

	getMapTypeForPosition(pos) {
		if (!pos) return 'los_santos';
		const c = this.CALIBRATION.cayo_perico.worldBounds;
		if (pos.x >= c.minX && pos.x <= c.maxX && pos.y >= c.minY && pos.y <= c.maxY)
			return 'cayo_perico';
		return 'los_santos';
	},

	isInBounds(worldPos, mapType = 'los_santos') {
		const b = (this.CALIBRATION[mapType] || this.CALIBRATION.los_santos).worldBounds;
		return worldPos.x >= b.minX && worldPos.x <= b.maxX &&
			worldPos.y >= b.minY && worldPos.y <= b.maxY;
	},

	/** Smooth animated pan/zoom toward a world position. */
	animateToWorld(radarState, worldPos, mapType, opts = {}) {
		const targetZoom = opts.zoom || Math.max(radarState.zoom, 3);
		const screen = this.worldToRadar(worldPos, { ...radarState, zoom: targetZoom, centerX: 0, centerY: 0 }, mapType);
		const m = this.getRadarMetrics();
		const goal = {
			zoom: targetZoom,
			centerX: m.centerX - screen.x,
			centerY: m.centerY - screen.y
		};
		const start = { zoom: radarState.zoom, centerX: radarState.centerX, centerY: radarState.centerY };
		const t0 = performance.now();
		const dur = opts.duration || 450;
		const step = (now) => {
			const u = Math.min(1, (now - t0) / dur);
			const e = 1 - Math.pow(1 - u, 3);
			radarState.zoom = start.zoom + (goal.zoom - start.zoom) * e;
			radarState.centerX = start.centerX + (goal.centerX - start.centerX) * e;
			radarState.centerY = start.centerY + (goal.centerY - start.centerY) * e;
			if (window.RadarApp) {
				MapRenderer.applyMapImageTransform(radarState, mapType);
				if (RadarApp.drawMarkers) RadarApp.drawMarkers();
			}
			if (u < 1) requestAnimationFrame(step);
		};
		requestAnimationFrame(step);
	},

	bindImageLoadHandlers() {
		const bg = document.getElementById('radarBackground');
		const cayo = document.getElementById('radarCayo');
		const onLoad = (type, el) => {
			this.rebuildCalibration(type, el);
			if (window.RadarApp && RadarApp.state)
				this.applyMapImageTransform(RadarApp.state, RadarApp.state.mapType || 'los_santos');
		};
		if (bg) {
			if (bg.complete) onLoad('los_santos', bg);
			else bg.addEventListener('load', () => onLoad('los_santos', bg));
		}
		if (cayo) {
			if (cayo.complete) onLoad('cayo_perico', cayo);
			else cayo.addEventListener('load', () => onLoad('cayo_perico', cayo));
		}
		window.addEventListener('resize', () => {
			if (window.RadarApp && RadarApp.state)
				this.applyMapImageTransform(RadarApp.state, RadarApp.state.mapType || 'los_santos');
		});
	}
};

if (typeof window !== 'undefined') window.MapRenderer = MapRenderer;
