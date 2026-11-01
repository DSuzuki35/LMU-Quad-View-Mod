(function (root) {
  "use strict";
  class TemporalAccumulator {
    constructor(width, height) {
      if (
        !Number.isInteger(width) ||
        !Number.isInteger(height) ||
        width < 1 ||
        height < 1
      )
        throw new RangeError("Expected positive image dimensions");
      this.pixels = width * height;
      this.previous = new Int16Array(this.pixels * 3);
      this.heatSquared = new Float64Array(this.pixels);
      this.frames = 0;
      this.spatialSquared = 0;
      this.changeSquared = 0;
    }
    add(output, reference) {
      if (
        output.length !== this.pixels * 4 ||
        reference.length !== output.length
      )
        throw new RangeError("Expected matching RGBA images");
      let spatial = 0,
        change = 0;
      for (let pixel = 0; pixel < this.pixels; pixel++) {
        let deltaSquared = 0;
        for (let channel = 0; channel < 3; channel++) {
          const error =
            output[pixel * 4 + channel] - reference[pixel * 4 + channel];
          spatial += error * error;
          if (this.frames > 0) {
            const delta = error - this.previous[pixel * 3 + channel];
            deltaSquared += delta * delta;
          }
          this.previous[pixel * 3 + channel] = error;
        }
        this.heatSquared[pixel] += deltaSquared;
        change += deltaSquared;
      }
      this.spatialSquared += spatial;
      this.changeSquared += change;
      this.frames++;
    }
    summary() {
      const transitions = Math.max(0, this.frames - 1);
      return {
        frames: this.frames,
        transitions,
        spatialRmse255: this.frames
          ? Math.sqrt(this.spatialSquared / (this.pixels * 3 * this.frames))
          : null,
        errorChangeRmse255: transitions
          ? Math.sqrt(this.changeSquared / (this.pixels * 3 * transitions))
          : null,
      };
    }
    heatRms() {
      const divisor = 3 * Math.max(1, this.frames - 1);
      return Float32Array.from(this.heatSquared, (v) => Math.sqrt(v / divisor));
    }
  }
  const api = { TemporalAccumulator };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  root.FoveationTemporal = api;
})(typeof globalThis !== "undefined" ? globalThis : this);
