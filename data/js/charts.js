// Minimal dependency-free canvas line chart. Handles one or two series
// sharing an x-axis of relative seconds-ago timestamps.

function drawLineChart(canvas, series, opts) {
  opts = opts || {};
  const dpr = window.devicePixelRatio || 1;
  const rect = canvas.getBoundingClientRect();
  const w = Math.max(rect.width, 200);
  const h = canvas.clientHeight || 220;

  canvas.width = w * dpr;
  canvas.height = h * dpr;
  const ctx = canvas.getContext("2d");
  ctx.scale(dpr, dpr);
  ctx.clearRect(0, 0, w, h);

  const padL = 38, padR = 12, padT = 14, padB = 22;
  const plotW = w - padL - padR;
  const plotH = h - padT - padB;

  const allPoints = series.flatMap((s) => s.points);
  if (allPoints.length < 2) {
    ctx.fillStyle = "#8b93a7";
    ctx.font = "13px -apple-system, sans-serif";
    ctx.fillText("Not enough data yet", padL, h / 2);
    return;
  }

  const xs = allPoints.map((p) => p.x);
  const ys = allPoints.map((p) => p.y);
  let minY = Math.min(...ys), maxY = Math.max(...ys);
  if (minY === maxY) { minY -= 1; maxY += 1; }
  const pad = (maxY - minY) * 0.1;
  minY -= pad; maxY += pad;
  const minX = Math.min(...xs), maxX = Math.max(...xs);

  const xPix = (x) => padL + ((x - minX) / (maxX - minX || 1)) * plotW;
  const yPix = (y) => padT + plotH - ((y - minY) / (maxY - minY || 1)) * plotH;

  // Gridlines + y labels
  ctx.strokeStyle = "#232c42";
  ctx.fillStyle = "#8b93a7";
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
    ctx.fillText(val.toFixed(0), 4, y + 3);
  }

  series.forEach((s) => {
    ctx.strokeStyle = s.color;
    ctx.lineWidth = 2;
    ctx.beginPath();
    s.points.forEach((p, i) => {
      const px = xPix(p.x), py = yPix(p.y);
      if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py);
    });
    ctx.stroke();
  });
}
