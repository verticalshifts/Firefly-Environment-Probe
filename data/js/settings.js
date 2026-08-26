const form = document.getElementById("settingsForm");
const numericFields = new Set([
  "sensorGpio", "environmentInterval", "networkInterval", "dashboardRefresh",
  "probeTimeoutMs", "probePacketCount", "rssiLowDbm",
  "tempHighC", "tempLowC", "humidityHighPct", "humidityLowPct",
  "latencyHighMs", "packetLossHighPct",
]);

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
  xhr.upload.addEventListener("progress", (ev) => {
    if (ev.lengthComputable) {
      statusEl.textContent = "Uploading… " + Math.round((ev.loaded / ev.total) * 100) + "%";
    }
  });
  xhr.onload = () => {
    if (xhr.status === 200) {
      statusEl.textContent = "Upload complete. Device is rebooting into the new firmware.";
    } else {
      statusEl.textContent = "Upload failed (HTTP " + xhr.status + ").";
    }
  };
  xhr.onerror = () => { statusEl.textContent = "Upload failed."; };
  xhr.send(formData);
  statusEl.textContent = "Uploading…";
});

loadConfig().catch((err) => Probe.toast("Failed to load settings: " + err.message));
loadStatus().catch(() => {});
