/* playerList.js - thin helpers; core list lives in main.js RadarApp */
const PlayerListManager = {
	init() { /* RadarApp owns the list */ },
	update(players) {
		if (window.RadarApp) {
			RadarApp.state.players = players || RadarApp.state.players;
			RadarApp.updatePlayerList();
		}
	}
};
window.PlayerListManager = PlayerListManager;
