const form = document.getElementById("settingsForm");
const numericFields = new Set([
  "sensorGpio", "environmentInterval", "networkInterval", "dashboardRefresh",
  "probeTimeoutMs", "probePacketCount", "rssiLowDbm",
  "tempHighC", "tempLowC", "humidityHighPct", "humidityLowPct",
  "latencyHighMs", "packetLossHighPct", "gen2IntervalS", "gen2SyncIntervalS",
  "iotgwIntervalS", "otaCheckIntervalS",
  "wifiConnectAttempts",
]);

// ---------------------------------------------------------------------------
// Monitor list. The rest of this page is a flat name->input bridge, which
// can't express a variable-length list, so monitors get their own render and
// collect path. Row inputs deliberately carry NO `name` attribute, so the
// generic submit loop below skips them.
// ---------------------------------------------------------------------------

// latencyDefault mirrors monitorTypeDefaultLatencyMs() in MonitorDef.h — 0
// means the type has no opinion and the global Alert Threshold applies.
// Connection-oriented checks pay for setup that ICMP doesn't, so they can't
// meet the same bar however healthy they are.
const MONITOR_TYPES = [
  { value: "ping", label: "Ping", hint: "IP or hostname — blank means the DHCP gateway", latencyDefault: 0 },
  { value: "dns", label: "DNS", hint: "domain to resolve, e.g. google.com", latencyDefault: 0 },
  { value: "http", label: "HTTP", hint: "full URL, e.g. https://example.com", latencyDefault: 2000 },
  { value: "port", label: "Port", hint: "host to open a TCP connection to", latencyDefault: 1000 },
];

// The global Alert Thresholds value, used to show what a blank per-monitor
// threshold will actually inherit.
let globalLatencyHigh = 100;

function monitorRow(m) {
  const gen2 = !!m.gen2;
  const dis = gen2 ? " disabled" : "";
  const opts = MONITOR_TYPES.map(
    (t) => `<option value="${t.value}"${m.type === t.value ? " selected" : ""}>${t.label}</option>`
  ).join("");

  const row = document.createElement("div");
  row.className = "monitor-row";
  row.dataset.id = m.id || "";
  row.dataset.gen2 = gen2 ? "1" : "";
  row.innerHTML = `
    <div class="field-row">
      <div class="field">
        <label>Name${gen2 ? ' <span class="hint">from GEN2 — only the latency bar is editable here</span>' : ""}</label>
        <input class="m-name" value="${Probe.esc(m.name || "")}"${dis}>
      </div>
      <div class="field">
        <label>Type</label>
        <select class="m-type"${dis}>${opts}</select>
      </div>
    </div>
    <div class="field-row">
      <div class="field">
        <label>Target</label>
        <input class="m-target" value="${Probe.esc(m.target || "")}"${dis}>
        <span class="hint m-hint">&nbsp;</span>
      </div>
      <div class="field m-port-field">
        <label>Port</label>
        <input class="m-port" type="number" min="1" max="65535" value="${m.port || ""}"${dis}>
      </div>
    </div>
    <div class="field">
      <label>Slow above (ms)</label>
      <input class="m-latency" type="number" min="10" max="60000" value="${m.latencyHighMs || ""}">
      <span class="hint m-latency-hint">&nbsp;</span>
    </div>
    <p><button type="button" class="m-remove"${dis}>Remove</button></p>`;
  return row;
}

// Port only applies to one type, and the target hint differs per type.
function applyRowType(row) {
  const type = row.querySelector(".m-type").value;
  const meta = MONITOR_TYPES.find((t) => t.value === type) || MONITOR_TYPES[0];
  row.querySelector(".m-hint").textContent = meta.hint;
  row.querySelector(".m-port-field").style.display = type === "port" ? "" : "none";

  // Spell out what blank inherits, so a DEGRADED row is explainable without
  // hunting through two different threshold settings.
  const inherited = meta.latencyDefault || globalLatencyHigh;
  const source = meta.latencyDefault ? `the ${meta.label} default` : "Alert Thresholds";
  row.querySelector(".m-latency-hint").textContent =
    `blank = ${inherited} ms, from ${source}`;
}

function updateMonitorCount() {
  const n = document.querySelectorAll("#monitorRows .monitor-row").length;
  document.getElementById("monitorCount").textContent =
    n === 0 ? "No monitors — the device isn't checking anything." : `${n} monitor${n === 1 ? "" : "s"}.`;
}

function renderMonitors(monitors) {
  const host = document.getElementById("monitorRows");
  host.innerHTML = "";
  (monitors || []).forEach((m) => {
    const row = monitorRow(m);
    host.appendChild(row);
    applyRowType(row);
  });
  updateMonitorCount();
}

// A GEN2-owned row is sent as name + latency bar only. The firmware keeps
// its own copy of everything else and ignores whatever a client claims for
// those fields, so sending them would be noise at best and a spoofing
// attempt at worst — but the latency bar IS ours to set.
function collectMonitors() {
  return Array.from(document.querySelectorAll("#monitorRows .monitor-row"))
    .map((row) => {
      const name = row.querySelector(".m-name").value.trim();
      // 0 is the firmware's "inherit" sentinel, so a blank box means inherit.
      const latencyHighMs = Number(row.querySelector(".m-latency").value || 0);

      if (row.dataset.gen2 === "1") return { gen2: true, name: name, latencyHighMs: latencyHighMs };

      const type = row.querySelector(".m-type").value;
      const m = {
        id: row.dataset.id || "",
        name: name,
        type: type,
        target: row.querySelector(".m-target").value.trim(),
        latencyHighMs: latencyHighMs,
      };
      if (type === "port") m.port = Number(row.querySelector(".m-port").value || 0);
      return m;
    })
    .filter((m) => m.name.length > 0);
}

document.getElementById("monitorRows").addEventListener("click", (e) => {
  const btn = e.target.closest(".m-remove");
  if (!btn || btn.disabled) return;
  btn.closest(".monitor-row").remove();
  updateMonitorCount();
});

document.getElementById("monitorRows").addEventListener("change", (e) => {
  if (e.target.classList.contains("m-type")) applyRowType(e.target.closest(".monitor-row"));
});

document.getElementById("addMonitorBtn").addEventListener("click", () => {
  const row = monitorRow({ name: "", type: "ping", target: "" });
  document.getElementById("monitorRows").appendChild(row);
  applyRowType(row);
  updateMonitorCount();
  row.querySelector(".m-name").focus();
});

// Bounded polling for "did the device actually come back up" after an OTA
// (manual upload or install-latest) — a bare 200 response only means the
// server accepted the write, not that the reboot+reconnect actually
// succeeded. Shared by both OTA paths.
async function pollForReboot(statusEl) {
  statusEl.textContent = "Update sent. Waiting for the device to come back up…";
  const deadline = Date.now() + 90000;
  const startUptime = await Probe.get("/api/status").then((s) => s.device.uptimeSeconds).catch(() => null);
  while (Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, 2000));
    try {
      const s = await Probe.get("/api/status");
      if (startUptime === null || s.device.uptimeSeconds < startUptime) {
        statusEl.textContent = "Device is back online, firmware " + s.device.firmware + ".";
        loadOtaStatus().catch(() => {});
        return;
      }
    } catch (e) { /* expected mid-reboot */ }
  }
  statusEl.textContent = "Update sent but the device hasn't reconnected yet — check it manually.";
}

function applyWifiAuthModeVisibility() {
  const enterprise = form.elements["wifiAuthMode"].value === "enterprise";
  document.getElementById("wifiPersonalFields").style.display = enterprise ? "none" : "";
  document.getElementById("wifiEnterpriseFields").style.display = enterprise ? "" : "none"; // "" restores .field-row's own `display: grid`
}
form.elements["wifiAuthMode"].addEventListener("change", applyWifiAuthModeVisibility);

async function loadConfig() {
  const cfg = await Probe.get("/api/config");
  document.getElementById("fwPlatform").textContent = "";
  Object.keys(cfg).forEach((key) => {
    const el = form.elements[key];
    if (!el) return;
    if (el.type === "checkbox") el.checked = !!cfg[key];
    else if (key !== "wifiPassword" && key !== "authPassword" && key !== "wifiEapPassword" && key !== "iotgwToken") el.value = cfg[key];
  });
  applyWifiAuthModeVisibility();
  // Read before rendering: the rows show what a blank threshold inherits.
  if (typeof cfg.latencyHighMs === "number") globalLatencyHigh = cfg.latencyHighMs;
  renderMonitors(cfg.monitors);
}

async function loadStatus() {
  const status = await Probe.get("/api/status");
  document.getElementById("fwVersion").textContent = status.device.firmware;
  document.getElementById("fwPlatform").textContent = status.device.platform;
}

form.addEventListener("submit", async (e) => {
  e.preventDefault();
  const payload = {};
  Object.entries(form.elements).forEach(([, el]) => {
    if (!el.name) return;
    if (el.type === "checkbox") { payload[el.name] = el.checked; return; }
    if (el.value === "") return; // don't overwrite with blanks (esp. passwords)
    payload[el.name] = numericFields.has(el.name) ? Number(el.value) : el.value;
  });
  // Always sent, even when empty — that is how the last monitor gets deleted.
  payload.monitors = collectMonitors();

  try {
    await Probe.post("/api/config", payload);
    Probe.toast("Settings saved");
    form.elements["wifiPassword"].value = "";
    form.elements["wifiEapPassword"].value = "";
    form.elements["iotgwToken"].value = "";
    form.elements["authPassword"].value = "";
  } catch (err) {
    Probe.toast("Save failed: " + err.message);
  }
});

document.getElementById("restartBtn").addEventListener("click", async () => {
  if (!confirm("Restart the device now?")) return;
  try {
    await Probe.post("/api/restart");
    Probe.toast("Restarting…");
  } catch (err) {
    Probe.toast("Failed: " + err.message);
  }
});

document.getElementById("factoryResetBtn").addEventListener("click", async () => {
  if (!confirm("This erases Wi-Fi credentials, settings, and history, then restarts into setup mode. Continue?")) return;
  try {
    await Probe.post("/api/factory-reset");
    Probe.toast("Factory reset — device is restarting into setup mode");
  } catch (err) {
    Probe.toast("Failed: " + err.message);
  }
});

document.getElementById("otaUploadBtn").addEventListener("click", () => {
  const fileInput = document.getElementById("otaFile");
  const file = fileInput.files[0];
  const statusEl = document.getElementById("otaStatus");
  if (!file) { statusEl.textContent = "Choose a .bin file first."; return; }

  const formData = new FormData();
  formData.append("firmware", file, file.name);

  const xhr = new XMLHttpRequest();
  xhr.open("POST", "/api/ota");
  // Lets the server reject an oversized image immediately (Update.begin()
  // knows the real size up front) instead of only failing after streaming
  // the whole thing — multipart uploads don't otherwise expose Content-
  // Length to the server-side handler.
  xhr.setRequestHeader("X-File-Size", String(file.size));
  xhr.upload.addEventListener("progress", (ev) => {
    if (ev.lengthComputable) {
      statusEl.textContent = "Uploading… " + Math.round((ev.loaded / ev.total) * 100) + "%";
    }
  });
  xhr.onload = () => {
    if (xhr.status === 200) {
      pollForReboot(statusEl);
    } else {
      statusEl.textContent = "Upload failed (HTTP " + xhr.status + ").";
    }
  };
  xhr.onerror = () => { statusEl.textContent = "Upload failed."; };
  xhr.send(formData);
  statusEl.textContent = "Uploading…";
});

// ---------------------------------------------------------------------------
// Automatic update checking (opt-in) — banner + install/check-now buttons
// ---------------------------------------------------------------------------

async function loadOtaStatus() {
  const info = await Probe.get("/api/ota/status");
  const banner = document.getElementById("otaUpdateBanner");
  if (info.available) {
    document.getElementById("otaLatestVersion").textContent = info.latestVersion;
    const link = document.getElementById("otaReleaseNotesLink");
    link.href = info.releaseNotesUrl || "#";
    banner.style.display = "";
  } else {
    banner.style.display = "none";
  }
}

document.getElementById("otaCheckNowBtn").addEventListener("click", async () => {
  const statusEl = document.getElementById("otaStatus");
  statusEl.textContent = "Checking for updates…";
  try {
    await Probe.post("/api/ota/check-now");
    await new Promise((r) => setTimeout(r, 3000)); // the actual check runs on the device's next loop() tick
    await loadOtaStatus();
    statusEl.textContent = "Check complete.";
  } catch (err) {
    statusEl.textContent = "Check failed: " + err.message;
  }
});

document.getElementById("otaInstallBtn").addEventListener("click", async () => {
  if (!confirm("Download and install the update now? The device will restart.")) return;
  const statusEl = document.getElementById("otaStatus");
  statusEl.textContent = "Downloading and installing update…";
  try {
    await Probe.post("/api/ota/install-latest");
    pollForReboot(statusEl);
  } catch (err) {
    statusEl.textContent = "Install failed: " + err.message;
  }
});

loadConfig().catch((err) => Probe.toast("Failed to load settings: " + err.message));
loadStatus().catch(() => {});
loadOtaStatus().catch(() => {});
