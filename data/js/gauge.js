// Semicircular dial gauge — dependency-free inline SVG. Used for single
// values read against a comfort range (temperature, humidity): a continuous
// blue -> green -> orange -> red arc (the same universal "low -> good ->
// high" sweep for every metric) plus a needle pointing at the current
// value, with the number itself as the hero figure underneath. Owns the
// *whole* card — header icon/title/subtitle, dial, and the "Ideal Range"
// footer row — since the header icon is reused (at a smaller size) in that
// footer row and keeping both in one place avoids duplicating icon markup
// across every page that renders a gauge.
//
// Zone thresholds (low/high) are visual comfort bands, not read live from
// device config — the dashboard's read-only views are unauthenticated by
// design (see docs/architecture.md's Auth model) and alert thresholds live
// behind /api/config, which is. Defaults passed in by callers mirror
// ConfigManager's own defaults (tempLowC/tempHighC/humidityLowPct/
// humidityHighPct) so they match out of the box.

const Gauge = (() => {
  const CX = 100, CY = 100, R = 80, NEEDLE_R = 62, BAND_WIDTH = 14;
  const GRADIENT_ID = "gaugeArcGradient";
  // Fractional stops along the arc (0 = left end, 1 = right end) for both
  // the SVG <linearGradient> and the matching JS color interpolation used
  // to tint the needle's glow — kept in one array so the two never drift
  // apart. Colors are read from CSS custom properties (Probe.cssVar) at
  // render time rather than hardcoded, so this stays in sync with the
  // design tokens in style.css.
  const STOPS = [
    { frac: 0, varName: "--accent" },
    { frac: 0.45, varName: "--healthy" },
    { frac: 0.75, varName: "--warning" },
    { frac: 1, varName: "--critical" },
  ];

  const ICONS = {
    thermometer: '<path d="M12 15V5a2 2 0 0 0-4 0v10a3.5 3.5 0 1 0 4 0Z"/>',
    droplet: '<path d="M12 3c3.5 4 6 7.2 6 10.5a6 6 0 0 1-12 0C6 10.2 8.5 7 12 3Z"/>',
  };

  function iconSvg(name, size) {
    const path = ICONS[name] || ICONS.thermometer;
    return `<svg viewBox="0 0 24 24" width="${size}" height="${size}" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${path}</svg>`;
  }

  function fracToAngle(frac) {
    return 180 - frac * 180; // 0 -> 180deg (left), 1 -> 0deg (right)
  }

  function polarPoint(r, angleDeg) {
    const rad = (angleDeg * Math.PI) / 180;
    return { x: CX + r * Math.cos(rad), y: CY - r * Math.sin(rad) };
  }

  // Sub-arc of the top semicircle between two value-fractions (0..1),
  // sweeping clockwise (left -> top -> right) as fraction increases.
  function bandPath(frac0, frac1, r) {
    const a0 = fracToAngle(frac0), a1 = fracToAngle(frac1);
    const p0 = polarPoint(r, a0), p1 = polarPoint(r, a1);
    const largeArc = a0 - a1 > 180 ? 1 : 0;
    return `M${p0.x.toFixed(2)},${p0.y.toFixed(2)} A${r},${r} 0 ${largeArc} 1 ${p1.x.toFixed(2)},${p1.y.toFixed(2)}`;
  }

  function clamp01(x) { return Math.max(0, Math.min(1, x)); }

  function hexToRgb(hex) {
    const m = /^#?([0-9a-f]{2})([0-9a-f]{2})([0-9a-f]{2})$/i.exec((hex || "").trim());
    return m ? [parseInt(m[1], 16), parseInt(m[2], 16), parseInt(m[3], 16)] : [255, 255, 255];
  }

  // Interpolates the same 4-stop gradient the arc is painted with, at a
  // given fraction — used to tint the needle-tip glow so it always matches
  // the arc color directly underneath the needle.
  function gradientColorAt(stopsRgb, frac) {
    for (let i = 0; i < stopsRgb.length - 1; i++) {
      const a = stopsRgb[i], b = stopsRgb[i + 1];
      if (frac <= b.frac || i === stopsRgb.length - 2) {
        const t = clamp01((frac - a.frac) / (b.frac - a.frac || 1));
        const c = a.rgb.map((v, idx) => Math.round(v + (b.rgb[idx] - v) * t));
        return `rgb(${c[0]}, ${c[1]}, ${c[2]})`;
      }
    }
    return "rgb(255,255,255)";
  }

  // The gradient the arc is painted with lives once in the document (not
  // per-card) — every gauge's <path> references it by the same url(#id),
  // which is valid regardless of which SVG element originally declared it.
  function ensureGlobalDefs() {
    if (document.getElementById(GRADIENT_ID)) return;
    const stopsMarkup = STOPS.map((s) =>
      `<stop offset="${s.frac * 100}%" stop-color="${Probe.cssVar(s.varName)}"></stop>`).join("");
    const svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("width", "0");
    svg.setAttribute("height", "0");
    svg.style.position = "absolute";
    svg.innerHTML = `<defs><linearGradient id="${GRADIENT_ID}" x1="0" y1="0" x2="1" y2="0">${stopsMarkup}</linearGradient></defs>`;
    document.body.appendChild(svg);
  }

  // Builds the static DOM skeleton once; subsequent calls just update the
  // needle angle, value text, and labels — cheap enough to call on every
  // poll tick.
  function ensureSkeleton(container, opts) {
    if (container.querySelector("svg.gauge")) return;
    ensureGlobalDefs();
    container.classList.add("card", "gauge-card");
    if (opts.accent) container.classList.add("accent-" + opts.accent);

    container.innerHTML = `
      <div class="card-head">
        <div class="card-head-icon">${iconSvg(opts.icon, 20)}</div>
        <div>
          <div class="card-head-title"></div>
          <div class="card-head-subtitle"></div>
        </div>
      </div>
      <div class="gauge-wrap">
        <svg class="gauge" viewBox="0 0 200 108" role="img" aria-hidden="true">
          <path class="gauge-arc-glow" fill="none" stroke="url(#${GRADIENT_ID})" stroke-width="${BAND_WIDTH + 10}" stroke-linecap="round"></path>
          <path class="gauge-arc" fill="none" stroke="url(#${GRADIENT_ID})" stroke-width="${BAND_WIDTH}" stroke-linecap="round"></path>
          <circle class="gauge-needle-glow" r="10"></circle>
          <line class="gauge-needle" x1="${CX}" y1="${CY}" x2="${CX}" y2="${CY}"></line>
          <circle class="gauge-pivot" cx="${CX}" cy="${CY}" r="4"></circle>
          <text class="gauge-scale-min" x="${CX - R}" y="104"></text>
          <text class="gauge-scale-max" x="${CX + R}" y="104" text-anchor="end"></text>
        </svg>
        <div class="metric gauge-value"></div>
      </div>
      <div class="gauge-footer">
        <div class="gauge-zone-label"></div>
        <div class="info-row">
          <span class="info-row-icon">${iconSvg(opts.icon, 14)}</span>
          <span class="info-row-text"></span>
        </div>
      </div>`;

    container.querySelectorAll(".gauge-arc-glow, .gauge-arc").forEach((el) => el.setAttribute("d", bandPath(0, 1, R)));
  }

  // opts: { value, min, max, low, high, unit, decimals, label, zoneNames,
  //         errored, icon, accent, title, subtitle, rangeLabel } — pass
  //         value: null/NaN and errored: true when the sensor is in
  //         SENSOR_ERROR, so a stale last-known-good number doesn't render
  //         a misleadingly confident needle position.
  function render(container, opts) {
    ensureSkeleton(container, opts);
    const { value, min, max, low, high, unit, label } = opts;
    const decimals = opts.decimals === undefined ? 1 : opts.decimals;
    const zoneNames = opts.zoneNames || ["Low", "Comfortable", "High"];
    const rangeLabel = opts.rangeLabel || "Ideal Range";

    if (opts.title) container.querySelector(".card-head-title").textContent = opts.title;
    if (opts.subtitle) container.querySelector(".card-head-subtitle").textContent = opts.subtitle;

    const valid = value !== null && value !== undefined && !Number.isNaN(value);

    const needle = container.querySelector(".gauge-needle");
    const needleGlow = container.querySelector(".gauge-needle-glow");
    const valueEl = container.querySelector(".gauge-value");
    const zoneEl = container.querySelector(".gauge-zone-label");
    const rangeTextEl = container.querySelector(".info-row-text");

    rangeTextEl.textContent = `${rangeLabel}: ${Number(low).toFixed(0)} – ${Number(high).toFixed(0)}${unit || ""}`;
    container.querySelector(".gauge-scale-min").textContent = Number(min).toFixed(0);
    container.querySelector(".gauge-scale-max").textContent = Number(max).toFixed(0);

    if (!valid) {
      needle.setAttribute("x2", CX);
      needle.setAttribute("y2", CY);
      needleGlow.setAttribute("cx", CX);
      needleGlow.setAttribute("cy", CY);
      needleGlow.style.opacity = 0;
      valueEl.innerHTML = "— <small>" + (unit || "") + "</small>";
      zoneEl.innerHTML = "";
      // Sensor genuinely erroring (vs. just not-yet-read on first boot) —
      // say so rather than leaving a bare "—" with no explanation.
      if (opts.errored) zoneEl.appendChild(Probe.badge("SENSOR_ERROR"));
      return;
    }

    const frac = clamp01((value - min) / (max - min));
    const tip = polarPoint(NEEDLE_R, fracToAngle(frac));
    needle.setAttribute("x2", tip.x.toFixed(2));
    needle.setAttribute("y2", tip.y.toFixed(2));
    needleGlow.setAttribute("cx", tip.x.toFixed(2));
    needleGlow.setAttribute("cy", tip.y.toFixed(2));
    needleGlow.style.opacity = 1;

    const stopsRgb = STOPS.map((s) => ({ frac: s.frac, rgb: hexToRgb(Probe.cssVar(s.varName)) }));
    needleGlow.setAttribute("fill", gradientColorAt(stopsRgb, frac));

    valueEl.innerHTML = Number(value).toFixed(decimals) + (unit ? " <small>" + unit + "</small>" : "");

    const zoneIdx = value < low ? 0 : value > high ? 2 : 1;
    const zoneClass = ["WARNING", "HEALTHY", "CRITICAL"][zoneIdx];
    zoneEl.innerHTML = "";
    const badge = Probe.badge(zoneClass);
    badge.textContent = zoneNames[zoneIdx];
    zoneEl.appendChild(badge);
    if (label) container.querySelector("svg.gauge").setAttribute("aria-label", label + ": " + Number(value).toFixed(decimals) + (unit || ""));
  }

  return { render, iconSvg };
})();
