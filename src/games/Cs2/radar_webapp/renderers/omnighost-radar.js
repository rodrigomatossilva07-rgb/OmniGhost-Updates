(() => {
  "use strict";

  const token = decodeURIComponent((location.hash || "").replace(/^#/, "").trim());
  const elements = {
    state: document.querySelector(".session-state"),
    stateLabel: document.getElementById("connection-label"),
    mapLabel: document.getElementById("map-label"),
    playerCount: document.getElementById("player-count"),
    latency: document.getElementById("latency-label"),
    map: document.getElementById("map-image"),
    entities: document.getElementById("entities"),
    empty: document.getElementById("empty-state"),
    mapError: document.getElementById("map-error"),
    teamT: document.getElementById("team-t-list"),
    teamCt: document.getElementById("team-ct-list"),
    teamTCount: document.getElementById("team-t-count"),
    teamCtCount: document.getElementById("team-ct-count"),
    bombStatus: document.getElementById("bomb-status")
  };
  const state = { mapName: "", mapData: null, players: new Map(), stopped: false, failures: 0 };

  function setConnection(kind, label) {
    elements.state.classList.remove("online", "error");
    if (kind) elements.state.classList.add(kind);
    elements.stateLabel.textContent = label;
  }

  function safeMapName(value) {
    const name = String(value || "unknown").split(/[\\/]/).pop().replace(/\.[^.]+$/, "");
    return /^[-a-zA-Z0-9_]+$/.test(name) ? name : "unknown";
  }

  function parseJson5(text) {
    return JSON.parse(text.replace(/^\s*\/\/.*$/gm, ""));
  }

  async function loadMap(name) {
    const mapName = safeMapName(name);
    if (mapName === state.mapName) return;
    state.mapName = mapName;
    state.mapData = null;
    elements.mapLabel.textContent = mapName === "unknown"
      ? "Mapa por identificar"
      : mapName.replace(/^de_|^cs_/, "").toUpperCase();
    elements.mapError.hidden = true;
    if (mapName === "unknown") {
      elements.map.src = "/img/empty.webp";
      return;
    }
    try {
      const response = await fetch(`/maps/${encodeURIComponent(mapName)}/meta.json5`, { cache: "force-cache" });
      if (!response.ok) throw new Error(`metadata ${response.status}`);
      const metadata = parseJson5(await response.text());
      if (!Number.isFinite(Number(metadata.resolution)) || !metadata.offset) {
        throw new Error("metadata inválida");
      }
      state.mapData = metadata;
      elements.map.src = `/maps/${encodeURIComponent(mapName)}/radar.png`;
      elements.map.onerror = () => { elements.mapError.hidden = false; };
    } catch (error) {
      elements.map.src = "/img/empty.webp";
      elements.mapError.hidden = false;
    }
  }

  function mapPosition(position) {
    if (!state.mapData || !position) return null;
    const resolution = Number(state.mapData.resolution);
    const offset = state.mapData.offset;
    let x = ((Number(position.x) + Number(offset.x)) / resolution / 1024) * 100;
    let y = 100 - ((Number(position.y) + Number(offset.y)) / resolution / 1024) * 100;
    const splits = Array.isArray(state.mapData.splits) ? state.mapData.splits : [];
    for (const split of splits) {
      if (!split || !split.bounds || !split.offset) continue;
      const z = Number(position.z);
      if (z > Number(split.bounds.bottom) && z < Number(split.bounds.top)) {
        x += Number(split.offset.x || 0);
        y -= Number(split.offset.y || 0);
        break;
      }
    }
    if (![x, y].every(Number.isFinite)) return null;
    return { x: Math.max(-4, Math.min(104, x)), y: Math.max(-4, Math.min(104, y)) };
  }

  function playerKey(player, fallback) {
    const id = Number(player.m_idx);
    return Number.isFinite(id) ? String(id) : `p${fallback}`;
  }

  function createPlayerNode(key) {
    const node = document.createElement("div");
    node.className = "radar-player";
    node.dataset.key = key;
    node.innerHTML = '<span class="direction"></span><span class="body"></span><span class="tag"></span>';
    elements.entities.appendChild(node);
    state.players.set(key, node);
    return node;
  }

  function renderPlayer(player, key) {
    const point = mapPosition(player.m_position);
    let node = state.players.get(key);
    if (!node) node = createPlayerNode(key);
    if (!point) {
      node.hidden = true;
      return;
    }
    node.hidden = false;
    node.style.left = `${point.x}%`;
    node.style.top = `${point.y}%`;
    const local = Boolean(player.m_is_local);
    const team = Number(player.m_team) === 3 ? "ct" : "t";
    const alive = Number(player.m_health) > 0;
    node.className = `radar-player ${team}${local ? " local" : ""}${alive ? "" : " dead"}`;
    node.querySelector(".direction").style.transform =
      `translateX(-50%) rotate(${Number(player.m_eye_angle || 0) + 90}deg)`;
    const name = String(player.m_name || (local ? "Local" : `Jogador ${key}`));
    node.querySelector(".tag").textContent =
      `${name} · ${Math.max(0, Number(player.m_health) || 0)} HP`;
  }

  function renderTeam(list, target, countTarget) {
    target.replaceChildren();
    const sorted = list.slice().sort((a, b) => Number(b.m_is_local) - Number(a.m_is_local));
    for (const player of sorted) {
      const row = document.createElement("div");
      const health = Math.max(0, Number(player.m_health) || 0);
      row.className = `player-row${player.m_is_local ? " local" : ""}${health ? "" : " dead"}`;
      const index = document.createElement("span");
      index.className = "player-index";
      index.textContent = String(Number(player.m_idx) >= 0 ? player.m_idx : "•");
      const name = document.createElement("span");
      name.className = "player-name";
      name.textContent = String(player.m_name || (player.m_is_local ? "Local" : "Jogador"));
      const hp = document.createElement("span");
      hp.className = "player-health";
      hp.textContent = `${health} HP`;
      row.append(index, name, hp);
      target.appendChild(row);
    }
    countTarget.textContent = String(list.length);
  }

  function renderBomb(bomb) {
    const existing = document.getElementById("radar-bomb");
    if (!bomb || !bomb.m_is_planted) {
      if (existing) existing.remove();
      elements.bombStatus.hidden = true;
      return;
    }
    const point = mapPosition(bomb.m_position);
    if (!point) return;
    const node = existing || document.createElement("div");
    node.id = "radar-bomb";
    node.className = "radar-bomb";
    node.textContent = "C4";
    node.style.left = `${point.x}%`;
    node.style.top = `${point.y}%`;
    if (!existing) elements.entities.appendChild(node);
    elements.bombStatus.hidden = false;
  }

  async function render(data) {
    await loadMap(data.m_map);
    const players = Array.isArray(data.m_players) ? data.m_players : [];
    const seen = new Set();
    players.forEach((player, index) => {
      const key = playerKey(player, index);
      seen.add(key);
      renderPlayer(player, key);
    });
    for (const [key, node] of state.players) {
      if (!seen.has(key)) {
        node.remove();
        state.players.delete(key);
      }
    }
    renderTeam(players.filter(player => Number(player.m_team) === 2),
      elements.teamT, elements.teamTCount);
    renderTeam(players.filter(player => Number(player.m_team) === 3),
      elements.teamCt, elements.teamCtCount);
    renderBomb(data.m_bomb);
    elements.playerCount.textContent =
      `${players.length} ${players.length === 1 ? "jogador" : "jogadores"}`;
    elements.empty.hidden = players.length > 0 && Boolean(state.mapData);
  }

  async function poll() {
    if (state.stopped) return;
    const started = performance.now();
    if (!token) {
      setConnection("error", "Link sem token de acesso");
      elements.empty.hidden = false;
      elements.empty.querySelector("strong").textContent = "Link do radar inválido";
      elements.empty.querySelector("span").textContent =
        "Volta ao menu CS2 e utiliza o botão Abrir link.";
      return;
    }
    try {
      const response = await fetch("/api/live", {
        cache: "no-store",
        headers: { Authorization: `Bearer ${token}` }
      });
      if (!response.ok) throw new Error(`HTTP ${response.status}`);
      const data = await response.json();
      state.failures = 0;
      setConnection("online", "Radar ligado em tempo real");
      elements.latency.textContent =
        `${Math.max(0, Math.round(performance.now() - started))} ms`;
      await render(data);
    } catch (error) {
      state.failures += 1;
      setConnection("error",
        state.failures > 3 ? "Ligação ao launcher perdida" : "A recuperar ligação…");
      elements.latency.textContent = "— ms";
    } finally {
      window.setTimeout(poll, state.failures
        ? Math.min(1500, 180 * state.failures)
        : 75);
    }
  }

  window.addEventListener("beforeunload", () => { state.stopped = true; });
  poll();
})();
