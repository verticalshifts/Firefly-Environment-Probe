// Semicircular dial gauge — dependency-free inline SVG. Used for single
// values read against a comfort range (temperature, humidity): a segmented
// three-zone band (low / healthy / high) plus a needle pointing at the
// current value, with the number itself as the hero figure underneath.
//
// Zone thresholds are visual comfort bands, not read live from device
// config — the dashboard's read-only views are unauthenticated by design
// (see docs/architecture.md's Auth model) and alert thresholds live behind
// /api/config, which is. Defaults below mirror ConfigManager's own
// defaults (tempLowC/tempHighC/humidityLowPct/humidityHighPct) so they
// match out of the box; pass custom {low, high} if you've changed those in
// Settings and want the gauge to reflect it.

const Gauge = (() => {
  const CX = 100, CY = 100, R = 80, NEEDLE_R = 62, BAND_WIDTH = 16;
  const GAP_FRAC = 0.012; // small angular gap between zone bands (surface-gap rule)

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

  // Builds the static SVG skeleton once; subsequent calls just update the
  // needle angle, value text, and zone badge — cheap enough to call on
  // every poll tick.
  function ensureSkeleton(container) {
    if (container.querySelector("svg.gauge")) return;
    container.innerHTML = `
      <svg class="gauge" viewBox="0 0 200 108" role="img" aria-hidden="true">
        <path class="gauge-band gauge-band-low" fill="none" stroke-width="${BAND_WIDTH}"></path>
        <path class="gauge-band gauge-band-mid" fill="none" stroke-width="${BAND_WIDTH}"></path>
        <path class="gauge-band gauge-band-high" fill="none" stroke-width="${BAND_WIDTH}"></path>
        <line class="gauge-needle" x1="${CX}" y1="${CY}" x2="${CX}" y2="${CY}"></line>
        <circle class="gauge-pivot" cx="${CX}" cy="${CY}" r="4"></circle>
        <text class="gauge-scale-min" x="${CX - R}" y="104"></text>
        <text class="gauge-scale-max" x="${CX + R}" y="104" text-anchor="end"></text>
      </svg>
      <div class="metric gauge-value"></div>
      <div class="gauge-zone-label"></div>`;
  }

  // opts: { value, min, max, low, high, unit, decimals, label, zoneNames,
  //         errored } — pass value: null/NaN and errored: true when the
  //         sensor is in SENSOR_ERROR, so a stale last-known-good number
  //         doesn't render a misleadingly confident needle position.
  function render(container, opts) {
    ensureSkeleton(container);
    const { value, min, max, low, high, unit, label } = opts;
    const decimals = opts.decimals === undefined ? 1 : opts.decimals;
    const zoneNames = opts.zoneNames || ["Low", "Comfortable", "High"];

    const valid = value !== null && value !== undefined && !Number.isNaN(value);

    const fLow = clamp01((low - min) / (max - min));
    const fHigh = clamp01((high - min) / (max - min));

    container.querySelector(".gauge-band-low")
      .setAttribute("d", bandPath(0, Math.max(0, fLow - GAP_FRAC), R));
    container.querySelector(".gauge-band-mid")
      .setAttribute("d", bandPath(fLow + GAP_FRAC, Math.max(fLow + GAP_FRAC, fHigh - GAP_FRAC), R));
    container.querySelector(".gauge-band-high")
      .setAttribute("d", bandPath(fHigh + GAP_FRAC, 1, R));

    const needle = container.querySelector(".gauge-needle");
    const valueEl = container.querySelector(".gauge-value");
    const zoneEl = container.querySelector(".gauge-zone-label");

    if (!valid) {
      needle.setAttribute("x2", CX);
      needle.setAttribute("y2", CY);
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

    valueEl.innerHTML = Number(value).toFixed(decimals) + (unit ? " <small>" + unit + "</small>" : "");

    container.querySelector(".gauge-scale-min").textContent = Number(min).toFixed(0);
    container.querySelector(".gauge-scale-max").textContent = Number(max).toFixed(0);

    const zoneIdx = value < low ? 0 : value > high ? 2 : 1;
    const zoneClass = ["WARNING", "HEALTHY", "CRITICAL"][zoneIdx];
    zoneEl.innerHTML = "";
    const badge = Probe.badge(zoneClass);
    badge.textContent = zoneNames[zoneIdx];
    zoneEl.appendChild(badge);
    if (label) container.querySelector("svg.gauge").setAttribute("aria-label", label + ": " + Number(value).toFixed(decimals) + (unit || ""));
  }

  return { render };
})();
