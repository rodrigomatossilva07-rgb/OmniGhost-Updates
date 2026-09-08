/* playerList.js - FiveM Radar Player List Management */

const PlayerListManager = {
	// State
	state: {
		players: [],
		filteredPlayers: [],
		sortBy: 'distance',
		searchQuery: '',
		selectedId: null,
		followingId: null
	},
	
	// DOM cache
	elements: {},
	
	init() {
		this.cacheElements();
		this.bindEvents();
	},
	
	cacheElements() {
		this.elements = {
			tableBody: document.getElementById('player-tbody'),
			count: document.getElementById('player-count'),
			search: document.getElementById('player-search'),
			sort: document.getElementById('player-sort')
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
	
	update(players) {
		this.state.players = players || [];
		this.applyFilters();
		this.render();
	},
	
	applyFilters() {
		const { players, searchQuery, sortBy } = this.state;
		
		// Filter
		this.state.filteredPlayers = players.filter(p => {
			// Search filter
			if (searchQuery) {
				const name = (p.name || '').toLowerCase();
				const vehicle = (p.vehicle || '').toLowerCase();
				if (!name.includes(searchQuery) && !vehicle.includes(searchQuery)) {
					return false;
				}
			}
			return true;
		});
		
		// Sort
		this.state.filteredPlayers.sort((a, b) => {
			switch (sortBy) {
				case 'name':
					return (a.name || '').localeCompare(b.name || '');
				case 'health':
					return (b.health || 0) - (a.health || 0);
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
		
		const { filteredPlayers, selectedId, followingId } = this.state;
		
		if (filteredPlayers.length === 0) {
			this.elements.tableBody.innerHTML = `
				<tr><td colspan="6" style="text-align:center;padding:20px;color:var(--fg-muted);">
					${window.t ? window.t('no_players') : 'Nenhum jogador'}
				</td></tr>
			`;
			this.updateCount(0);
			return;
		}
		
		this.elements.tableBody.innerHTML = filteredPlayers.map(p => this.renderRow(p)).join('');
		
		// Bind click events
		this.elements.tableBody.querySelectorAll('tr').forEach((row, idx) => {
			const player = filteredPlayers[idx];
			row.addEventListener('click', () => this.selectPlayer(player.id));
			row.addEventListener('dblclick', () => this.followPlayer(player.id));
		});
		
		this.updateCount(filteredPlayers.length);
	},
	
	renderRow(player) {
		const isSelected = player.id === this.state.selectedId;
		const isFollowing = player.id === this.state.followingId;
		const hp = player.health || 100;
		const hpClass = hp > 75 ? 'high' : hp > 35 ? 'med' : 'low';
		const nameClass = player.is_local ? 'local' : (player.is_player ? '' : 'npc');
		
		const name = this.truncateName(player.name || 'Unknown', 20);
		const dist = player.distance !== undefined ? `${Math.round(player.distance)}m` : '—';
		const veh = player.in_vehicle ? (player.vehicle || 'Vehicle') : 'On foot';
		const dir = player.yaw !== undefined ? `${Math.round(player.yaw)}°` : '—';
		
		return `
			<tr class="${isSelected ? 'selected' : ''} ${isFollowing ? 'following' : ''}" data-id="${player.id}">
				<td class="player-name ${nameClass}">${this.escapeHtml(name)}${player.is_local ? ` <span class="local-badge" style="font-size:9px;background:var(--accent-gold);color:var(--bg-primary);padding:1px 4px;border-radius:3px;margin-left:4px;">${window.t ? window.t('local_player') : 'You'}</span>` : ''}</td>
				<td class="player-dist">${dist}</td>
				<td class="player-hp ${hpClass}">${Math.round(hp)}</td>
				<td class="player-armor">${player.armor ? Math.round(player.armor) : '—'}</td>
				<td class="player-vehicle">${this.escapeHtml(veh)}</td>
				<td class="player-dir">${dir}</td>
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
	
	selectPlayer(playerId) {
		this.state.selectedId = playerId;
		this.state.followingId = null; // Stop following on single select
		
		// Update row highlights
		if (this.elements.tableBody) {
			this.elements.tableBody.querySelectorAll('tr').forEach(row => {
				row.classList.toggle('selected', row.dataset.id == playerId);
				row.classList.remove('following');
			});
		}
		
		// Emit event for map to center
		window.dispatchEvent(new CustomEvent('playerSelected', { detail: { playerId } }));
	},
	
	followPlayer(playerId) {
		this.state.followingId = playerId;
		this.state.selectedId = playerId;
		
		if (this.elements.tableBody) {
			this.elements.tableBody.querySelectorAll('tr').forEach(row => {
				row.classList.toggle('following', row.dataset.id == playerId);
				row.classList.toggle('selected', row.dataset.id == playerId);
			});
		}
		
		window.dispatchEvent(new CustomEvent('playerFollow', { detail: { playerId } }));
	},
	
	stopFollowing() {
		this.state.followingId = null;
		if (this.elements.tableBody) {
			this.elements.tableBody.querySelectorAll('tr').forEach(row => {
				row.classList.remove('following');
			});
		}
	},
	
	getSelectedPlayer() {
		return this.state.players.find(p => p.id === this.state.selectedId);
	},
	
	getFollowingPlayer() {
		return this.state.players.find(p => p.id === this.state.followingId);
	}
};

window.PlayerListManager = PlayerListManager;