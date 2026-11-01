const test = require("node:test");
const assert = require("node:assert/strict");
const { TemporalAccumulator } = require("./temporal.js");
const pixel = (r, g = r, b = r, a = 255) => Uint8Array.from([r, g, b, a]);

test("static color bias has spatial error but no temporal change", () => {
  const acc = new TemporalAccumulator(1, 1);
  acc.add(pixel(12), pixel(10));
  acc.add(pixel(12), pixel(10));
  assert.equal(acc.summary().spatialRmse255, 2);
  assert.equal(acc.summary().errorChangeRmse255, 0);
  assert.equal(acc.heatRms()[0], 0);
});
test("reference motion cancels and alpha has no effect", () => {
  const acc = new TemporalAccumulator(1, 1);
  acc.add(pixel(14, 24, 34, 0), pixel(10, 20, 30, 255));
  acc.add(pixel(54, 64, 74, 255), pixel(50, 60, 70, 0));
  assert.equal(acc.summary().spatialRmse255, 4);
  assert.equal(acc.summary().errorChangeRmse255, 0);
});
test("alternating residual has known frame-change RMS", () => {
  const acc = new TemporalAccumulator(1, 1);
  for (const x of [12, 8, 12]) acc.add(pixel(x), pixel(10));
  assert.equal(acc.summary().spatialRmse255, 2);
  assert.equal(acc.summary().errorChangeRmse255, 4);
  assert.equal(acc.heatRms()[0], 4);
  assert.equal(acc.summary().frames, 3);
  assert.equal(acc.summary().transitions, 2);
});
test("dimensions and incomplete measurements are explicit", () => {
  assert.throws(() => new TemporalAccumulator(0, 1));
  const acc = new TemporalAccumulator(1, 1);
  assert.equal(acc.summary().spatialRmse255, null);
  assert.equal(acc.summary().errorChangeRmse255, null);
  assert.throws(() => acc.add(new Uint8Array(3), pixel(0)));
  acc.add(pixel(0), pixel(0));
  assert.equal(acc.summary().errorChangeRmse255, null);
});
