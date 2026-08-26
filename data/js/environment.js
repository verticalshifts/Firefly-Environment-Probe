let currentRange = "1h";

async function loadEnv() {
  const env = await Probe.get("/api/environment");
  document.getElementById("mTemp").innerHTML = Probe.fmt(env.temperature) + " <small>°C</small>";
  document.getElementById("mHum").innerHTML = Probe.fmt(env.humidity) + " <small>%RH</small>";
  document.getElementById("mSensor").textContent = env.sensorType;
  document.getElementById("mSensorSub").innerHTML = "";
  document.getElementById("mSensorSub").appendChild(Probe.badge(env.status));
  document.getElementById("hdrDot").style.background =
    env.status === "HEALTHY" ? "var(--healthy)" : env.status === "SENSOR_ERROR" ? "var(--critical)" : "var(--warning)";
}

async function loadChart() {
  const hist = await Probe.get("/api/history?range=" + currentRange);
  const tempPoints = hist.points.map((p) => ({ x: p.t, y: p.temp }));
  const humPoints = hist.points.map((p) => ({ x: p.t, y: p.hum }));
  drawLineChart(document.getElementById("tempChart"), [{ points: tempPoints, color: "#4f8cff" }]);
  drawLineChart(document.getElementById("humChart"), [{ points: humPoints, color: "#33c17a" }]);
}

document.getElementById("rangePicker").addEventListener("click", (e) => {
  if (e.target.tagName !== "BUTTON") return;
  document.querySelectorAll("#rangePicker button").forEach((b) => b.classList.remove("active"));
  e.target.classList.add("active");
  currentRange = e.target.dataset.range;
  loadChart().catch(() => {});
});

window.addEventListener("resize", () => loadChart().catch(() => {}));

Probe.poll(loadEnv, 5000);
Probe.poll(loadChart, 30000);
