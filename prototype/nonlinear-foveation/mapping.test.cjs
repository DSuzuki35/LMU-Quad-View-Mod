const test = require("node:test");
const assert = require("node:assert/strict");
const M = require("./mapping.js");
const near = (a, b, eps = 1e-10) =>
  assert.ok(Math.abs(a - b) < eps, `${a} != ${b}`);

test("identity and center are exact", () => {
  for (const p of [
    [0, 0],
    [-1, 1],
    [0.7, -0.4],
  ])
    assert.deepEqual(M.forward(p, [0.2, -0.3], 0), p);
  assert.deepEqual(M.forward([0.2, -0.3], [0.2, -0.3], 8), [0.2, -0.3]);
  assert.deepEqual(M.inverse([0.2, -0.3], [0.2, -0.3], 8), [0.2, -0.3]);
});
test("dense full rectangle roundtrips including extreme gaze and corners", () => {
  for (const gaze of [
    [0, 0],
    [0.8, 0.8],
    [-0.8, 0.5],
    [0.4, -0.8],
  ])
    for (const k of [0, 0.00001, 0.2, 4, 12]) {
      for (let y = 0; y <= 40; y++)
        for (let x = 0; x <= 40; x++) {
          const p = [x / 20 - 1, y / 20 - 1],
            q = M.forward(p, gaze, k),
            back = M.inverse(q, gaze, k);
          assert.ok(
            q.every(
              (v) => Number.isFinite(v) && v >= -1 - 1e-12 && v <= 1 + 1e-12,
            ),
          );
          near(back[0], p[0]);
          near(back[1], p[1]);
          if (x === 0 || x === 40 || y === 0 || y === 40) {
            near(q[0], p[0]);
            near(q[1], p[1]);
          }
          assert.ok(M.density(p, gaze, k) > 0);
        }
    }
});
test("analytic area agrees with finite differences away from sector boundaries", () => {
  const p = [0.51, 0.23],
    g = [-0.1, 0.13],
    k = 5,
    h = 1e-6;
  const a = M.forward(p, g, k),
    x = M.forward([p[0] + h, p[1]], g, k),
    y = M.forward([p[0], p[1] + h], g, k);
  const det =
    ((x[0] - a[0]) * (y[1] - a[1]) - (y[0] - a[0]) * (x[1] - a[1])) / (h * h);
  near(det, M.density(p, g, k), 1e-5);
  near(M.centerScale(4), 4 / Math.asinh(4));
});
test("invalid domain is rejected", () => {
  for (const [p, g, k] of [
    [[NaN, 0], [0, 0], 1],
    [[0, 0], [1, 0], 1],
    [[1.1, 0], [0, 0], 1],
    [[0, 0], [0, 0], -1],
  ])
    assert.throws(() => M.forward(p, g, k));
});
test("quad comparison matches central density and nominal pixel budget", () => {
  const plan = M.budget(1024, 768, 0.55, 4, 0.3);
  assert.ok(plan.feasible);
  near(plan.centerScale, (0.55 * 4) / Math.asinh(4));
  near(
    plan.outerScale ** 2 + (plan.focusFraction * plan.centerScale) ** 2,
    0.55 ** 2,
  );
  assert.ok(
    Math.abs(plan.quadPixels - plan.warpPixels) / plan.warpPixels < 0.015,
  );
  assert.equal(M.budget(1024, 768, 0.4, 12, 0.8).feasible, false);
});
test("conservative per-triangle bounds contain dense warped interior samples", () => {
  const triangles = [
    [
      [0.1, 0.2],
      [0.8, 0.1],
      [0.4, 0.7],
    ],
    [
      [-0.9, -0.8],
      [0.9, -0.6],
      [-0.1, 0.9],
    ],
    [
      [-1, -1],
      [-0.7, 0.8],
      [0.2, 1],
    ],
  ];
  for (const points of triangles)
    for (const gaze of [
      [0, 0],
      [0.8, -0.8],
      [-0.6, 0.5],
    ])
      for (const k of [0, 4, 12]) {
        const bounds = M.conservativeBounds(points, gaze, k);
        for (let i = 0; i <= 30; i++)
          for (let j = 0; j <= 30 - i; j++) {
            const p = [0, 1].map(
              (axis) =>
                (i * points[0][axis] +
                  j * points[1][axis] +
                  (30 - i - j) * points[2][axis]) /
                30,
            );
            const q = M.forward(p, gaze, k);
            assert.ok(
              q[0] >= bounds[0] - 1e-10 &&
                q[1] >= bounds[1] - 1e-10 &&
                q[0] <= bounds[2] + 1e-10 &&
                q[1] <= bounds[3] + 1e-10,
            );
          }
      }
  assert.deepEqual(
    M.conservativeBounds(
      [
        [0.1, 0.2],
        [0.8, 0.1],
        [0.4, 0.7],
      ],
      [0, 0],
      0,
    ),
    [0.1, 0.1, 0.8, 0.7],
  );
});

test("refined boundary bounds stay conservative and reduce loose rectangles", () => {
  const points = [
    [-0.9, -0.8],
    [0.9, -0.6],
    [-0.1, 0.9],
  ];
  let improvements = 0;
  for (const gaze of [
    [0, 0],
    [0.8, -0.8],
    [-0.7, 0.6],
  ])
    for (const k of [0, 0.1, 4, 12]) {
      const coarse = M.conservativeBounds(points, gaze, k);
      for (const maxDepth of [0, 8]) {
        const result = M.refinedBounds(
          points,
          gaze,
          k,
          [0.001, 0.001],
          maxDepth,
        );
        const b = result.bounds;
        assert.ok(result.segments <= 3 * 2 ** maxDepth);
        assert.ok(
          b[0] >= coarse[0] - 1e-12 &&
            b[1] >= coarse[1] - 1e-12 &&
            b[2] <= coarse[2] + 1e-12 &&
            b[3] <= coarse[3] + 1e-12,
        );
        for (let i = 0; i <= 60; i++)
          for (let j = 0; j <= 60 - i; j++) {
            const p = [0, 1].map(
              (axis) =>
                (i * points[0][axis] +
                  j * points[1][axis] +
                  (60 - i - j) * points[2][axis]) /
                60,
            );
            const q = M.forward(p, gaze, k);
            assert.ok(
              q[0] >= b[0] - 1e-10 &&
                q[1] >= b[1] - 1e-10 &&
                q[0] <= b[2] + 1e-10 &&
                q[1] <= b[3] + 1e-10,
              "refinement lost interior coverage",
            );
          }
        if (
          maxDepth === 8 &&
          (b[2] - b[0]) * (b[3] - b[1]) <
            0.95 * (coarse[2] - coarse[0]) * (coarse[3] - coarse[1])
        )
          improvements++;
      }
    }
  assert.ok(
    improvements >= 3,
    "expected useful tightening across gaze positions",
  );
  assert.throws(() => M.refinedBounds(points, [0, 0], 4, [0, 0.001], 8));
  assert.throws(() => M.refinedBounds(points, [0, 0], 4, [0.001, 0.001], 15));
});
