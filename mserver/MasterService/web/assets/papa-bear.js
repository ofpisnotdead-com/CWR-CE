(function () {
    async function fetchJson(url) {
        const response = await fetch(url, {
            headers: {
                Accept: "application/json",
            },
        });

        if (!response.ok) {
            throw new Error("Request failed");
        }

        return response.json();
    }

    function escapeHtml(value) {
        return String(value)
            .replaceAll("&", "&amp;")
            .replaceAll("<", "&lt;")
            .replaceAll(">", "&gt;")
            .replaceAll('"', "&quot;")
            .replaceAll("'", "&#39;");
    }

    // Human-readable download size; "-" when the catalog entry carries no size.
    function formatBytes(bytes) {
        if (bytes === null || bytes === undefined || !Number.isFinite(bytes) || bytes <= 0) {
            return "-";
        }
        const units = ["B", "KB", "MB", "GB", "TB"];
        let value = bytes;
        let unit = 0;
        while (value >= 1024 && unit < units.length - 1) {
            value /= 1024;
            unit += 1;
        }
        const rounded = value >= 100 || unit === 0 ? Math.round(value) : value.toFixed(1);
        return `${rounded} ${units[unit]}`;
    }

    function splitServerMods(value) {
        return String(value)
            .split(/[\s,;|]+/)
            .map((part) => part.trim())
            .filter(Boolean);
    }

    function renderServerModLinks(modValue, modsById) {
        const modIds = splitServerMods(modValue);
        if (modIds.length === 0) {
            return "-";
        }

        return modIds
            .map((modId) => {
                const mod = modsById.get(modId);
                if (!mod) {
                    return escapeHtml(modId);
                }

                return `<a href="/mods/${encodeURIComponent(modId)}">${escapeHtml(mod.name)}</a>`;
            })
            .join(", ");
    }

    function verificationLabel(server) {
        switch (server.verificationState) {
            case "verified":
                return "VERIFIED";
            case "unreachable":
                return "UNREACHABLE";
            default:
                return "SELF REPORTED";
        }
    }

    function verificationClass(server) {
        return `observation-${escapeHtml(server.verificationState || "self_reported")}`;
    }

    function relativeObservedTime(observedUnixMs) {
        if (!observedUnixMs) {
            return "not observed";
        }

        const minutes = Math.max(Math.round((Date.now() - observedUnixMs) / 60000), 0);
        if (minutes < 1) {
            return "just now";
        }
        if (minutes < 60) {
            return `${minutes}m ago`;
        }
        const hours = Math.round(minutes / 60);
        if (hours < 24) {
            return `${hours}h ago`;
        }
        return `${Math.round(hours / 24)}d ago`;
    }

    function formatDuration(totalSeconds) {
        const secs = Number.isFinite(totalSeconds) && totalSeconds > 0 ? Math.floor(totalSeconds) : 0;
        const h = Math.floor(secs / 3600);
        const m = Math.floor((secs % 3600) / 60);
        const mm = String(m).padStart(2, "0");
        const ss = String(secs % 60).padStart(2, "0");
        return h > 0 ? `${h}:${mm}:${ss}` : `${m}:${ss}`;
    }

    // The snapshot is at most one heartbeat old; extrapolate from lastSeen so the
    // cell advances between refreshes instead of freezing on the published value.
    function liveStateElapsed(server) {
        const base = Number(server.stateElapsedSeconds) || 0;
        const lastSeen = Number(server.lastSeenUnixMs) || 0;
        return lastSeen > 0 ? base + Math.max(0, (Date.now() - lastSeen) / 1000) : base;
    }

    // Bit i of `difficulty` == the i-th flag, in engine DifficultyType order.
    const DIFFICULTY_FLAGS = [
        "Armor", "FriendlyTag", "EnemyTag", "HUD", "AutoSpot", "Map",
        "WeaponCursor", "AutoGuideAT", "ClockIndicator", "3rdPersonView", "Tracers", "UltraAI",
    ];
    const RESPAWN_NAMES = ["none", "seagull", "instant", "base", "group", "friendly"];

    function difficultySummary(server) {
        const mask = Number(server.difficulty) || 0;
        const on = DIFFICULTY_FLAGS.filter((_, i) => (mask & (1 << i)) !== 0);
        return `${server.cadet ? "Cadet" : "Veteran"} — aids: ${on.length ? on.join(", ") : "none"}`;
    }

    function sessionConfigSummary(server) {
        const parts = [];
        if (server.jip) parts.push("JIP");
        if (server.locked) parts.push("locked");
        if (server.dedicated) parts.push("dedicated");
        if (server.disabledAI) parts.push("AI off");
        const respawn = Number(server.respawn) || 0;
        if (respawn > 0) {
            const delay = Number(server.respawnDelay) || 0;
            parts.push(`respawn ${RESPAWN_NAMES[respawn] || respawn}${delay > 0 ? ` ${delay}s` : ""}`);
        }
        return parts.join(" · ");
    }

    function renderBrowserRows(servers, modsById) {
        if (servers.length === 0) {
            return '<tr><td colspan="6">No servers match the current filter.</td></tr>';
        }

        return servers
            .map((server, index) => {
                const status = server.state >= 14 ? "PLAYING" : "LOBBY";
                const statusClass = status === "PLAYING" ? "status-playing" : "status-lobby";
                const password = server.password ? "LOCKED" : "OPEN";
                return `
                    <tr class="browser-row" data-server-id="${escapeHtml(server.serverId)}">
                        <td>${index + 1}</td>
                        <td>
                            ${escapeHtml(server.hostname)}
                            <div class="server-subline">${escapeHtml(server.platform)} / ${escapeHtml(formatServerGameVersion(server))}</div>
                        </td>
                        <td>
                            ${escapeHtml(server.gametype)}
                            <div class="server-subline">${server.mapname ? "map: " + escapeHtml(server.mapname) + " · " : ""}${renderServerModLinks(server.mod, modsById)}</div>
                            <div class="server-subline">${escapeHtml(difficultySummary(server))}</div>
                            ${sessionConfigSummary(server) ? `<div class="server-subline">${escapeHtml(sessionConfigSummary(server))}</div>` : ""}
                        </td>
                        <td>
                            <span class="status-chip ${statusClass}">${status}</span>
                            <div class="server-subline">in state ${formatDuration(liveStateElapsed(server))}</div>
                            ${server.timeleft > 0 ? `<div class="server-subline">${escapeHtml(server.timeleft)}m left</div>` : ""}
                            <div><span class="status-chip observation-chip ${verificationClass(server)}">${verificationLabel(server)}</span></div>
                            <div class="server-subline">${password}</div>
                            <div class="server-subline">${escapeHtml(relativeObservedTime(server.lastObservedUnixMs))}</div>
                        </td>
                        <td>${escapeHtml(server.numplayers)}/${escapeHtml(server.maxplayers)}</td>
                        <td>${escapeHtml(server.address)}:${escapeHtml(server.hostport)}</td>
                    </tr>
                `;
            })
            .join("");
    }

    function formatServerGameVersion(server) {
        const app = String(server.app || "").trim() || "unknown";
        const tag = String(server.vertag || "").trim();
        return `${app} ver ${server.actver}${tag ? ` ${tag}` : ""}`;
    }

    function renderDetailMods(mods) {
        if (!mods.length) {
            return '<tr><td colspan="3">No mods.</td></tr>';
        }

        return mods
            .map((mod) => {
                const links = [];
                if (mod.known) {
                    links.push(`<a href="/mods/${encodeURIComponent(mod.modId)}">detail</a>`);
                }
                if (mod.downloadUrl) {
                    links.push(`<a href="${escapeHtml(mod.downloadUrl)}">download</a>`);
                }
                return `
                    <tr>
                        <td>
                            ${mod.known ? `<a href="/mods/${encodeURIComponent(mod.modId)}">${escapeHtml(mod.name)}</a>` : escapeHtml(mod.name)}
                            <div class="server-subline">${escapeHtml(mod.modId)}${mod.description ? ` / ${escapeHtml(mod.description)}` : ""}</div>
                        </td>
                        <td>${mod.version ? escapeHtml(mod.version) : "-"}</td>
                        <td>${links.join(" / ") || "-"}</td>
                    </tr>
                `;
            })
            .join("");
    }

    function renderDetailPlayers(players) {
        if (!players.length) {
            return '<tr><td colspan="2">No players on this server.</td></tr>';
        }

        return players
            .map(
                (player) => `
                    <tr>
                        <td>${escapeHtml(player.name)}</td>
                        <td>${escapeHtml(player.role)}</td>
                    </tr>
                `,
            )
            .join("");
    }

    function renderModLinks(mod) {
        const links = [];
        if (mod.homepageUrl) {
            links.push(`<a href="${escapeHtml(mod.homepageUrl)}">home</a>`);
        }
        if (mod.downloadUrl) {
            links.push(`<a href="${escapeHtml(mod.downloadUrl)}">download</a>`);
        }
        return links.join(" / ") || "-";
    }

    function renderModsRows(mods) {
        if (mods.length === 0) {
            return '<tr><td colspan="5">No mods match the current filter.</td></tr>';
        }

        return mods
            .map(
                (mod) => `
                    <tr class="browser-row" data-mod-id="${escapeHtml(mod.modId)}">
                        <td>
                            ${escapeHtml(mod.name)}
                            <div class="server-subline">${escapeHtml(mod.folderName || mod.modId)}</div>
                        </td>
                        <td>${escapeHtml(mod.version)}</td>
                        <td class="column-size">${escapeHtml(formatBytes(mod.sizeBytes))}</td>
                        <td>${renderModLinks(mod)}</td>
                        <td><button type="button" class="action-button" data-add-mod-id="${escapeHtml(mod.modId)}">ADD</button></td>
                    </tr>
                `,
            )
            .join("");
    }

    function currentDetailId(prefix) {
        const parts = window.location.pathname.split("/").filter(Boolean);
        if (parts[0] !== prefix || parts.length < 2) {
            return null;
        }
        return decodeURIComponent(parts.slice(1).join("/"));
    }

    function currentBrowserVerificationFilter() {
        const verification = new URLSearchParams(window.location.search).get("verification") || "";
        if (verification === "all" || verification === "self_reported" || verification === "verified" || verification === "unreachable") {
            return verification;
        }
        return "";
    }

    function currentBrowserHostnameFilter() {
        return new URLSearchParams(window.location.search).get("hostname") || "";
    }

    function currentModsTextFilter() {
        return new URLSearchParams(window.location.search).get("q") || "";
    }

    function currentBrowserVersionFilter() {
        const params = new URLSearchParams(window.location.search);
        const app = params.get("app") || "";
        const actver = params.get("actver") || "";
        const vertag = params.get("vertag") || "";
        return app && actver ? `${app}/${actver}/${vertag}` : "";
    }

    function currentBrowserGameFilter() {
        return new URLSearchParams(window.location.search).get("app") || "";
    }

    function parseVersionKey(value) {
        const [app, actver, ...tagParts] = String(value || "").split("/");
        return {
            app: app || "",
            actver: actver || "",
            vertag: tagParts.join("/") || "",
        };
    }

    function versionKey(group) {
        return `${group.app || ""}/${group.actver || ""}/${group.vertag || ""}`;
    }

    function versionLabel(group) {
        const app = group.app || "unknown";
        const tag = String(group.vertag || "").trim();
        const version = `${app} ver ${group.actver || "?"}${tag ? ` ${tag}` : ""}`;
        return `${version} (${group.servers})`;
    }

    function gameLabel(app, groups) {
        const entries = groups
            .filter((group) => (group.app || "") === app)
            .reduce((total, group) => total + (Number(group.servers) || 0), 0);
        return `${app || "unknown"} (${entries})`;
    }

    function modCompatibilityGroups(mods) {
        const groups = new Map();
        mods.forEach((mod) => {
            const app = String(mod.app || "").trim();
            const actver = Number(mod.actver) || 0;
            if (!app || actver <= 0) {
                return;
            }
            const vertag = String(mod.vertag || "").trim();
            const key = `${app}/${actver}/${vertag}`;
            const group = groups.get(key) || { app, actver, vertag, servers: 0 };
            group.servers += 1;
            groups.set(key, group);
        });
        return [...groups.values()].sort((a, b) =>
            String(a.app).localeCompare(String(b.app)) ||
            Number(a.actver) - Number(b.actver) ||
            String(a.vertag || "").localeCompare(String(b.vertag || "")),
        );
    }

    function populateGameSelect(element, groups) {
        const apps = [...new Set(groups.map((group) => group.app || ""))].sort();
        element.innerHTML =
            '<option value="">all games</option>' +
            apps.map((app) => `<option value="${escapeHtml(app)}">${escapeHtml(gameLabel(app, groups))}</option>`).join("");
    }

    function populateVersionSelect(element, groups, selectedGame) {
        const visibleGroups = selectedGame ? groups.filter((group) => (group.app || "") === selectedGame) : groups;
        element.innerHTML =
            '<option value="">all game versions</option>' +
            visibleGroups
                .map((group) => `<option value="${escapeHtml(versionKey(group))}">${escapeHtml(versionLabel(group))}</option>`)
                .join("");
    }

    function applyCompatibilityParams(params, gameValue, versionValue) {
        const version = parseVersionKey(versionValue);
        if (version.app && version.actver) {
            params.set("app", version.app);
            params.set("actver", version.actver);
            params.set("vertag", version.vertag);
            return;
        }
        if (gameValue) {
            params.set("app", gameValue);
        }
    }

    function browserUrlFor(filterValue, verificationValue, gameValue, versionValue) {
        const params = new URLSearchParams();
        const needle = filterValue.trim();
        if (needle) {
            params.set("hostname", needle);
        }
        if (verificationValue) {
            params.set("verification", verificationValue);
        }
        applyCompatibilityParams(params, gameValue, versionValue);

        const next = params.toString();
        return next ? `/browser?${next}` : "/browser";
    }

    function updateBrowserUrl(filterValue, verificationValue, gameValue, versionValue) {
        window.history.replaceState({}, "", browserUrlFor(filterValue, verificationValue, gameValue, versionValue));
    }

    function modsUrlFor(filterValue, gameValue, versionValue) {
        const params = new URLSearchParams();
        const needle = filterValue.trim();
        if (needle) {
            params.set("q", needle);
        }
        applyCompatibilityParams(params, gameValue, versionValue);

        const next = params.toString();
        return next ? `/mods?${next}` : "/mods";
    }

    function updateModsUrl(filterValue, gameValue, versionValue) {
        window.history.replaceState({}, "", modsUrlFor(filterValue, gameValue, versionValue));
    }

    function renderPlayerHistory(detail) {
        const history = detail.playerHistory || [];
        if (!history.length) {
            return "No history.";
        }

        const maxPlayers = Math.max(detail.server.maxplayers, 1);
        const bars = history
            .map(
                (point) => `
                    <div class="history-bar" style="height: ${Math.max((point.players / maxPlayers) * 120, 12)}px" title="${escapeHtml(formatObservedTime(point.observedUnixMs))} / ${escapeHtml(point.players)} players"></div>
                `,
            )
            .join("");
        const labels = history.map((point) => `<div>${escapeHtml(formatObservedTime(point.observedUnixMs))}</div>`).join("");
        return `<div class="history-bars">${bars}</div><div class="history-labels">${labels}</div>`;
    }

    function formatObservedTime(observedUnixMs) {
        const date = new Date(observedUnixMs);
        return `${String(date.getHours()).padStart(2, "0")}:${String(date.getMinutes()).padStart(2, "0")}`;
    }

    function relativeEndedTime(endedUnixMs) {
        const minutes = Math.max(Math.round((Date.now() - endedUnixMs) / 60000), 0);
        if (minutes < 60) {
            return `${minutes}m ago`;
        }
        const hours = Math.round(minutes / 60);
        if (hours < 24) {
            return `${hours}h ago`;
        }
        return `${Math.round(hours / 24)}d ago`;
    }

    function renderRecentSessions(detail) {
        const sessions = detail.recentSessions || [];
        if (!sessions.length) {
            return '<tr><td colspan="4">No recent sessions.</td></tr>';
        }

        return sessions
            .map(
                (session) => `
                    <tr>
                        <td>${escapeHtml(session.mission)}<div class="server-subline">${escapeHtml(session.label)}</div></td>
                        <td>${escapeHtml(session.playedMinutes)}m</td>
                        <td>${escapeHtml(session.peakPlayers)}</td>
                        <td>${escapeHtml(relativeEndedTime(session.endedUnixMs))}</td>
                    </tr>
                `,
            )
            .join("");
    }

    function renderVersionRows(modId, versions) {
        if (!versions.length) {
            return '<tr><td colspan="3">No related versions.</td></tr>';
        }

        return versions
            .map(
                (version) => `
                    <tr>
                        <td>${escapeHtml(version.version)}</td>
                        <td>${version.modId === modId ? "current" : "related"}</td>
                        <td>${renderModLinks(version)}</td>
                    </tr>
                `,
            )
            .join("");
    }

    function renderModServers(servers) {
        if (!servers.length) {
            return '<tr><td colspan="4">No current servers.</td></tr>';
        }

        return servers
            .map(
                (server) => `
                    <tr>
                        <td><a href="/browser/${encodeURIComponent(server.serverId)}">${escapeHtml(server.hostname)}</a></td>
                        <td>${escapeHtml(server.gametype)}</td>
                        <td>${escapeHtml(server.players)}/${escapeHtml(server.maxPlayers)}</td>
                        <td>${server.password ? "LOCKED" : "OPEN"}</td>
                    </tr>
                `,
            )
            .join("");
    }

    function fillServerDetail(detail) {
        const title = document.getElementById("detail-title");
        const summary = document.getElementById("detail-summary");
        const facts = document.getElementById("detail-facts");
        const players = document.getElementById("detail-players");
        const mods = document.getElementById("detail-mods");
        const history = document.getElementById("detail-history");
        const sessions = document.getElementById("detail-sessions");
        if (!title || !summary || !facts || !players || !mods || !history || !sessions) {
            return;
        }

        title.textContent = detail.server.hostname;
        summary.innerHTML =
            `${escapeHtml(detail.server.numplayers)}/${escapeHtml(detail.server.maxplayers)} players / ` +
            `${escapeHtml(detail.server.address)}:${escapeHtml(detail.server.hostport)} ` +
            `<span class="status-chip observation-chip ${verificationClass(detail.server)}">${verificationLabel(detail.server)}</span>`;
        const sv = detail.server;
        const phaseTime =
            `in state ${formatDuration(liveStateElapsed(sv))}` +
            (sv.timeleft > 0 ? ` · ${escapeHtml(sv.timeleft)}m left` : "");
        const cfg = sessionConfigSummary(sv);
        const factLines = [
            `${escapeHtml(sv.gametype)}${sv.mapname ? ` · map ${escapeHtml(sv.mapname)}` : ""}`,
            difficultySummary(sv),
            cfg,
            phaseTime,
            sv.description ? escapeHtml(sv.description) : "",
            [sv.param1, sv.param2].filter(Boolean).map((p) => escapeHtml(p)).join(" · "),
            sv.requiredAddons ? `addons: ${escapeHtml(sv.requiredAddons)}` : "",
            `${escapeHtml(sv.platform)} · ${escapeHtml(formatServerVersion(sv))} (req ${escapeHtml(sv.reqver)})${sv.impl ? ` · ${escapeHtml(sv.impl)}` : ""}`,
            `${sv.password ? "password-protected" : "open"} · observed ${escapeHtml(relativeObservedTime(sv.lastObservedUnixMs))}`,
        ];
        facts.innerHTML = factLines.filter(Boolean).map((line) => `<div>${line}</div>`).join("");
        players.innerHTML = renderDetailPlayers(detail.players);
        mods.innerHTML = renderDetailMods(detail.mods);
        history.innerHTML = renderPlayerHistory(detail);
        sessions.innerHTML = renderRecentSessions(detail);
    }

    function fillModDetail(detail) {
        const title = document.getElementById("mod-detail-title");
        const summary = document.getElementById("mod-detail-summary");
        const facts = document.getElementById("mod-detail-facts");
        const description = document.getElementById("mod-detail-description");
        const links = document.getElementById("mod-detail-links");
        const versions = document.getElementById("mod-versions-rows");
        const servers = document.getElementById("mod-servers-rows");
        if (!title || !summary || !facts || !description || !links || !versions || !servers) {
            return;
        }

        title.textContent = detail.mod.name;
        const size = formatBytes(detail.mod.sizeBytes);
        summary.textContent =
            size === "-" ? `version ${detail.mod.version}` : `version ${detail.mod.version} · ${size}`;
        const factParts = [detail.modId];
        if (detail.mod.folderName) {
            factParts.push(detail.mod.folderName);
        }
        if (size !== "-") {
            factParts.push(size);
        }
        facts.textContent = factParts.join(" · ");
        description.innerHTML = escapeHtml(detail.mod.description || "-");
        links.innerHTML = renderModLinks(detail.mod);
        versions.innerHTML = renderVersionRows(detail.modId, detail.versions || []);
        servers.innerHTML = renderModServers(detail.servers);
    }

    async function loadLandingPage() {
        const serviceElement = document.getElementById("landing-service");
        const publicCountElement = document.getElementById("landing-public-count");
        const publicStatusElement = document.getElementById("landing-public-status");
        const playerCountElement = document.getElementById("landing-player-count");
        const modCountElement = document.getElementById("landing-mod-count");
        const selfReportedCountElement = document.getElementById("landing-self-reported-count");
        const unreachableCountElement = document.getElementById("landing-unreachable-count");
        const rowsElement = document.getElementById("landing-rows");
        const gameElement = document.getElementById("landing-game");
        const versionElement = document.getElementById("landing-version");
        const statusElement = document.getElementById("landing-browser-status");
        const browserLinkElement = document.getElementById("landing-browser-link");
        if (!publicCountElement || !publicStatusElement || !playerCountElement || !modCountElement ||
            !selfReportedCountElement || !unreachableCountElement || !rowsElement || !gameElement || !versionElement ||
            !statusElement || !browserLinkElement) {
            return;
        }

        try {
            const [meta, summary, mods, versions] = await Promise.all([
                fetchJson("/v1/meta/service"),
                fetchJson("/v1/meta/summary"),
                fetchJson("/v1/mods"),
                fetchJson("/v1/servers/versions?includeFullServers=true&includePasswordedServers=true&includeUnverifiedServers=true"),
            ]);
            const modsById = new Map(mods.map((mod) => [mod.modId, mod]));

            if (serviceElement) {
                serviceElement.textContent = `${meta.productName} / ${meta.serviceName} / ${meta.apiVersion}`;
            }
            publicCountElement.textContent = String(summary.publicVerifiedServers);
            publicStatusElement.textContent =
                "Public browser shows only observer-verified rows by default";
            playerCountElement.textContent = String(summary.publicPlayers);
            modCountElement.textContent = String(summary.modCount);
            selfReportedCountElement.textContent = String(summary.selfReportedServers);
            unreachableCountElement.textContent = String(summary.unreachableServers);

            populateGameSelect(gameElement, versions);
            gameElement.value = currentBrowserGameFilter();
            populateVersionSelect(versionElement, versions, gameElement.value);
            versionElement.value = currentBrowserVersionFilter();

            const render = async () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app && gameElement.value && selectedVersion.app !== gameElement.value) {
                    versionElement.value = "";
                }

                const params = new URLSearchParams();
                params.set("limit", "5");
                params.set("includeFullServers", "true");
                applyCompatibilityParams(params, gameElement.value, versionElement.value);
                browserLinkElement.href = browserUrlFor("", "", gameElement.value, versionElement.value);
                const servers = await fetchJson(`/v1/servers?${params.toString()}`);
                rowsElement.innerHTML = renderBrowserRows(servers, modsById);
                statusElement.textContent =
                    versionElement.value ? `${servers.length} compatible servers` :
                    gameElement.value ? `${servers.length} ${gameElement.value} servers` :
                    `${servers.length} verified servers`;
                rowsElement.querySelectorAll("[data-server-id]").forEach((row) => {
                    row.addEventListener("click", (event) => {
                        if (event.target.closest("a,button")) {
                            return;
                        }
                        const serverId = row.getAttribute("data-server-id");
                        if (serverId) {
                            window.location.href = `/browser/${encodeURIComponent(serverId)}`;
                        }
                    });
                });
            };

            gameElement.addEventListener("change", () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app && selectedVersion.app !== gameElement.value) {
                    versionElement.value = "";
                }
                populateVersionSelect(versionElement, versions, gameElement.value);
                render().catch(() => {
                    rowsElement.innerHTML = '<tr><td colspan="6">Failed to load public server list.</td></tr>';
                    statusElement.textContent = "Unavailable";
                });
            });
            versionElement.addEventListener("change", () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app) {
                    gameElement.value = selectedVersion.app;
                    populateVersionSelect(versionElement, versions, gameElement.value);
                    versionElement.value = versionKey(selectedVersion);
                }
                render().catch(() => {
                    rowsElement.innerHTML = '<tr><td colspan="6">Failed to load public server list.</td></tr>';
                    statusElement.textContent = "Unavailable";
                });
            });
            await render();
        } catch {
            serviceElement.textContent = "Service summary unavailable";
            publicStatusElement.textContent = "Unavailable";
            rowsElement.innerHTML = '<tr><td colspan="6">Failed to load public server list.</td></tr>';
        }
    }

    async function loadBrowserList() {
        const rowsElement = document.getElementById("browser-rows");
        const filterElement = document.getElementById("browser-filter");
        const verificationElement = document.getElementById("browser-verification");
        const gameElement = document.getElementById("browser-game");
        const versionElement = document.getElementById("browser-version");
        const statusElement = document.getElementById("browser-status");
        if (!rowsElement || !filterElement || !verificationElement || !gameElement || !versionElement || !statusElement) {
            return;
        }

        try {
            const [mods, versions] = await Promise.all([
                fetchJson("/v1/mods"),
                fetchJson("/v1/servers/versions?includeFullServers=true&includePasswordedServers=true&includeUnverifiedServers=true"),
            ]);
            const modsById = new Map(mods.map((mod) => [mod.modId, mod]));
            filterElement.value = currentBrowserHostnameFilter();
            verificationElement.value = currentBrowserVerificationFilter();
            gameElement.value = currentBrowserGameFilter();
            populateGameSelect(gameElement, versions);
            gameElement.value = currentBrowserGameFilter();
            populateVersionSelect(versionElement, versions, gameElement.value);
            versionElement.value = currentBrowserVersionFilter();

            const render = async () => {
                const params = new URLSearchParams();
                params.set("includeFullServers", "true");
                const needle = filterElement.value.trim();
                if (needle) {
                    params.set("hostname", needle);
                }
                const verification = verificationElement.value;
                if (verification === "all") {
                    params.set("includeUnverifiedServers", "true");
                } else if (verification) {
                    params.set("verificationState", verification);
                }
                applyCompatibilityParams(params, gameElement.value, versionElement.value);
                updateBrowserUrl(needle, verification, gameElement.value, versionElement.value);

                const servers = await fetchJson(`/v1/servers?${params.toString()}`);
                rowsElement.innerHTML = renderBrowserRows(servers, modsById);
                statusElement.textContent =
                    versionElement.value ? `${servers.length} compatible servers` :
                    gameElement.value ? `${servers.length} ${gameElement.value} servers` :
                    verification === "" ? `${servers.length} verified servers` : `${servers.length} shown`;
                rowsElement.querySelectorAll("[data-server-id]").forEach((row) => {
                    row.addEventListener("click", (event) => {
                        if (event.target.closest("a,button")) {
                            return;
                        }
                        const serverId = row.getAttribute("data-server-id");
                        if (serverId) {
                            window.location.href = `/browser/${encodeURIComponent(serverId)}`;
                        }
                    });
                });
            };

            const rerender = () => {
                render().catch(() => {
                    rowsElement.innerHTML = '<tr><td colspan="6">Failed to load server list.</td></tr>';
                    statusElement.textContent = "Unavailable";
                });
            };

            filterElement.addEventListener("input", rerender);
            verificationElement.addEventListener("change", rerender);
            gameElement.addEventListener("change", () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app && selectedVersion.app !== gameElement.value) {
                    versionElement.value = "";
                }
                populateVersionSelect(versionElement, versions, gameElement.value);
                rerender();
            });
            versionElement.addEventListener("change", () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app) {
                    gameElement.value = selectedVersion.app;
                    populateVersionSelect(versionElement, versions, gameElement.value);
                    versionElement.value = versionKey(selectedVersion);
                }
                rerender();
            });
            await render();
        } catch {
            rowsElement.innerHTML = '<tr><td colspan="6">Failed to load server list.</td></tr>';
            statusElement.textContent = "Unavailable";
        }
    }

    async function loadServerDetailPage() {
        const serverId = currentDetailId("browser");
        if (!serverId) {
            return;
        }

        try {
            const detail = await fetchJson(`/v1/servers/${encodeURIComponent(serverId)}`);
            fillServerDetail(detail);
        } catch {
            const title = document.getElementById("detail-title");
            if (title) {
                title.textContent = "Server not found";
            }
        }
    }

    async function loadModsList() {
        const rowsElement = document.getElementById("mods-rows");
        const filterElement = document.getElementById("mods-filter");
        const gameElement = document.getElementById("mods-game");
        const versionElement = document.getElementById("mods-version");
        const statusElement = document.getElementById("mods-status");
        if (!rowsElement || !filterElement || !gameElement || !versionElement || !statusElement) {
            return;
        }

        try {
            const catalog = await fetchJson("/v1/mods");
            const versions = modCompatibilityGroups(catalog);
            const selected = new Set();
            filterElement.value = currentModsTextFilter();
            gameElement.value = currentBrowserGameFilter();
            populateGameSelect(gameElement, versions);
            gameElement.value = currentBrowserGameFilter();
            populateVersionSelect(versionElement, versions, gameElement.value);
            versionElement.value = currentBrowserVersionFilter();

            const render = async () => {
                const params = new URLSearchParams();
                const needle = filterElement.value.trim();
                if (needle) {
                    params.set("q", needle);
                }
                applyCompatibilityParams(params, gameElement.value, versionElement.value);
                updateModsUrl(needle, gameElement.value, versionElement.value);

                const query = params.toString();
                const mods = await fetchJson(query ? `/v1/mods?${query}` : "/v1/mods");
                mods.sort((a, b) => a.name.localeCompare(b.name));
                rowsElement.innerHTML = renderModsRows(mods);
                statusElement.textContent = selected.size
                    ? `${mods.length} shown / ${selected.size} selected`
                    : versionElement.value ? `${mods.length} compatible mods`
                    : gameElement.value ? `${mods.length} ${gameElement.value} mods`
                    : `${mods.length} shown`;
                rowsElement.querySelectorAll("[data-mod-id]").forEach((row) => {
                    row.addEventListener("click", (event) => {
                        if (event.target.closest("a,button")) {
                            return;
                        }
                        const modId = row.getAttribute("data-mod-id");
                        if (modId) {
                            window.location.href = `/mods/${encodeURIComponent(modId)}`;
                        }
                    });
                });
                rowsElement.querySelectorAll("[data-add-mod-id]").forEach((button) => {
                    const modId = button.getAttribute("data-add-mod-id");
                    if (modId && selected.has(modId)) {
                        button.textContent = "ADDED";
                    }
                    button.addEventListener("click", () => {
                        const modId = button.getAttribute("data-add-mod-id");
                        if (!modId) {
                            return;
                        }
                        if (selected.has(modId)) {
                            selected.delete(modId);
                            button.textContent = "ADD";
                        } else {
                            selected.add(modId);
                            button.textContent = "ADDED";
                        }
                        statusElement.textContent = selected.size
                            ? `${mods.length} shown / ${selected.size} selected`
                            : versionElement.value ? `${mods.length} compatible mods`
                            : gameElement.value ? `${mods.length} ${gameElement.value} mods`
                            : `${mods.length} shown`;
                    });
                });
            };

            const rerender = () => {
                render().catch(() => {
                    rowsElement.innerHTML = '<tr><td colspan="5">Failed to load mod catalog.</td></tr>';
                    statusElement.textContent = "Unavailable";
                });
            };

            filterElement.addEventListener("input", rerender);
            gameElement.addEventListener("change", () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app && selectedVersion.app !== gameElement.value) {
                    versionElement.value = "";
                }
                populateVersionSelect(versionElement, versions, gameElement.value);
                rerender();
            });
            versionElement.addEventListener("change", () => {
                const selectedVersion = parseVersionKey(versionElement.value);
                if (selectedVersion.app) {
                    gameElement.value = selectedVersion.app;
                    populateVersionSelect(versionElement, versions, gameElement.value);
                    versionElement.value = versionKey(selectedVersion);
                }
                rerender();
            });
            await render();
        } catch {
            rowsElement.innerHTML = '<tr><td colspan="5">Failed to load mod catalog.</td></tr>';
            statusElement.textContent = "Unavailable";
        }
    }

    async function loadModDetailPage() {
        const modId = currentDetailId("mods");
        if (!modId) {
            return;
        }

        try {
            const [mod, versions, servers] = await Promise.all([
                fetchJson(`/v1/mods/${encodeURIComponent(modId)}`),
                fetchJson(`/v1/mods/${encodeURIComponent(modId)}/versions`),
                fetchJson(`/v1/mods/${encodeURIComponent(modId)}/servers`),
            ]);
            fillModDetail({
                modId,
                mod,
                versions,
                servers,
            });
        } catch {
            const title = document.getElementById("mod-detail-title");
            const summary = document.getElementById("mod-detail-summary");
            const versions = document.getElementById("mod-versions-rows");
            const servers = document.getElementById("mod-servers-rows");
            if (title) {
                title.textContent = "Mod not found";
            }
            if (summary) {
                summary.textContent = "Unavailable";
            }
            if (versions) {
                versions.innerHTML = '<tr><td colspan="3">Unavailable</td></tr>';
            }
            if (servers) {
                servers.innerHTML = '<tr><td colspan="4">Unavailable</td></tr>';
            }
        }
    }
    // Single source of truth for the top navigation. Each page ships an empty
    // <nav class="top-menu"> placeholder; the active link is derived from data-page.
    function renderCommunityBanner() {
        if (document.querySelector(".community-banner")) {
            return;
        }
        const banner = document.createElement("div");
        banner.className = "community-banner";
        banner.innerHTML = 'Share your ideas, issues, and mod requests at <a href="https://github.com/ofpisnotdead-com/CWR-CE/discussions/new?category=papa-bear-cz">GitHub Discussions</a>.';
        document.body.insertBefore(banner, document.body.firstChild);
    }

    function renderTopMenu() {
        const nav = document.querySelector("nav.top-menu");
        if (!nav) {
            return;
        }
        const links = [
            { href: "/", label: "HOME", pages: ["landing"] },
            { href: "/browser", label: "SERVERS", pages: ["browser", "browser-detail"] },
            { href: "/mods", label: "MODS", pages: ["mods", "mod-detail"] },
        ];
        const current = document.body.dataset.page;
        nav.innerHTML = links
            .map((link) => {
                const active = link.pages.includes(current) ? ' class="active"' : "";
                return `<a href="${link.href}"${active}>${link.label}</a>`;
            })
            .join("");
    }

    function renderSiteFooter() {
        if (document.querySelector(".site-footer")) {
            return;
        }
        const footer = document.createElement("footer");
        footer.className = "site-footer";
        footer.innerHTML = 'Community project of <a href="https://ofpisnotdead.com">ofpisnotdead.com</a> group &lt;3.';
        document.body.appendChild(footer);
    }

    renderCommunityBanner();
    renderTopMenu();
    renderSiteFooter();

    if (document.body.dataset.page === "landing") {
        loadLandingPage();
    }

    if (document.body.dataset.page === "browser") {
        loadBrowserList();
    }

    if (document.body.dataset.page === "browser-detail") {
        loadServerDetailPage();
    }

    if (document.body.dataset.page === "mods") {
        loadModsList();
    }

    if (document.body.dataset.page === "mod-detail") {
        loadModDetailPage();
    }
})();
