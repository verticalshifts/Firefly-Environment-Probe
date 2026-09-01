// Minimal dependency-free canvas line/area chart. Handles one or two series
// sharing an x-axis of relative seconds-since-boot timestamps, with a soft
// fill, gridlines, x/y-axis labels, and a hover crosshair + tooltip.

function formatAgo(secondsAgo) {
  if (secondsAgo <= 0) return "now";
  if (secondsAgo < 60) return secondsAgo + "s ago";
  if (secondsAgo < 3600) return Math.round(secondsAgo / 60) + "m ago";
  if (secondsAgo < 86400) return (secondsAgo / 3600).toFixed(secondsAgo < 36000 ? 1 : 0) + "h ago";
  return (secondsAgo / 86400).toFixed(1) + "d ago";
}

function chartTooltip(canvas) {
  const card = canvas.closest(".card") || canvas.parentElement;
  let el = card.querySelector(":scope > .chart-tooltip[data-for='" + canvas.id + "']");
  if (!el) {
    el = document.createElement("div");
    el.className = "chart-tooltip";
    el.dataset.for = canvas.id;
    card.appendChild(el);
  }
  return el;
}

function drawLineChart(canvas, series, opts) {
  opts = opts || {};
  canvas._series = series;
  canvas._opts = opts;

  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const w = Math.max(rect.width, 200);
  const h = canvas.clientHeight || 220;

  canvas.width = w * dpr;
  canvas.height = h * dpr;
  const ctx = canvas.getContext("2d");
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, w, h);

  const gridColor = Probe.cssVar("--panel-border");
  const dimColor = Probe.cssVar("--text-dim");
  const textColor = Probe.cssVar("--text");

  const padL = 40, padR = 12, padT = 14, padB = 26;
  const plotW = w - padL - padR;
  const plotH = h - padT - padB;

  const allPoints = series.flatMap((s) => s.points);
  if (allPoints.length < 2) {
    ctx.fillStyle = dimColor;
    ctx.font = "13px -apple-system, sans-serif";
    ctx.fillText("Not enough data yet", padL, h / 2);
    chartTooltip(canvas).classList.remove("show");
    return;
  }

  const xs = allPoints.map((p) => p.x);
  const ys = allPoints.map((p) => p.y);
  let minY = Math.min(...ys), maxY = Math.max(...ys);
  if (minY === maxY) { minY -= 1; maxY += 1; }
  const pad = (maxY - minY) * 0.12;
  minY -= pad; maxY += pad;
  const minX = Math.min(...xs), maxX = Math.max(...xs);

  const xPix = (x) => padL + ((x - minX) / (maxX - minX || 1)) * plotW;
  const yPix = (y) => padT + plotH - ((y - minY) / (maxY - minY || 1)) * plotH;
  canvas._scale = { xPix, yPix, padL, padR, padT, padB, plotW, plotH, w, h, minX, maxX };

  // Gridlines + y labels — recessive, matches the panel border/dim-text tokens.
  ctx.strokeStyle = gridColor;
  ctx.fillStyle = dimColor;
  ctx.font = "11px -apple-system, sans-serif";
  ctx.lineWidth = 1;
  const gridLines = 4;
  for (let i = 0; i <= gridLines; i++) {
    const y = padT + (plotH / gridLines) * i;
    ctx.beginPath();
    ctx.moveTo(padL, y);
    ctx.lineTo(w - padR, y);
    ctx.stroke();
    const val = maxY - ((maxY - minY) / gridLines) * i;
    ctx.textAlign = "right";
    ctx.fillText(val.toFixed(0) + (opts.unit || ""), padL - 8, y + 3);
  }

  // X-axis: a few relative-time ticks across the plot width.
  ctx.textAlign = "center";
  const xTicks = 4;
  for (let i = 0; i <= xTicks; i++) {
    const x = minX + ((maxX - minX) / xTicks) * i;
    const secondsAgo = maxX - x;
    ctx.fillText(formatAgo(secondsAgo), xPix(x), h - 6);
  }
  ctx.textAlign = "left";

  series.forEach((s) => {
    // Soft fill under the line, fading to transparent at the baseline.
    const grad = ctx.createLinearGradient(0, padT, 0, padT + plotH);
    grad.addColorStop(0, s.color + "33");
    grad.addColorStop(1, s.color + "00");
    ctx.beginPath();
    s.points.forEach((p, i) => {
      const px = xPix(p.x), py = yPix(p.y);
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
    });
    ctx.lineTo(xPix(s.points[s.points.length - 1].x), padT + plotH);
    ctx.lineTo(xPix(s.points[0].x), padT + plotH);
    ctx.closePath();
    ctx.fillStyle = grad;
    ctx.fill();

    ctx.strokeStyle = s.color;
    ctx.lineWidth = 2;
    ctx.lineJoin = "round";
    ctx.lineCap = "round";
    ctx.beginPath();
    s.points.forEach((p, i) => {
      const px = xPix(p.x), py = yPix(p.y);
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
    });
    ctx.stroke();
  });

  // Hover crosshair + point markers + tooltip.
  if (opts.hoverX !== undefined && opts.hoverX !== null) {
    const nearest = series[0].points.reduce((best, p, i) => {
      const d = Math.abs(xPix(p.x) - opts.hoverX);
      return d < best.d ? { d, i, p } : best;
    }, { d: Infinity, i: -1, p: null });

    if (nearest.p) {
      const px = xPix(nearest.p.x);
      ctx.strokeStyle = dimColor;
      ctx.setLineDash([3, 3]);
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(px, padT);
      ctx.lineTo(px, padT + plotH);
      ctx.stroke();
      ctx.setLineDash([]);

      series.forEach((s) => {
        const p = s.points[nearest.i];
        if (!p) return;
        const py = yPix(p.y);
        ctx.beginPath();
        ctx.arc(px, py, 4, 0, Math.PI * 2);
        ctx.fillStyle = Probe.cssVar("--bg");
        ctx.fill();
        ctx.lineWidth = 2;
        ctx.strokeStyle = s.color;
        ctx.stroke();
      });

      const tip = chartTooltip(canvas);
      const lines = series.map((s) =>
        `<span class="chart-tooltip-swatch" style="background:${s.color}"></span>${s.label || ""} ${s.points[nearest.i].y.toFixed(opts.decimals === undefined ? 1 : opts.decimals)}${opts.unit || ""}`
      ).join("<br>");
      tip.innerHTML = `<div class="chart-tooltip-time">${formatAgo(maxX - nearest.p.x)}</div>${lines}`;
      tip.classList.add("show");
      const cardRect = canvas.closest(".card").getBoundingClientRect();
      let left = rect.left - cardRect.left + px + 12;
      if (left + 140 > cardRect.width) left = rect.left - cardRect.left + px - 152;
      tip.style.left = left + "px";
      tip.style.top = (rect.top - cardRect.top + yPix(nearest.p.y) - 12) + "px";
      return;
    }
  }
  chartTooltip(canvas).classList.remove("show");
}

function bindChartHover(canvas) {
  if (canvas._hoverBound) return;
  canvas._hoverBound = true;
  canvas.addEventListener("mousemove", (e) => {
    if (!canvas._series) return;
    const rect = canvas.getBoundingClientRect();
    drawLineChart(canvas, canvas._series, { ...canvas._opts, hoverX: e.clientX - rect.left });
  });
  canvas.addEventListener("mouseleave", () => {
    if (!canvas._series) return;
    drawLineChart(canvas, canvas._series, { ...canvas._opts, hoverX: null });
  });
}

document.addEventListener("DOMContentLoaded", () => {
  document.querySelectorAll("canvas.chart").forEach(bindChartHover);
});
