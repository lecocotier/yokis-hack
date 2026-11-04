#pragma once

#include <Arduino.h>

// Served directly from flash: the page has no template substitutions or credentials.
static const char diagnostic_html[] PROGMEM = R"YOKISDIAG(<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="referrer" content="no-referrer">
<title>Yokis · Diagnostic</title>
<style>
:root{color-scheme:light dark;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;line-height:1.5;background:#f3f6fa;color:#17263a}
*{box-sizing:border-box}body{margin:0}main{max-width:1200px;margin:auto;padding:24px}header{display:flex;justify-content:space-between;align-items:center;gap:16px;flex-wrap:wrap;margin-bottom:24px}h1{font-size:1.8rem;margin:0}h2{font-size:1.1rem;margin:0 0 12px}p{margin:8px 0}.muted{color:#53657c}nav{display:flex;gap:16px}a{color:#1555ad;text-underline-offset:3px}.cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:16px;margin:20px 0}.card,.devices{background:#fff;border:1px solid #dbe3ed;border-radius:12px;padding:20px}dl{margin:0}dt{font-size:.85rem;color:#53657c;margin-top:10px}dd{margin:2px 0;overflow-wrap:anywhere}button{font:inherit;background:#1555ad;color:#fff;border:0;border-radius:7px;padding:9px 14px;cursor:pointer}button:disabled{opacity:.45;cursor:default}button:focus-visible,a:focus-visible{outline:3px solid #d48300;outline-offset:3px}.actions{display:flex;align-items:center;gap:12px;flex-wrap:wrap}.notice{min-height:1.5em;margin:12px 0}.notice.error{color:#a51d29}.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.9rem}th,td{text-align:left;padding:12px 10px;border-bottom:1px solid #e1e7ef;vertical-align:top}th{color:#53657c;font-weight:600;white-space:nowrap}td{overflow-wrap:anywhere;max-width:220px}.pager{margin-top:16px;display:flex;justify-content:flex-end;align-items:center;gap:12px;flex-wrap:wrap}.fingerprint{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:.82rem}.footnote{font-size:.85rem;margin-top:16px}
@media(prefers-color-scheme:dark){:root{background:#101925;color:#e2ebf6}.card,.devices{background:#182434;border-color:#304158}.muted,dt,th{color:#afbed1}a{color:#8bbaff}th,td{border-color:#304158}.notice.error{color:#ffafb8}}
@media(max-width:600px){main{padding:16px}.card,.devices{padding:16px}h1{font-size:1.5rem}}
</style>
</head>
<body>
<main>
<header><div><h1>Diagnostic Yokis</h1><p class="muted">État de la passerelle et de ses appareils</p></div><nav aria-label="Navigation"><a href="/" aria-current="page">Diagnostic</a><a href="/config">Configuration</a></nav></header>
<div class="actions"><button id="refresh" type="button">Actualiser</button><span class="muted">Actualisation toutes les 5 secondes</span></div>
<p id="notice" class="notice" role="status" aria-live="polite">Chargement des données…</p>
<section class="cards" aria-label="Passerelle">
<article class="card"><h2>Wi-Fi</h2><dl><dt>Connexion</dt><dd id="wifi-connected">—</dd><dt>Réseau</dt><dd id="wifi-ssid">—</dd><dt>Adresse IP</dt><dd id="wifi-ip">—</dd><dt>Signal RSSI</dt><dd id="wifi-rssi">—</dd></dl></article>
<article class="card"><h2>MQTT</h2><dl><dt>Connexion</dt><dd id="mqtt-connected">—</dd><dt>Configuration</dt><dd id="mqtt-configured">—</dd><dt>Serveur</dt><dd id="mqtt-host">—</dd><dt>Port</dt><dd id="mqtt-port">—</dd><dt>Nettoyage HA</dt><dd id="cleanup">—</dd></dl></article>
<article class="card"><h2>Système</h2><dl><dt>Compteur de fonctionnement</dt><dd id="uptime">—</dd><dt>Mémoire libre</dt><dd id="heap">—</dd><dt>Niveau de debug</dt><dd id="debug">—</dd><dt>Build</dt><dd id="build" class="fingerprint">—</dd><dt>Révision Git</dt><dd id="git" class="fingerprint">—</dd><dt>Empreinte des sources</dt><dd id="source" class="fingerprint">—</dd></dl></article>
</section>
<section class="devices" aria-labelledby="devices-heading" id="devices-section" aria-busy="false">
<h2 id="devices-heading">Appareils</h2>
<div class="scroll"><table><thead><tr><th scope="col">Nom</th><th scope="col">Mode</th><th scope="col">État</th><th scope="col">Disponibilité</th><th scope="col">Âge de l’état</th><th scope="col">Origine</th><th scope="col">Dernière commande</th><th scope="col">Vérification STOP</th></tr></thead><tbody id="device-rows"></tbody></table></div>
<p id="empty" class="muted" hidden>Aucun appareil enregistré.</p>
<div class="pager"><span id="page-label">Page 1</span><button id="previous" type="button" disabled>Précédent</button><button id="next" type="button" disabled>Suivant</button></div>
<p class="muted footnote">Ces données sont les derniers états connus, éventuellement estimés. Actualiser cette page ne déclenche aucune lecture radio et ne confirme pas la position intermédiaire d’un volet. Le compteur système revient à zéro au redémarrage et après son débordement.</p>
</section>
</main>
<script>
"use strict";
(() => {
    const byId = id => document.getElementById(id);
    const value = input => input === null || input === undefined ? "—" : String(input);
    const set = (id, input) => { byId(id).textContent = value(input); };
    const elapsed = input => {
        const ms = Number(input);
        if (!Number.isFinite(ms) || ms < 0) return "—";
        if (ms < 1000) return Math.floor(ms) + " ms";
        const seconds = Math.floor(ms / 1000);
        if (seconds < 60) return seconds + " s";
        const minutes = Math.floor(seconds / 60);
        if (minutes < 60) return minutes + " min " + seconds % 60 + " s";
        const hours = Math.floor(minutes / 60);
        if (hours < 24) return hours + " h " + minutes % 60 + " min";
        return Math.floor(hours / 24) + " j " + hours % 24 + " h";
    };
    let page = 0;
    let hasNext = false;
    let busy = false;
    let received = false;

    function buttons() {
        byId("refresh").disabled = busy;
        byId("previous").disabled = busy || page === 0;
        byId("next").disabled = busy || !hasNext;
        byId("devices-section").setAttribute("aria-busy", String(busy));
    }

    function render(data) {
        if (!data || !data.wifi || !data.mqtt || !Array.isArray(data.devices) ||
            data.devices.length > 8 || !Number.isInteger(data.page) || data.page < 0 ||
            !Number.isInteger(data.total) || data.total < 0 || data.total > 64 || data.page_size !== 8) {
            throw new Error("invalid snapshot");
        }
        const rows = data.devices.map(device => {
            if (!device || typeof device !== "object") throw new Error("invalid device");
            const row = document.createElement("tr");
            const state = value(device.status) + (device.estimated === true ? " (estimé)" : "");
            const cells = [device.name, device.mode, state, device.availability,
                elapsed(device.status_age_ms), device.state_source, device.last_command, device.stop_check];
            cells.forEach(item => {
                const cell = document.createElement("td");
                cell.textContent = value(item);
                row.appendChild(cell);
            });
            return row;
        });
        set("wifi-connected", data.wifi.connected ? "Connecté" : "Déconnecté");
        set("wifi-ssid", data.wifi.ssid);
        set("wifi-ip", data.wifi.ip);
        set("wifi-rssi", value(data.wifi.rssi) + " dBm");
        set("mqtt-connected", data.mqtt.connected ? "Connecté" : "Déconnecté");
        set("mqtt-configured", data.mqtt.configured ? "Configuré" : "Non configuré");
        set("mqtt-host", data.mqtt.host);
        set("mqtt-port", data.mqtt.port);
        set("cleanup", data.mqtt.cleanup_blocked ? "Bloqué (réessai planifié)" :
            data.mqtt.cleanup_pending ? "En attente" : "À jour");
        set("uptime", elapsed(data.uptime_ms));
        set("heap", value(data.free_heap) + " octets");
        set("debug", data.debug_level);
        set("build", data.build);
        set("git", data.git);
        set("source", data.source_sha256);
        byId("device-rows").replaceChildren(...rows);
        byId("empty").hidden = data.total !== 0;
        page = data.page;
        hasNext = data.has_next === true;
        set("page-label", "Page " + (page + 1) + " / " + Math.max(1, Math.ceil(data.total / 8)) + " · " + data.total + " appareil(s)");
    }

    async function load(targetPage = page) {
        if (busy) return;
        busy = true;
        buttons();
        const controller = new AbortController();
        const timeout = setTimeout(() => controller.abort(), 4000);
        let authenticationRequired = false;
        try {
            const response = await fetch("/api/status?page=" + targetPage, {
                credentials: "same-origin", cache: "no-store", signal: controller.signal
            });
            authenticationRequired = response.status === 401;
            if (!response.ok) throw new Error("request failed");
            render(await response.json());
            received = true;
            byId("notice").className = "notice";
            set("notice", "Données actualisées à " + new Date().toLocaleTimeString("fr-FR") + ".");
        } catch (_) {
            byId("notice").className = "notice error";
            const message = authenticationRequired
                ? "Authentification requise. Rechargez la page pour vous reconnecter."
                : "Diagnostic indisponible. Vérifiez la connexion puis réessayez.";
            set("notice", message + (received ? " Les dernières données reçues sont conservées." : ""));
        } finally {
            clearTimeout(timeout);
            busy = false;
            buttons();
        }
    }

    byId("refresh").addEventListener("click", () => load());
    byId("previous").addEventListener("click", () => { if (page > 0) load(page - 1); });
    byId("next").addEventListener("click", () => { if (hasNext) load(page + 1); });
    setInterval(() => { if (!document.hidden) load(); }, 5000);
    document.addEventListener("visibilitychange", () => { if (!document.hidden) load(); });
    load();
})();
</script>
</body>
</html>)YOKISDIAG";
