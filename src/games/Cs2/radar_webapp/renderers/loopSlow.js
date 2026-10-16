




let radarStyle = document.getElementById("container").style

let radarQueues = {
	scale: [1, 1, 1, 1, 1, 1],
	x: [0, 0, 0, 0, 0, 0],
	y: [0, 0, 0, 0, 0, 0]
}


setInterval(() => {

	if (!global.config.autozoom.enable && !global.followedPlayer) return




	let bounds = {
		x: {
			min: 100,
			max: 0
		},
		y: {
			min: 100,
			max: 0
		}
	}


	for (let player of global.playerPos) {

		if (!player.alive) continue


		if (bounds.x.min > player.x) bounds.x.min = player.x
		if (bounds.x.max < player.x) bounds.x.max = player.x
		if (bounds.y.min > player.y) bounds.y.min = player.y
		if (bounds.y.max < player.y) bounds.y.max = player.y
	}

	let radarScale, radarX, radarY


	if (global.followedPlayer != null && global.playerPos[global.followedPlayer] && global.playerPos[global.followedPlayer].x != null) {
		let followed = global.playerPos[global.followedPlayer]
		radarScale = 2.5
		radarX = ((followed.x - 50)) * -1
		radarY = (followed.y - 50)
	}
	else {

		radarScale = 1 + (1 - Math.max(bounds.x.max - bounds.x.min, bounds.y.max - bounds.y.min) / 100)


		if (radarScale === 3) return


		radarScale = Math.max(1, radarScale - global.config.autozoom.padding)


		radarX = (((bounds.x.max + bounds.x.min) / 2) - 50) * -1
		radarY = ((bounds.y.max + bounds.y.min) / 2) - 50


		if (radarScale < global.config.autozoom.minZoom) {
			radarScale = 1
			radarX = radarY = 0
		}
	}


	radarQueues.scale.unshift(radarScale)
	radarQueues.scale = radarQueues.scale.slice(0, global.config.autozoom.smoothing)
	radarQueues.x.unshift(radarX)
	radarQueues.x = radarQueues.x.slice(0, global.config.autozoom.smoothing)
	radarQueues.y.unshift(radarY)
	radarQueues.y = radarQueues.y.slice(0, global.config.autozoom.smoothing)


	let avgScale = radarQueues.scale.reduce((sum, el) => sum + el, 0) / radarQueues.scale.length
	let avgX = radarQueues.x.reduce((sum, el) => sum + el, 0) / radarQueues.x.length
	let avgY = radarQueues.y.reduce((sum, el) => sum + el, 0) / radarQueues.y.length


	let rotation = 0
	if (global.config.radar.followRotation && global.localPlayerNum != null && global.playerPos[global.localPlayerNum] && global.playerPos[global.localPlayerNum].alive) {
		rotation = -(global.playerPos[global.localPlayerNum].a || 0)
	}


	radarStyle.transform = `scale(${avgScale}) translate(${avgX}%, ${avgY}%) rotate(${rotation}deg)`
}, 25)
