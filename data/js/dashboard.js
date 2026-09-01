let refreshMs = 5000;

async function loadOnce() {
  const status = await Probe.get("/api/status");
  refreshMs = 5000;

  document.getElementById("hdrName").textContent = status.device.name || "Environment Probe";
  document.getElementById("hdrDot").style.background =
    status.environment.status === "HEALTHY" && status.network.status === "HEALTHY" ? "var(--healthy)" : "var(--warning)";

  const envOk = status.environment.status === "HEALTHY";
  Gauge.render(document.getElementById("gaugeTemp"), {
    value: envOk ? status.environment.temperature : null,
    errored: !envOk,
    min: 0, max: 45, low: 10, high: 35, unit: "°C", decimals: 1,
    label: "Temperature",
  });
  Gauge.render(document.getElementById("gaugeHum"), {
    value: envOk ? status.environment.humidity : null,
    errored: !envOk,
    min: 0, max: 100, low: 30, high: 80, unit: "%RH", decimals: 0,
    label: "Humidity",
  });

  document.getElementById("mDevice").textContent = status.network.connected ? "ONLINE" : "OFFLINE";
  document.getElementById("mDeviceSub").textContent =
    status.network.connected ? status.network.ssid + " · " + status.network.rssi + " dBm" : "Not connected";

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
