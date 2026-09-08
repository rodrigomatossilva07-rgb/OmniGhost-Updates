/* renderer.js - FiveM Object ESP Map Rendering */

const MapRenderer = {
	// Map calibration constants for GTA V world coordinates
	CALIBRATION: {
		los_santos: {
			worldBounds: { minX: -4000, maxX: 4000, minY: -4000, maxY: 8000 },
			imageSize: { width: 4096, height: 4096 },
			origin: { x: 2048, y: 2048 },
			scale: 1.953
		},
		cayo_perico: {
			worldBounds: { minX: 4500, maxX: 7500, minY: -3500, maxY: -500 },
			imageSize: { width: 2048, height: 2048 },
			origin: { x: 1024, y: 1024 },
			scale: 1.465
		}
	},
	
	worldToMap(worldPos, mapType = 'los_santos') {
		const cal = this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
		return {
			x: cal.origin.x + worldPos.x / cal.scale,
			y: cal.origin.y - worldPos.y / cal.scale
		};
	},
	
	worldToRadar(worldPos, radarState, mapType = 'los_santos') {
		const cal = this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
		const mapPos = this.worldToMap(worldPos, mapType);
		
		const { zoom, centerX, centerY, rotation } = radarState;
		const radarRect = document.getElementById('radar').getBoundingClientRect();
		const radarCenterX = radarRect.width / 2;
		const radarCenterY = radarRect.height / 2;
		
		const relX = mapPos.x - cal.imageSize.width / 2;
		const relY = mapPos.y - cal.imageSize.height / 2;
		
		let rotX = relX, rotY = relY;
		if (rotation !== 0) {
			const rad = rotation * Math.PI / 180;
			const cos = Math.cos(rad);
			const sin = Math.sin(rad);
			rotX = relX * cos - relY * sin;
			rotY = relX * sin + relY * cos;
		}
		
		return {
			x: radarCenterX + centerX + rotX * zoom,
			y: radarCenterY + centerY + rotY * zoom
		};
	},
	
	getMapTypeForPosition(pos) {
		if (pos.x > 4500 && pos.x < 7500 && pos.y < -500 && pos.y > -3500) {
			return 'cayo_perico';
		}
		return 'los_santos';
	},
	
	drawGraticule(ctx, radarState, mapType = 'los_santos') {
		const cal = this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
		const radarRect = document.getElementById('radar').getBoundingClientRect();
		const centerX = radarRect.width / 2 + radarState.centerX;
		const centerY = radarRect.height / 2 + radarState.centerY;
		const zoom = radarState.zoom;
		
		ctx.save();
		ctx.strokeStyle = 'rgba(212, 175, 55, 0.1)';
		ctx.lineWidth = 1 / zoom;
		ctx.beginPath();
		
		const gridSize = 500 * zoom;
		const maxDim = Math.max(radarRect.width, radarRect.height) / zoom + 1000;
		
		for (let x = -maxDim; x <= maxDim; x += gridSize) {
			ctx.moveTo(centerX + x, centerY - maxDim);
			ctx.lineTo(centerX + x, centerY + maxDim);
		}
		for (let y = -maxDim; y <= maxDim; y += gridSize) {
			ctx.moveTo(centerX - maxDim, centerY + y);
			ctx.lineTo(centerX + maxDim, centerY + y);
		}
		
		ctx.stroke();
		ctx.restore();
	},
	
	drawCompass(ctx, radarState) {
		const radarRect = document.getElementById('radar').getBoundingClientRect();
		const size = 50;
		const x = radarRect.width - size - 20;
		const y = size + 20;
		
		ctx.save();
		ctx.translate(x, y);
		ctx.rotate(-radarState.rotation * Math.PI / 180);
		
		ctx.strokeStyle = 'rgba(212, 175, 55, 0.5)';
		ctx.lineWidth = 1;
		ctx.beginPath();
		ctx.arc(0, 0, size / 2, 0, Math.PI * 2);
		ctx.stroke();
		
		ctx.fillStyle = '#d4af37';
		ctx.font = 'bold 10px Segoe UI';
		ctx.textAlign = 'center';
		ctx.textBaseline = 'middle';
		
		const directions = ['N', 'E', 'S', 'W'];
		directions.forEach((dir, i) => {
			const angle = (i * Math.PI / 2) - Math.PI / 2;
			const rx = Math.cos(angle) * (size / 2 - 8);
			const ry = Math.sin(angle) * (size / 2 - 8);
			ctx.fillText(dir, rx, ry);
		});
		
		ctx.fillStyle = '#ef5350';
		ctx.beginPath();
		ctx.moveTo(0, -size / 2 + 4);
		ctx.lineTo(-6, -size / 2 + 16);
		ctx.lineTo(6, -size / 2 + 16);
		ctx.closePath();
		ctx.fill();
		
		ctx.restore();
	},
	
	drawScaleBar(ctx, radarState, mapType = 'los_santos') {
		const cal = this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
		const radarRect = document.getElementById('radar').getBoundingClientRect();
		const zoom = radarState.zoom;
		
		const scalePixels = 1000 / cal.scale * zoom;
		const barWidth = Math.min(scalePixels, 200);
		const worldDist = barWidth / zoom * cal.scale;
		
		const x = 20;
		const y = radarRect.height - 40;
		const h = 6;
		
		ctx.save();
		ctx.fillStyle = 'rgba(212, 175, 55, 0.9)';
		ctx.fillRect(x, y, barWidth, h);
		
		const segments = 4;
		const segWidth = barWidth / segments;
		ctx.fillStyle = 'rgba(5, 5, 6, 0.9)';
		for (let i = 1; i < segments; i += 2) {
			ctx.fillRect(x + i * segWidth, y, segWidth, h);
		}
		
		ctx.fillStyle = '#aaa';
		ctx.font = '9px JetBrains Mono, monospace';
		ctx.textAlign = 'center';
		ctx.fillText('0', x, y - 4);
		ctx.fillText(`${Math.round(worldDist / 1000)} km`, x + barWidth, y - 4);
		
		ctx.restore();
	},
	
	drawCoordinates(ctx, radarState, mapType = 'los_santos') {
		if (this.mouseWorldPos) {
			ctx.save();
			ctx.fillStyle = 'rgba(13, 13, 16, 0.9)';
			ctx.strokeStyle = 'rgba(212, 175, 55, 0.3)';
			ctx.lineWidth = 1;
			
			const text = `X: ${this.mouseWorldPos.x.toFixed(0)}  Y: ${this.mouseWorldPos.y.toFixed(0)}`;
			ctx.font = '11px JetBrains Mono, monospace';
			const metrics = ctx.measureText(text);
			const w = metrics.width + 20;
			const h = 24;
			const x = radarRect.width - 180;
			const y = 20;
			
			ctx.fillRect(x, y, w, h);
			ctx.strokeRect(x, y, w, h);
			ctx.fillStyle = '#d4af37';
			ctx.fillText(text, x + 10, y + 16);
			ctx.restore();
		}
	},
	
	isInBounds(worldPos, mapType = 'los_santos') {
		const cal = this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
		return worldPos.x >= cal.worldBounds.minX && 
			   worldPos.x <= cal.worldBounds.maxX &&
			   worldPos.y >= cal.worldBounds.minY && 
			   worldPos.y <= cal.worldBounds.maxY;
	},
	
	getViewBounds(radarState, mapType = 'los_santos') {
		const cal = this.CALIBRATION[mapType] || this.CALIBRATION.los_santos;
		const radarRect = document.getElementById('radar').getBoundingClientRect();
		
		const halfW = radarRect.width / 2 / radarState.zoom;
		const halfH = radarRect.height / 2 / radarState.zoom;
		
		const centerMapX = cal.imageSize.width / 2 - radarState.centerX / radarState.zoom;
		const centerMapY = cal.imageSize.height / 2 - radarState.centerY / radarState.zoom;
		
		return {
			minX: (centerMapX - halfW - cal.origin.x) * cal.scale,
			maxX: (centerMapX + halfW - cal.origin.x) * cal.scale,
			minY: (cal.origin.y - centerMapY - halfH) * cal.scale,
			maxY: (cal.origin.y - centerMapY + halfH) * cal.scale
		};
	}
};

window.MapRenderer = MapRenderer;