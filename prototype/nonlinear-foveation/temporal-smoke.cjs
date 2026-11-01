"use strict";
const { CDP } = require("./gpu-smoke.cjs");
const fs = require("node:fs/promises"),
  path = require("node:path");
const debug = process.env.CDP_URL || "http://127.0.0.1:19226";
const url = process.env.LAB_URL || "http://127.0.0.1:18767";
const output = path.resolve(__dirname, "../../mod/build/nonlinear");
(async () => {
  await fs.mkdir(output, { recursive: true });
  const target = await (
    await fetch(debug + "/json/new?" + encodeURIComponent(url), {
      method: "PUT",
    })
  ).json();
  const cdp = new CDP(target.webSocketDebuggerUrl);
  try {
    await cdp.call("Runtime.enable");
    await cdp.call("Page.enable");
    const deadline = Date.now() + 30000;
    while (
      !(await cdp.evaluate(
        'document.body && document.body.dataset.ready === "true"',
      ))
    ) {
      const error = await cdp.evaluate(
        "document.body && document.body.dataset.error",
      );
      if (error) throw new Error(error);
      if (Date.now() > deadline)
        throw new Error("Temporal fixture did not initialize");
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    const report = { tests: [], scenarios: [] };
    await cdp.evaluate(
      "NonlinearLab.configure({width:128,height:96,strength:0,scale:1,work:0,camera:0,gaze:[0,0]})",
    );
    for (const cameraMotion of [false, true]) {
      const result = await cdp.evaluate(
        `NonlinearLab.measureTemporal({frames:9,cameraMotion:${cameraMotion}})`,
      );
      if (
        result.paths.nonlinear.errorChangeRmse255 !==
        result.paths.reference.errorChangeRmse255
      )
        throw new Error(
          "Identity nonlinear temporal changes differ from ordinary",
        );
      if (!cameraMotion && result.paths.reference.errorChangeRmse255 !== 0)
        throw new Error("Stationary ordinary rendering changed with gaze");
      if (
        Object.values(result.paths).some(
          (p) => p.closedReturnChangedBytes !== 0,
        )
      )
        throw new Error("Identity trajectory did not close exactly");
      report.tests.push({ identity: true, ...result });
    }
    for (const cameraMotion of [false, true]) {
      await cdp.evaluate(
        "NonlinearLab.configure({width:768,height:576,strength:4,scale:.55,work:8,camera:0,gaze:[0,0],filter:true})",
      );
      const result = await cdp.evaluate(
        `NonlinearLab.measureTemporal({frames:24,cameraMotion:${cameraMotion}})`,
      );
      if (!cameraMotion && result.paths.reference.errorChangeRmse255 !== 0)
        throw new Error("Ordinary image changed with gaze");
      if (result.paths.nonlinear.errorChangeRmse255 <= 0)
        throw new Error("Warped trajectory did not change sampling");
      for (const metrics of Object.values(result.paths)) {
        if (
          !Number.isFinite(metrics.errorChangeRmse255) ||
          !Number.isFinite(metrics.spatialRmse255)
        )
          throw new Error("Nonfinite temporal result");
        if (metrics.closedReturnChangedBytes !== 0)
          throw new Error("Warped trajectory did not return exactly");
      }
      report.scenarios.push(result);
      const png = await cdp.evaluate(
        'document.getElementById("temporalView").toDataURL("image/png")',
      );
      await fs.writeFile(
        path.join(
          output,
          cameraMotion ? "temporal-camera.png" : "temporal-gaze.png",
        ),
        Buffer.from(png.split(",")[1], "base64"),
      );
    }
    const layout = await cdp.call("Page.getLayoutMetrics");
    const shot = await cdp.call("Page.captureScreenshot", {
      format: "png",
      captureBeyondViewport: true,
      clip: {
        x: 0,
        y: 0,
        width: layout.cssContentSize.width,
        height: layout.cssContentSize.height,
        scale: 1,
      },
    });
    await fs.writeFile(
      path.join(output, "temporal-viewer.png"),
      Buffer.from(shot.data, "base64"),
    );
    report.browserErrors = cdp.errors;
    if (cdp.errors.length) throw new Error("Temporal browser exceptions");
    report.passed = true;
    await fs.writeFile(
      path.join(output, "temporal-report.json"),
      JSON.stringify(report, null, 2),
    );
    console.log(JSON.stringify(report, null, 2));
  } finally {
    cdp.socket.close();
    await fetch(debug + "/json/close/" + target.id);
  }
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
