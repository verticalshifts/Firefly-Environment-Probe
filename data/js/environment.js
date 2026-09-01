let currentRange = "1h";

async function loadEnv() {
  const env = await Probe.get("/api/environment");
  const envOk = env.status === "HEALTHY";
  Gauge.render(document.getElementById("gaugeTemp"), {
    value: envOk ? env.temperature : null,
    errored: !envOk,
    min: 0, max: 45, low: 10, high: 35, unit: "°C", decimals: 1,
    label: "Temperature",
  });
  Gauge.render(document.getElementById("gaugeHum"), {
    value: envOk ? env.humidity : null,
    errored: !envOk,
    min: 0, max: 100, low: 30, high: 80, unit: "%RH", decimals: 0,
    label: "Humidity",
  });
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
  drawLineChart(document.getElementById("tempChart"),
    [{ points: tempPoints, color: Probe.cssVar("--accent"), label: "Temp" }],
    { unit: "°C", decimals: 1 });
  drawLineChart(document.getElementById("humChart"),
    [{ points: humPoints, color: Probe.cssVar("--healthy"), label: "Humidity" }],
    { unit: "%", decimals: 0 });
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
