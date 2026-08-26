async function loadNetwork() {
  const net = await Probe.get("/api/network");
  const w = net.wifi;

  document.getElementById("hdrDot").style.background = w.connected ? "var(--healthy)" : "var(--critical)";
  document.getElementById("wSsid").textContent = w.connected ? w.ssid : (w.provisioning ? "Provisioning AP: " + w.apSsid : "—");
  document.getElementById("wIp").textContent = w.ip;
  document.getElementById("wGw").textContent = w.gateway || "—";
  document.getElementById("wRssi").textContent = w.connected ? w.rssi + " dBm" : "—";
  document.getElementById("wChan").textContent = w.connected ? w.channel : "—";
  document.getElementById("wRecon").textContent = w.reconnectCount;

  const rows = net.probes.map((p) => `
    <tr>
      <td>${p.label}</td>
      <td>${p.target || "—"}${p.extra ? `<div class="hint">${p.extra}</div>` : ""}</td>
      <td></td>
      <td class="num">${p.reachable ? Probe.fmt(p.latencyMs, 1) + " ms" : "—"}</td>
      <td class="num">${Probe.fmt(p.packetLossPercent, 0)}%</td>
      <td class="hint">${p.lastProbeSecondsAgo !== undefined ? Probe.age(p.lastProbeSecondsAgo) : "—"}</td>
    </tr>`).join("");
  document.getElementById("probeRows").innerHTML = rows;
  document.querySelectorAll("#probeRows tr").forEach((tr, i) => {
    tr.children[2].appendChild(Probe.badge(net.probes[i].status));
  });
}

Probe.poll(loadNetwork, 5000);
