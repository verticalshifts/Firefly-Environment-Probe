let refreshMs = 5000;

async function loadOnce() {
  const status = await Probe.get("/api/status");
  refreshMs = 5000;

  document.getElementById("hdrName").textContent = status.device.name || "Environment Probe";
  document.getElementById("hdrDot").style.background =
    status.environment.status === "HEALTHY" && status.network.status === "HEALTHY" ? "var(--healthy)" : "var(--warning)";

  // WARNING/CRITICAL are still real readings, just outside the comfort band —
  // the gauge shows its own zone badge for those. Only blank the value when
  // there is genuinely nothing to show (sensor faulted, or not read yet).
  const envStatus = status.environment.status;
  const sensorErrored = envStatus === "SENSOR_ERROR";
  const noReading = sensorErrored || envStatus === "OFFLINE";
  Gauge.render(document.getElementById("gaugeTemp"), {
    value: noReading ? null : status.environment.temperature,
    errored: sensorErrored,
    min: 0, max: 45, low: 10, high: 35, unit: "°C", decimals: 1,
    label: "Temperature", icon: "thermometer", accent: "blue",
    title: "Temperature", subtitle: "Ambient Temperature",
  });
  Gauge.render(document.getElementById("gaugeHum"), {
    value: noReading ? null : status.environment.humidity,
    errored: sensorErrored,
    min: 0, max: 100, low: 30, high: 80, unit: "%RH", decimals: 0,
    label: "Humidity", icon: "droplet", accent: "teal",
    title: "Humidity", subtitle: "Relative Humidity",
  });

  const online = status.network.connected;
  const statusText = document.getElementById("deviceStatusText");
  statusText.textContent = online ? "ONLINE" : "OFFLINE";
  statusText.className = "device-status-text " + (online ? "online" : "offline");
  document.getElementById("deviceStatusDot").style.color = online ? "var(--healthy)" : "var(--critical)";
  document.getElementById("deviceStatusSub").innerHTML =
    online ? status.network.ssid + "<br>" + status.network.rssi + " dBm" : "Not connected";
  document.getElementById("wifiRing").classList.toggle("offline", !online);
  document.getElementById("deviceSignalValue").textContent = online ? status.network.rssi + " dBm" : "—";

  const net = await Probe.get("/api/network");
  const rows = net.probes.map((p) => `
    <tr>
      <td>${p.label}<div class="hint">${p.target || ""}</div></td>
      <td></td>
      <td class="num">${p.reachable ? Probe.fmt(p.latencyMs, 1) + " ms" : "—"}</td>
      <td class="num">${Probe.fmt(p.packetLossPercent, 0)}%</td>
    </tr>`).join("");
  document.getElementById("netRows").innerHTML = rows;
  document.querySelectorAll("#netRows tr").forEach((tr, i) => {
    tr.children[1].appendChild(Probe.badge(net.probes[i].status));
  });

  const hist = await Probe.get("/api/history?range=6h");
  const tempPoints = hist.points.map((p) => ({ x: p.t, y: p.temp }));
  drawLineChart(document.getElementById("miniChart"),
    [{ points: tempPoints, color: Probe.cssVar("--accent"), label: "Temp" }],
    { unit: "°C", decimals: 1 });
}

Probe.poll(loadOnce, refreshMs);
window.addEventListener("resize", () => loadOnce().catch(() => {}));
