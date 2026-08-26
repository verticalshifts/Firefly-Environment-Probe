async function loadInfo() {
  try {
    const info = await Probe.get("/api/provisioning-info");
    document.getElementById("deviceId").textContent = info.deviceId;
    document.getElementById("genUser").textContent = "Username: " + info.dashboardUsername;
    document.getElementById("genPass").textContent = "Password: " + info.dashboardPassword;
    document.getElementById("credsCard").style.display = "block";
  } catch (e) {
    // Not in provisioning mode (or already configured) — nothing to show.
  }
}

document.getElementById("provisionForm").addEventListener("submit", async (e) => {
  e.preventDefault();
  const statusEl = document.getElementById("formStatus");
  const payload = {};
  Array.from(e.target.elements).forEach((el) => {
    if (el.name && el.value !== "") payload[el.name] = el.value;
  });

  statusEl.textContent = "Connecting…";
  try {
    await Probe.post("/provision", payload);
    statusEl.textContent = "Saved. The device is joining your Wi-Fi network — this page will stop responding shortly. Reconnect your phone/laptop to your normal Wi-Fi, then find the device on your network (check your router, or try the device's mDNS name).";
  } catch (err) {
    statusEl.textContent = "Failed: " + err.message;
  }
});

loadInfo();
