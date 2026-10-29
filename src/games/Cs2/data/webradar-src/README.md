# OmniGhost CS2 Web Radar frontend

This directory contains the modified corresponding source for the CS2 Web
Radar frontend. It is based on `clauadv/cs2_webradar` revision
`ab2e14579b38f2daec8e02cff2e7d408110b8de7` and remains licensed under
GPL-3.0. See `LICENSE`.

The original process reader and unauthenticated Node relay are not used.
OmniGhost's native C++ server publishes its existing read-only DMA runtime
snapshot over an authenticated same-origin WebSocket.

## Requirements

- Windows 10/11
- Node.js 20.19 or newer
- npm

## Build

```powershell
cd src/games/Cs2/data/webradar-src
npm ci
npm run build
```

Vite writes the deployable files to the adjacent `../webradar` directory.
Those files are embedded into `OmniGhost.exe` by
`src/project/tools/Build-EmbeddedResources.ps1`.

## Development

`npm run dev` serves the React interface only. Live game data requires the
native OmniGhost radar server because authentication tokens and snapshots are
created there. The production interface connects to `/ws` on the same origin,
using the token from the URL fragment as a WebSocket subprotocol.

## Supported map overviews

Mirage, Inferno, Dust II, Nuke, Ancient, Anubis, Vertigo, Overpass and Train.
The overview origin/scale metadata and radar images come from the upstream
GPL-3.0 project. Nuke and Vertigo currently use the upstream combined overview;
the client preserves Z coordinates but does not claim automatic floor switching
without separately validated floor assets and thresholds.

## Runtime protocol

The server emits version 1 JSON snapshots containing the current map, local
team, players and bomb state. Missing fields remain optional. The frontend
rejects different message types/versions and clears positions when data becomes
stale or the WebSocket disconnects.

