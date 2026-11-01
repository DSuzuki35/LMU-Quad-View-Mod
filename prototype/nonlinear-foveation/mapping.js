(function (root) {
  "use strict";
  function validate(point, gaze, strength) {
    if (
      !Number.isFinite(strength) ||
      strength < 0 ||
      strength > 12 ||
      point.length !== 2 ||
      gaze.length !== 2 ||
      point.some((v) => !Number.isFinite(v) || Math.abs(v) > 1 + 1e-9) ||
      gaze.some((v) => !Number.isFinite(v) || Math.abs(v) >= 1)
    )
      throw new RangeError(
        "Expected an eye-image point, interior gaze, and strength 0..12.",
      );
  }
  // Distance along this ray, normalized to its rectangular image boundary.
  // At any edge t=1. Scaling the displacement keeps the same ray/sector.
  function radius(point, gaze) {
    const d = point.map((v, i) => v - gaze[i]);
    return Math.max(
      ...d.map((v, i) => Math.abs(v) / (v >= 0 ? 1 - gaze[i] : 1 + gaze[i])),
    );
  }
  function transform(point, gaze, strength, inverse) {
    validate(point, gaze, strength);
    const t = radius(point, gaze);
    if (strength === 0 || t === 0 || Math.abs(t - 1) < 1e-14)
      return point.slice();
    const a = Math.asinh(strength);
    const mapped = inverse
      ? Math.sinh(a * t) / strength
      : Math.asinh(strength * t) / a;
    return point.map((v, i) => gaze[i] + ((v - gaze[i]) * mapped) / t);
  }
  function centerScale(strength) {
    return strength === 0 ? 1 : strength / Math.asinh(strength);
  }
  function density(point, gaze, strength) {
    validate(point, gaze, strength);
    const t = radius(point, gaze);
    if (strength === 0) return 1;
    if (t < 1e-10) return centerScale(strength) ** 2;
    // det(J) = radial derivative * tangential scale. Positive throughout
    // each sector; the continuous map has derivative seams at sector borders.
    const a = Math.asinh(strength);
    return (
      ((strength / (a * Math.sqrt(1 + (strength * t) ** 2))) *
        Math.asinh(strength * t)) /
      (a * t)
    );
  }
  function budget(width, height, scale, strength, focusFraction) {
    if (
      !Number.isInteger(width) ||
      !Number.isInteger(height) ||
      width < 1 ||
      height < 1 ||
      !Number.isFinite(scale) ||
      scale <= 0 ||
      scale > 1 ||
      !Number.isFinite(strength) ||
      strength < 0 ||
      strength > 12 ||
      !Number.isFinite(focusFraction) ||
      focusFraction <= 0 ||
      focusFraction > 1
    )
      throw new RangeError("Invalid render budget.");
    const center = scale * centerScale(strength);
    const remaining = scale ** 2 - (focusFraction * center) ** 2;
    const dims = (s) => [
      Math.max(1, Math.round(width * s)),
      Math.max(1, Math.round(height * s)),
    ];
    const source = dims(scale),
      focus = dims(focusFraction * center),
      outer = dims(Math.sqrt(Math.max(0, remaining)));
    return {
      width,
      height,
      scale,
      strength,
      focusFraction,
      centerScale: center,
      outerScale: Math.sqrt(Math.max(0, remaining)),
      feasible: remaining > 0,
      source,
      focus,
      outer,
      warpPixels: source[0] * source[1],
      quadPixels: focus[0] * focus[1] + outer[0] * outer[1],
    };
  }
  function conservativeBounds(points, gaze, strength) {
    if (points.length < 3) throw new RangeError("Expected a polygon.");
    points.forEach((p) => validate(p, gaze, strength));
    return intervalBounds(points, gaze, strength);
  }
  function intervalBounds(points, gaze, strength) {
    const lo = [0, 1].map((i) => Math.min(...points.map((p) => p[i]))),
      hi = [0, 1].map((i) => Math.max(...points.map((p) => p[i])));
    if (strength === 0) return [...lo, ...hi];
    const closest = gaze.map((v, i) => Math.max(lo[i], Math.min(hi[i], v)));
    const tmin = radius(closest, gaze),
      tmax = Math.max(
        ...[
          [lo[0], lo[1]],
          [lo[0], hi[1]],
          [hi[0], lo[1]],
          [hi[0], hi[1]],
        ].map((p) => radius(p, gaze)),
      );
    const factor = (t) =>
      t < 1e-12
        ? centerScale(strength)
        : Math.asinh(strength * t) / (Math.asinh(strength) * t);
    // f(t)/t decreases monotonically. Interval products enclose every point
    // in the original polygon's rectangle, hence every warped triangle point.
    // This is conservative rather than tight; the extra area costs fragments.
    const lower = factor(tmax),
      upper = factor(tmin),
      outLo = [],
      outHi = [];
    for (let i = 0; i < 2; i++) {
      const products = [
        (lo[i] - gaze[i]) * lower,
        (lo[i] - gaze[i]) * upper,
        (hi[i] - gaze[i]) * lower,
        (hi[i] - gaze[i]) * upper,
      ];
      outLo.push(Math.max(-1, gaze[i] + Math.min(...products)));
      outHi.push(Math.min(1, gaze[i] + Math.max(...products)));
    }
    return [...outLo, ...outHi];
  }
  function refinedBounds(
    points,
    gaze,
    strength,
    tolerance = [0.001, 0.001],
    maxDepth = 10,
  ) {
    const coarse = conservativeBounds(points, gaze, strength);
    if (
      tolerance.length !== 2 ||
      tolerance.some((v) => !Number.isFinite(v) || v <= 0) ||
      !Number.isInteger(maxDepth) ||
      maxDepth < 0 ||
      maxDepth > 12
    )
      throw new RangeError("Expected positive XY tolerances and depth 0..12.");
    if (strength === 0)
      return { bounds: coarse, segments: points.length, limitedSegments: 0 };
    const bounds = [Infinity, Infinity, -Infinity, -Infinity];
    let segments = 0,
      limitedSegments = 0;
    function edge(a, b, depth) {
      const mid = a.map((v, i) => (v + b[i]) * 0.5);
      const enclosure = intervalBounds([a, b], gaze, strength);
      const samples = [a, mid, b].map((p) =>
        transform(p, gaze, strength, false),
      );
      const tight = [0, 1].every(
        (i) =>
          Math.min(...samples.map((p) => p[i])) - enclosure[i] <=
            tolerance[i] &&
          enclosure[i + 2] - Math.max(...samples.map((p) => p[i])) <=
            tolerance[i],
      );
      if (!tight && depth < maxDepth) {
        edge(a, mid, depth + 1);
        edge(mid, b, depth + 1);
        return;
      }
      segments++;
      if (!tight) limitedSegments++;
      for (let i = 0; i < 2; i++) {
        bounds[i] = Math.min(bounds[i], enclosure[i]);
        bounds[i + 2] = Math.max(bounds[i + 2], enclosure[i + 2]);
      }
    }
    // An invertible continuous map preserves the polygon's boundary. Coordinate
    // extrema cannot occur in its open interior. Bound every edge continuously,
    // using interval enclosures even at the recursion cap (never samples alone).
    for (let i = 0; i < points.length; i++)
      edge(points[i], points[(i + 1) % points.length], 0);
    for (let i = 0; i < 2; i++) {
      bounds[i] = Math.max(bounds[i], coarse[i]);
      bounds[i + 2] = Math.min(bounds[i + 2], coarse[i + 2]);
    }
    return { bounds, segments, limitedSegments };
  }
  const api = {
    forward: (p, g, k) => transform(p, g, k, false),
    inverse: (p, g, k) => transform(p, g, k, true),
    radius,
    density,
    centerScale,
    budget,
    conservativeBounds,
    refinedBounds,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  root.FoveationMapping = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
