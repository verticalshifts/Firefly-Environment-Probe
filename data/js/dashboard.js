let refreshMs = 5000;

async function loadOnce() {
  const status = await Probe.get("/api/status");
  refreshMs = 5000;

  document.getElementById("hdrName").textContent = status.device.name || "Environment Probe";
  document.getElementById("hdrDot").style.background =
    status.environment.status === "HEALTHY" && status.network.status === "HEALTHY" ? "var(--healthy)" : "var(--warning)";

  document.getElementById("mTemp").innerHTML = Probe.fmt(status.environment.temperature) + " <small>°C</small>";
  document.getElementById("mTempSub").innerHTML = "";
  document.getElementById("mTempSub").appendChild(Probe.badge(status.environment.status));

  document.getElementById("mHum").innerHTML = Probe.fmt(status.environment.humidity) + " <small>%RH</small>";
  document.getElementById("mHumSub").textContent = status.environment.sensorType;

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
  drawLineChart(document.getElementById("miniChart"), [{ points: tempPoints, color: "#4f8cff" }]);
}

Probe.poll(loadOnce, refreshMs);
window.addEventListener("resize", () => loadOnce().catch(() => {}));
