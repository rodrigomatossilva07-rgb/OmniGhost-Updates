/* objectList.js - FiveM Object ESP Object List Management */

const ObjectListManager = {
	state: {
		objects: [],
		filteredObjects: [],
		sortBy: 'distance',
		searchQuery: '',
		selectedId: null,
		followingId: null
	},
	
	elements: {},
	
	init() {
		this.cacheElements();
		this.bindEvents();
	},
	
	cacheElements() {
		this.elements = {
			tableBody: document.getElementById('object-tbody'),
			count: document.getElementById('object-count'),
			search: document.getElementById('object-search'),
			sort: document.getElementById('object-sort')
		};
	},
	
	bindEvents() {
		if (this.elements.search) {
			this.elements.search.addEventListener('input', (e) => this.setSearch(e.target.value));
		}
		if (this.elements.sort) {
			this.elements.sort.addEventListener('change', (e) => this.setSort(e.target.value));
		}
	},
	
	update(objects) {
		this.state.objects = objects || [];
		this.applyFilters();
		this.render();
	},
	
	applyFilters() {
		const { objects, searchQuery, sortBy } = this.state;
		
		this.state.filteredObjects = objects.filter(p => {
			if (searchQuery) {
				const name = (p.name || '').toLowerCase();
				const vehicle = (p.vehicle || '').toLowerCase();
				if (!name.includes(searchQuery) && !vehicle.includes(searchQuery)) {
					return false;
				}
			}
			return true;
		});
		
		this.state.filteredObjects.sort((a, b) => {
			switch (sortBy) {
				case 'name':
					return (a.name || '').localeCompare(b.name || '');
				case 'category':
					return (a.category || '').localeCompare(b.category || '');
				case 'distance':
				default:
					return (a.distance || 0) - (b.distance || 0);
			}
		});
	},
	
	setSearch(query) {
		this.state.searchQuery = query.toLowerCase().trim();
		this.applyFilters();
		this.render();
	},
	
	setSort(sortBy) {
		this.state.sortBy = sortBy;
		this.applyFilters();
		this.render();
	},
	
	render() {
		if (!this.elements.tableBody) return;
		
		const { filteredObjects, selectedId, followingId } = this.state;
		
		if (filteredObjects.length === 0) {
			this.elements.tableBody.innerHTML = `
				<tr><td colspan="4" style="text-align:center;padding:20px;color:var(--fg-muted);">
					Nenhum objeto
				</td></tr>
			`;
			this.updateCount(0);
			return;
		}
		
		this.elements.tableBody.innerHTML = filteredObjects.map(p => this.renderRow(p)).join('');
		
		this.elements.tableBody.querySelectorAll('tr').forEach((row, idx) => {
			const obj = filteredObjects[idx];
			row.addEventListener('click', () => this.selectObject(obj.id));
			row.addEventListener('dblclick', () => this.followObject(obj.id));
		});
		
		this.updateCount(filteredObjects.length);
	},
	
	renderRow(obj) {
		const isSelected = obj.id === this.state.selectedId;
		const isFollowing = obj.id === this.state.followingId;
		const hp = obj.health || 100;
		const hpClass = hp > 75 ? 'high' : hp > 35 ? 'med' : 'low';
		const nameClass = obj.is_local ? 'local' : (obj.is_player ? '' : 'npc');
		
		const name = this.truncateName(obj.name || 'Unknown', 20);
		const dist = obj.distance !== undefined ? `${Math.round(obj.distance)}m` : '—';
		const veh = obj.in_vehicle ? (obj.vehicle || 'Vehicle') : 'On foot';
		const dir = obj.yaw !== undefined ? `${Math.round(obj.yaw)}°` : '—';
		
		return `
			<tr class="${isSelected ? 'selected' : ''} ${isFollowing ? 'following' : ''}" data-id="${obj.id}">
				<td class="object-name ${nameClass}">${this.escapeHtml(name)}${obj.is_local ? ` <span class="local-badge" style="font-size:9px;background:var(--accent-gold);color:var(--bg-primary);padding:1px 4px;border-radius:3px;margin-left:4px;">${window.t ? window.t('local_player') : 'You'}</span>` : ''}</td>
				<td class="object-category">${obj.category || 'Unknown'}</td>
				<td class="object-dist">${dist}</td>
				<td class="object-enabled">
					<input type="checkbox" ${obj.enabled ? 'checked' : ''} onchange="window.ObjectESPApp.toggleObjectEnabled('${obj.id}', this.checked)">
				</td>
			</tr>
		`;
	},
	
	truncateName(name, maxLen) {
		if (name.length <= maxLen) return name;
		return name.substring(0, maxLen - 1) + '…';
	},
	
	escapeHtml(text) {
		const div = document.createElement('div');
		div.textContent = text;
		return div.innerHTML;
	},
	
	updateCount(count) {
		if (this.elements.count) {
			this.elements.count.textContent = count;
		}
	},
	
	selectObject(objectId) {
		this.state.selectedId = objectId;
		this.state.followingId = null;
		
		if (this.elements.tableBody) {
			this.elements.tableBody.querySelectorAll('tr').forEach(row => {
				row.classList.toggle('selected', row.dataset.id == objectId);
				row.classList.remove('following');
			});
		}
		
		window.dispatchEvent(new CustomEvent('objectSelected', { detail: { objectId } }));
	},
	
	followObject(objectId) {
		this.state.followingId = objectId;
		this.state.selectedId = objectId;
		
		if (this.elements.tableBody) {
			this.elements.tableBody.querySelectorAll('tr').forEach(row => {
				row.classList.toggle('following', row.dataset.id == objectId);
				row.classList.toggle('selected', row.dataset.id == objectId);
			});
		}
		
		window.dispatchEvent(new CustomEvent('objectFollow', { detail: { objectId } }));
	},
	
	stopFollowing() {
		this.state.followingId = null;
		if (this.elements.tableBody) {
			this.elements.tableBody.querySelectorAll('tr').forEach(row => {
				row.classList.remove('following');
			});
		}
	},
	
	getSelectedObject() {
		return this.state.objects.find(p => p.id === this.state.selectedId);
	},
	
	getFollowingObject() {
		return this.state.objects.find(p => p.id === this.state.followingId);
	}
};

window.ObjectListManager = ObjectListManager;