



global = {

	config: {},

	effects: {},
	mapData: {},
	currentMap: "none",

	gamePhase: "freezetime",

	playerPos: [],
	playerBuffers: [],
	playerSplits: [],
	playerDots: [],
	playerLabels: [],
	playerAmmos: [],
	playerHealths: [],
	playerBlasts: [],

	projectilePos: {},
	projectileBuffer: {},
	bomb: {status: "carried", countdown: Infinity, position: {x: 0, y: 0, z: 0}},









	positionToPerc: (positionObj, axis, playerNum) => {

		if (typeof positionObj != "object") positionObj = {[axis]: positionObj}

		let gamePosition = positionObj[axis] + global.mapData.offset[axis]

		let pixelPosition = gamePosition / global.mapData.resolution

		let precPosition = pixelPosition / 1024 * 100


		let currentSplit = -1

		if (global.mapData.splits.length > 0 && typeof positionObj.z == "number") {

			for (let i in global.mapData.splits) {
				let split = global.mapData.splits[i]


				if (positionObj.z > split.bounds.bottom && positionObj.z < split.bounds.top) {

					precPosition += split.offset[axis]
					currentSplit = parseInt(i)


					break
				}
			}
		}


		if (typeof playerNum == "number") {


			if (global.playerSplits[playerNum] != currentSplit) {
				global.playerBuffers[playerNum] = []
			}


			global.playerSplits[playerNum] = currentSplit
			global.playerPos[playerNum].split = currentSplit
		}


		const cal = global.calibration
		if (cal && typeof cal.rotationDeg === "number") {

			if (typeof positionObj.x === "number" && typeof positionObj.y === "number") {

				let x = (positionObj.x + global.mapData.offset.x) / global.mapData.resolution / 1024 * 100
				let y = (positionObj.y + global.mapData.offset.y) / global.mapData.resolution / 1024 * 100
				if (currentSplit >= 0) {
					x += global.mapData.splits[currentSplit].offset.x
					y += global.mapData.splits[currentSplit].offset.y
				}

				const rad = cal.rotationDeg * Math.PI / 180
				const cx = x - 50
				const cy = y - 50
				x = 50 + (cx * Math.cos(rad) - cy * Math.sin(rad))
				y = 50 + (cx * Math.sin(rad) + cy * Math.cos(rad))

				const scale = cal.scale || 1.0
				x = 50 + (x - 50) / scale
				y = 50 + (y - 50) / scale

				x += (cal.offsetX || 0)
				y += (cal.offsetY || 0)
				return (axis === "y") ? y : x
			}

			const scale = cal.scale || 1.0
			precPosition = 50 + (precPosition - 50) / scale
			precPosition += (axis === "y") ? (cal.offsetY || 0) : (cal.offsetX || 0)
		}


		return precPosition
	}
}


for (var i = 0; i < 10; i++) {
	global.playerPos.push({
		x: null,
		y: null,
		alive: false
	})

	global.playerSplits.push(-1)
	global.playerBuffers.push([])
	global.playerDots.push(document.getElementById("dot" + i))
	global.playerLabels.push(document.getElementById("label" + i))
	global.playerAmmos.push({})
	global.playerHealths.push(0)
}


global.followedPlayer = null

global.localPlayerNum = null
