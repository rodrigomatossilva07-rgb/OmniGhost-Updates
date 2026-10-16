
(function() {
    if (typeof window.radarLazyInit === 'undefined') {
        window.radarLazyInit = [];
    }
    window.radarLazyInit.push(function() {








function initSettings() {
	const toggle = document.getElementById("settings-toggle")
	const panel = document.getElementById("settings-panel")
	const resetBtn = document.getElementById("settings-reset")
	const langSelect = document.getElementById("cfg-language")

	const controls = panel.querySelectorAll("[data-config]")


	function refreshControls() {
		controls.forEach(function(ctrl) {
			const key = ctrl.getAttribute("data-config")
			const value = getConfigValue(key)
			if (value === undefined) return

			if (ctrl.type === "checkbox") {
				ctrl.checked = value
			}
			else {
				ctrl.value = value
				if (ctrl.type === "range") {
					const valueDisplay = panel.querySelector(`[data-value-for="${key}"]`)
					if (valueDisplay) {
						valueDisplay.textContent = value
					}
				}
			}
		})
	}


	if (langSelect) {
		langSelect.value = currentLang
		langSelect.addEventListener("change", function() {
			setLang(langSelect.value)
		})
	}


	toggle.addEventListener("click", function() {
		if (panel.hasAttribute("hidden")) {
			panel.removeAttribute("hidden")
			refreshControls()
		}
		else {
			panel.setAttribute("hidden", "")
		}
	})


	resetBtn.addEventListener("click", function() {
		if (confirm(t("settings_confirm_reset"))) {
			resetConfig()
			refreshControls()
		}
	})


	controls.forEach(function(ctrl) {
		const key = ctrl.getAttribute("data-config")
		const eventName = ctrl.type === "checkbox" ? "change" : "input"
		ctrl.addEventListener(eventName, function() {
			let value
			if (ctrl.type === "checkbox") {
				value = ctrl.checked
			}
			else if (ctrl.type === "range") {
				value = parseFloat(ctrl.value)
				const valueDisplay = panel.querySelector(`[data-value-for="${key}"]`)
				if (valueDisplay) {
					valueDisplay.textContent = ctrl.value
				}
			}
			else {
				value = ctrl.value
			}
			applySetting(key, value)
		})
	})


	document.addEventListener("configchange", function(e) {
		if (e.detail.key === null) {
			refreshControls()
		}
	})


	refreshControls()
}


if (document.readyState === "loading") {
	document.addEventListener("DOMContentLoaded", initSettings)
}
else {
	initSettings()
}

    });
})();
