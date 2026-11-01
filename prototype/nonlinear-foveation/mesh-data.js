(function (root) {
  "use strict";
  const mix = (a, b, t) => a.map((v, i) => v + (b[i] - v) * t);
  const vertex = (x, y, z, w, u, v, id) => [x * w, y * w, z * w, w, u, v, id];
  function originalScene() {
    return [
      [
        vertex(-0.88, -0.72, -0.25, 1, 0, 0, 1),
        vertex(0.86, -0.55, 0.3, 2.8, 1, 0, 1),
        vertex(-0.08, 0.88, 0.15, 1.5, 0.4, 1, 1),
      ],
      [
        vertex(-1.15, 0.52, -1.35, 0.7, 0, 0, 2),
        vertex(0.93, 0.66, 0.45, 2, 1, 0, 2),
        vertex(0.48, -0.86, -0.1, 1.2, 0.5, 1, 2),
      ],
    ];
  }
  function clip(triangle) {
    let polygon = triangle.map((v) => v.slice());
    // Clip before warping. Nonlinear interpolation is not a substitute for
    // ordinary homogeneous near/far/side clipping.
    for (let axis = 0; axis < 3; axis++)
      for (const sign of [-1, 1]) {
        const output = [];
        for (let i = 0; i < polygon.length; i++) {
          const a = polygon[i],
            b = polygon[(i + 1) % polygon.length],
            fa = a[3] + sign * a[axis],
            fb = b[3] + sign * b[axis];
          if (fa >= 0) output.push(a);
          if (fa >= 0 !== fb >= 0) output.push(mix(a, b, fa / (fa - fb)));
        }
        polygon = output;
      }
    const result = [];
    for (let i = 1; i + 1 < polygon.length; i++)
      result.push([polygon[0], polygon[i], polygon[i + 1]]);
    return result;
  }
  function clippedScene() {
    return originalScene().flatMap(clip);
  }
  function subdivide(triangles, level) {
    if (!Number.isInteger(level) || level < 0 || level > 7)
      throw new RangeError("Subdivision level must be 0..7.");
    let result = triangles;
    for (let step = 0; step < level; step++) {
      const next = [];
      for (const [a, b, c] of result) {
        const ab = mix(a, b, 0.5),
          bc = mix(b, c, 0.5),
          ca = mix(c, a, 0.5);
        next.push([a, ab, ca], [ab, b, bc], [ca, bc, c], [ab, bc, ca]);
      }
      result = next;
    }
    return result;
  }
  const api = { originalScene, clip, clippedScene, subdivide };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  root.FoveationMeshData = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
