



let bombElement = document.getElementById("bomb")
let bombStyle = bombElement.style

let lastKnownBomb = null

socket.element.addEventListener("bomb", event => {
	let bomb = event.data


	if (bomb && bomb.state && bomb.position) {
		lastKnownBomb = {
			position: { x: bomb.position.x, y: bomb.position.y, z: bomb.position.z },
			state: bomb.state,
			timestamp: Date.now()
		}
	} else {

		if (lastKnownBomb) {
			let ttl = 0
			if (lastKnownBomb.state === "dropped") ttl = 180
			else if (lastKnownBomb.state === "planted") ttl = 650

			if (Date.now() - lastKnownBomb.timestamp < ttl) {

				bomb = {
					state: lastKnownBomb.state,
					position: lastKnownBomb.position,
					player: "",
					countdown: 0
				}
			} else {

				lastKnownBomb = null
			}
		}
	}

	global.bomb = bomb

	if (!bomb || !bomb.state) {
		bombStyle.display = "none"
		return
	}

	if (bomb.state == "carried" || bomb.state == "exploded") {
		bombStyle.display = "none"
	}
	else {
		bombStyle.display = "block"
		bombStyle.left = global.positionToPerc(bomb.position, "x") + "%"
		bombStyle.bottom = global.positionToPerc(bomb.position, "y") + "%"
	}

	if (bomb.state == "planted" || bomb.state == "defusing") {
		bombElement.className = "planted"
	}
	else if (bomb.state == "defused") {
		bombElement.className = "defused"
	}
	else if (bomb.state == "planting") {
		bombElement.className = "planting"
	}
	else {
		bombElement.className = ""
	}
})
