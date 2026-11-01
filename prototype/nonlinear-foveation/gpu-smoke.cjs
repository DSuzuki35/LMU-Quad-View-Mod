// Test only our local fixture in a separate browser profile. No user tabs.
"use strict";
const fs = require("node:fs/promises"),
  path = require("node:path");
const debug = process.env.CDP_URL || "http://127.0.0.1:19226";
const page = process.env.LAB_URL || "http://127.0.0.1:18767";
const output = path.resolve(__dirname, "../../mod/build/nonlinear");
class CDP {
  constructor(url) {
    this.id = 0;
    this.pending = new Map();
    this.errors = [];
    this.socket = new WebSocket(url);
    this.ready = new Promise((resolve, reject) => {
      this.socket.addEventListener("open", resolve, { once: true });
      this.socket.addEventListener("error", reject, { once: true });
    });
    this.socket.addEventListener("message", (event) => {
      const message = JSON.parse(event.data);
      if (message.id && this.pending.has(message.id)) {
        const p = this.pending.get(message.id);
        this.pending.delete(message.id);
        clearTimeout(p.timeout);
        message.error
          ? p.reject(new Error(JSON.stringify(message.error)))
          : p.resolve(message.result);
      }
      if (message.method === "Runtime.exceptionThrown")
        this.errors.push(message.params.exceptionDetails);
    });
  }
  async call(method, params = {}) {
    await this.ready;
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error("CDP timeout: " + method));
      }, 60000);
      this.pending.set(id, { resolve, reject, timeout });
      this.socket.send(JSON.stringify({ id, method, params }));
    });
  }
  async evaluate(expression) {
    const result = await this.call("Runtime.evaluate", {
      expression,
      awaitPromise: true,
      returnByValue: true,
    });
    if (result.exceptionDetails)
      throw new Error(JSON.stringify(result.exceptionDetails));
    return result.result.value;
  }
}
async function main() {
  await fs.mkdir(output, { recursive: true });
  const target = await (
    await fetch(debug + "/json/new?" + encodeURIComponent(page), {
      method: "PUT",
    })
  ).json();
  const cdp = new CDP(target.webSocketDebuggerUrl);
  try {
    await cdp.call("Runtime.enable");
    await cdp.call("Page.enable");
    const deadline = Date.now() + 45000;
    while (
      !(await cdp.evaluate(
        'document.body && document.body.dataset.ready === "true"',
      ))
    ) {
      const error = await cdp.evaluate(
        "document.body && document.body.dataset.error",
      );
      if (error) throw new Error(error);
      if (Date.now() > deadline) throw new Error("Fixture did not initialize");
      await new Promise((r) => setTimeout(r, 100));
    }
    const report = {
      device: (await cdp.evaluate("NonlinearLab.getReport()")).device,
    };
    report.tests = await cdp.evaluate("NonlinearLab.runTests()");
    if (!report.tests.passed) throw new Error("GPU identity checks failed");
    report.scenarios = [];
    for (const settings of [
      {
        width: 768,
        height: 576,
        scale: 0.55,
        strength: 4,
        gaze: [0, 0],
        work: 8,
      },
      {
        width: 768,
        height: 576,
        scale: 0.55,
        strength: 4,
        gaze: [0.7, -0.6],
        work: 8,
      },
      {
        width: 768,
        height: 576,
        scale: 0.55,
        strength: 4,
        gaze: [0, 0],
        work: 48,
      },
      {
        width: 2048,
        height: 1536,
        scale: 0.55,
        strength: 4,
        gaze: [0, 0],
        work: 48,
      },
    ]) {
      await cdp.evaluate(
        "NonlinearLab.configure(" + JSON.stringify(settings) + ")",
      );
      const quality = await cdp.evaluate("NonlinearLab.measureQuality()");
      const timing = await cdp.evaluate("NonlinearLab.benchmark(8)");
      report.scenarios.push({
        settings,
        budget: (await cdp.evaluate("NonlinearLab.getReport()")).budget,
        quality,
        timing,
      });
    }
    await cdp.evaluate(
      "NonlinearLab.configure({width:768,height:576,work:8,gaze:[0,0]})",
    );
    await cdp.evaluate("NonlinearLab.measureQuality()");
    const metrics = await cdp.call("Page.getLayoutMetrics");
    const shot = await cdp.call("Page.captureScreenshot", {
      format: "png",
      captureBeyondViewport: true,
      clip: {
        x: 0,
        y: 0,
        width: metrics.cssContentSize.width,
        height: metrics.cssContentSize.height,
        scale: 1,
      },
    });
    await fs.writeFile(
      path.join(output, "viewer.png"),
      Buffer.from(shot.data, "base64"),
    );
    const image = await cdp.evaluate(
      'document.getElementById("view").toDataURL("image/png")',
    );
    await fs.writeFile(
      path.join(output, "comparison.png"),
      Buffer.from(image.split(",")[1], "base64"),
    );
    report.browserErrors = cdp.errors;
    if (cdp.errors.length)
      throw new Error("Browser exceptions: " + JSON.stringify(cdp.errors));
    await fs.writeFile(
      path.join(output, "gpu-report.json"),
      JSON.stringify(report, null, 2),
    );
    console.log(JSON.stringify(report, null, 2));
  } finally {
    cdp.socket.close();
    await fetch(debug + "/json/close/" + target.id);
  }
}
if (require.main === module)
  main().catch((error) => {
    console.error(error);
    process.exitCode = 1;
  });
module.exports = { CDP };
