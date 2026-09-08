let currentRange = "1h";

async function loadEnv() {
  const env = await Probe.get("/api/environment");
  // WARNING/CRITICAL are still real readings, just outside the comfort band —
  // the gauge shows its own zone badge for those. Only blank the value when
  // there is genuinely nothing to show (sensor faulted, or not read yet).
  const sensorErrored = env.status === "SENSOR_ERROR";
  const noReading = sensorErrored || env.status === "OFFLINE";
  Gauge.render(document.getElementById("gaugeTemp"), {
    value: noReading ? null : env.temperature,
    errored: sensorErrored,
    min: 0, max: 45, low: 10, high: 35, unit: "°C", decimals: 1,
    label: "Temperature", icon: "thermometer", accent: "blue",
    title: "Temperature", subtitle: "Current Reading",
  });
  Gauge.render(document.getElementById("gaugeHum"), {
    value: noReading ? null : env.humidity,
    errored: sensorErrored,
    min: 0, max: 100, low: 30, high: 80, unit: "%RH", decimals: 0,
    label: "Humidity", icon: "droplet", accent: "teal",
    title: "Humidity", subtitle: "Current Reading",
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
