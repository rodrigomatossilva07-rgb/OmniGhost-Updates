/* settings.js - settings bridged from main.js panel */
const SettingsManager = {
	init() {},
	get(key, def) {
		if (!window.RadarApp) return def;
		const v = RadarApp.state.settings[key];
		return v === undefined ? def : v;
	},
	set(key, value) {
		if (!window.RadarApp) return;
		RadarApp.state.settings[key] = value;
		RadarApp.saveSettings();
	}
};
window.SettingsManager = SettingsManager;
