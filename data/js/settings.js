const form = document.getElementById("settingsForm");
const numericFields = new Set([
  "sensorGpio", "environmentInterval", "networkInterval", "dashboardRefresh",
  "probeTimeoutMs", "probePacketCount", "rssiLowDbm",
  "tempHighC", "tempLowC", "humidityHighPct", "humidityLowPct",
  "latencyHighMs", "packetLossHighPct", "gen2IntervalS", "otaCheckIntervalS",
  "wifiConnectAttempts",
]);

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

async function loadConfig() {
  const cfg = await Probe.get("/api/config");
  document.getElementById("fwPlatform").textContent = "";
  Object.keys(cfg).forEach((key) => {
    const el = form.elements[key];
    if (!el) return;
    if (el.type === "checkbox") el.checked = !!cfg[key];
    else if (key !== "wifiPassword" && key !== "authPassword") el.value = cfg[key];
  });
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

  try {
    await Probe.post("/api/config", payload);
    Probe.toast("Settings saved");
    form.elements["wifiPassword"].value = "";
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
