// Shared helpers for every dashboard page. No frameworks, no CDN (section 13).

const Probe = {
  async get(path) {
    const res = await fetch(path, { cache: "no-store" });
    if (!res.ok) throw new Error("HTTP " + res.status);
    return res.json();
  },

  async post(path, body) {
    const res = await fetch(path, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {}),
    });
    let data = null;
    try { data = await res.json(); } catch (e) { /* no body */ }
    if (!res.ok) throw new Error((data && data.error) || "HTTP " + res.status);
    return data;
  },

  badge(status) {
    const s = (status || "UNKNOWN").toUpperCase();
    const span = document.createElement("span");
    span.className = "badge " + s;
    span.textContent = s.replace(/_/g, " ");
    return span;
  },

  fmt(value, digits) {
    if (value === null || value === undefined || Number.isNaN(value)) return "—";
    return Number(value).toFixed(digits === undefined ? 1 : digits);
  },

  age(seconds) {
    if (seconds === null || seconds === undefined) return "—";
    if (seconds < 60) return seconds + "s ago";
    if (seconds < 3600) return Math.floor(seconds / 60) + "m ago";
    if (seconds < 86400) return Math.floor(seconds / 3600) + "h ago";
    return Math.floor(seconds / 86400) + "d ago";
  },

  uptime(seconds) {
    seconds = Math.floor(seconds || 0);
    const d = Math.floor(seconds / 86400);
    const h = Math.floor((seconds % 86400) / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    if (d > 0) return `${d}d ${h}h ${m}m`;
    if (h > 0) return `${h}h ${m}m`;
    return `${m}m ${seconds % 60}s`;
  },

  toast(message) {
    let el = document.querySelector(".toast");
    if (!el) {
      el = document.createElement("div");
      el.className = "toast";
      document.body.appendChild(el);
    }
    el.textContent = message;
    el.classList.add("show");
    clearTimeout(el._t);
    el._t = setTimeout(() => el.classList.remove("show"), 2600);
  },

  // Polls `fn` every `intervalMs`, pausing while the tab is hidden to keep
  // network traffic (and device load) low (section 34).
  poll(fn, intervalMs) {
    let timer = null;
    const tick = async () => {
      if (!document.hidden) {
        try { await fn(); } catch (e) { console.warn(e); }
      }
      timer = setTimeout(tick, intervalMs);
    };
    tick();
    document.addEventListener("visibilitychange", () => {
      if (!document.hidden && timer) { clearTimeout(timer); tick(); }
    });
  },
};

// Highlights the current page in nav.tabs, if present.
document.addEventListener("DOMContentLoaded", () => {
  const path = location.pathname.split("/").pop() || "index.html";
  document.querySelectorAll("nav.tabs a").forEach((a) => {
    if (a.getAttribute("href") === path) a.classList.add("active");
  });
});
