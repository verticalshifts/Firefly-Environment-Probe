function fillTable(tableId, rows) {
  const tbody = document.querySelector("#" + tableId + " tbody");
  tbody.innerHTML = rows.map(([k, v]) => `<tr><th>${k}</th><td>${v}</td></tr>`).join("");
}

let lastStatus = null, lastNetwork = null, lastEnv = null;

async function loadDiagnostics() {
  const [status, net, env] = await Promise.all([
    Probe.get("/api/status"),
    Probe.get("/api/network"),
    Probe.get("/api/environment"),
  ]);
  lastStatus = status; lastNetwork = net; lastEnv = env;

  const d = status.device;
  fillTable("deviceTable", [
    ["Device Name", d.name],
    ["Device ID", d.id],
    ["Platform", d.platform],
    ["Chip", d.chipModel],
    ["Firmware", d.firmware],
    ["Uptime", Probe.uptime(d.uptimeS)],
    ["Boot Count", d.bootCount],
    ["Reset Reason", d.resetReason],
    ["Free Heap", d.freeHeap + " bytes"],
    ["Flash Size", (d.flashSize / 1024 / 1024).toFixed(1) + " MB"],
    ["MAC Address", d.macAddress],
  ]);

  const w = net.wifi;
  fillTable("wifiTable", [
    ["SSID", w.connected ? w.ssid : "—"],
    ["IP Address", w.ip],
    ["Gateway", w.gateway || "—"],
    ["RSSI", w.connected ? w.rssi + " dBm" : "—"],
    ["Channel", w.connected ? w.channel : "—"],
    ["Connection Status", w.connected ? "Connected" : (w.provisioning ? "Provisioning AP" : "Disconnected")],
    ["Reconnect Count", w.reconnectCount],
  ]);

  fillTable("sensorTable", [
    ["Sensor Type", env.sensorType],
    ["Sensor Status", env.status],
    ["Last Reading", Probe.age(env.lastReadingAgeSeconds)],
    ["Temperature", Probe.fmt(env.temperature) + " °C"],
    ["Humidity", Probe.fmt(env.humidity) + " %RH"],
  ]);
}

document.getElementById("exportBtn").addEventListener("click", () => {
  const payload = { status: lastStatus, network: lastNetwork, environment: lastEnv, exportedAt: new Date().toISOString() };
  const blob = new Blob([JSON.stringify(payload, null, 2)], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "diagnostics-" + (lastStatus ? lastStatus.device.id : "device") + ".json";
  document.body.appendChild(a);
  a.click();
  a.remove();
  URL.revokeObjectURL(url);
});

document.getElementById("restartBtn").addEventListener("click", async () => {
  if (!confirm("Restart the device now?")) return;
  try { await Probe.post("/api/restart"); Probe.toast("Restarting…"); }
  catch (err) { Probe.toast("Failed: " + err.message); }
});

document.getElementById("factoryResetBtn").addEventListener("click", async () => {
  if (!confirm("This erases Wi-Fi credentials, settings, and history, then restarts into setup mode. Continue?")) return;
  try { await Probe.post("/api/factory-reset"); Probe.toast("Factory reset — restarting into setup mode"); }
  catch (err) { Probe.toast("Failed: " + err.message); }
});

Probe.poll(loadDiagnostics, 5000);
