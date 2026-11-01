const test = require("node:test"),
  assert = require("node:assert/strict");
const G = require("./mesh-data.js");
test("homogeneous clipping preserves finite frustum domain and attributes", () => {
  const triangles = G.clippedScene();
  assert.ok(triangles.length >= 2);
  for (const triangle of triangles)
    for (const v of triangle) {
      assert.equal(v.length, 7);
      assert.ok(v.every(Number.isFinite));
      assert.ok(
        v[3] > 0 &&
          Math.abs(v[0]) <= v[3] + 1e-9 &&
          Math.abs(v[1]) <= v[3] + 1e-9 &&
          Math.abs(v[2]) <= v[3] + 1e-9,
      );
      assert.ok(v[4] >= 0 && v[4] <= 1 && v[5] >= 0 && v[5] <= 1);
    }
});
test("uniform subdivision grows by four and preserves original vertices/IDs", () => {
  const base = G.clippedScene();
  for (let level = 0; level <= 4; level++) {
    const result = G.subdivide(base, level);
    assert.equal(result.length, base.length * 4 ** level);
    assert.deepEqual(result[0][0], base[0][0]);
    for (const triangle of result)
      assert.ok(triangle.every((v) => v[6] === triangle[0][6]));
  }
  assert.throws(() => G.subdivide(base, 9));
  assert.throws(() => G.subdivide(base, -1));
});
test("all clipped/subdivided vertices retain their original plane", () => {
  const original = G.clippedScene()[0],
    result = G.subdivide([original], 5);
  // Original UVs provide independent barycentric coordinates on the triangle.
  const [a, b, c] = original,
    den = (b[4] - a[4]) * (c[5] - a[5]) - (c[4] - a[4]) * (b[5] - a[5]);
  for (const triangle of result)
    for (const v of triangle) {
      const lb =
        ((v[4] - a[4]) * (c[5] - a[5]) - (c[4] - a[4]) * (v[5] - a[5])) / den;
      const lc =
        ((b[4] - a[4]) * (v[5] - a[5]) - (v[4] - a[4]) * (b[5] - a[5])) / den;
      for (let i = 0; i < 4; i++)
        assert.ok(
          Math.abs(v[i] - ((1 - lb - lc) * a[i] + lb * b[i] + lc * c[i])) <
            1e-10,
        );
    }
});
