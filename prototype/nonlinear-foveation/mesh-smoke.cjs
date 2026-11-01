"use strict";
const { CDP } = require("./gpu-smoke.cjs");
const fs = require("node:fs/promises"),
  path = require("node:path");
const debug = process.env.CDP_URL || "http://127.0.0.1:19226",
  url = (process.env.LAB_URL || "http://127.0.0.1:18767") + "/mesh.html";
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
    const end = Date.now() + 30000;
    while (
      !(await cdp.evaluate(
        'document.body && document.body.dataset.ready === "true"',
      ))
    ) {
      const error = await cdp.evaluate(
        "document.body && document.body.dataset.error",
      );
      if (error) throw new Error(error);
      if (Date.now() > end) throw new Error("Mesh fixture did not initialize");
      await new Promise((r) => setTimeout(r, 100));
    }
    const report = await cdp.evaluate("MeshLab.runTests()");
    if (!report.passed) throw new Error("Mesh checks failed");
    for (const level of [0, 6]) {
      await cdp.evaluate(
        "MeshLab.configure({strength:4,level:" + level + ",gaze:[0,0]})",
      );
      const data = await cdp.evaluate(
        'document.getElementById("mesh").toDataURL("image/png")',
      );
      await fs.writeFile(
        path.join(output, "mesh-level-" + level + ".png"),
        Buffer.from(data.split(",")[1], "base64"),
      );
    }
    await cdp.evaluate(
      "MeshLab.configure({strength:4,level:0,gaze:[0,0],bounded:true})",
    );
    const boundedImage = await cdp.evaluate(
      'window.meshExportUnderTest=document.getElementById("mesh").toDataURL("image/png")',
    );
    report.exportPaneRgb = await cdp.evaluate(`(async()=>{
      const image=new Image();image.src=window.meshExportUnderTest;await image.decode();
      const copy=document.createElement('canvas');copy.width=image.width;copy.height=image.height;
      const ctx=copy.getContext('2d');ctx.drawImage(image,0,0);const data=ctx.getImageData(0,0,copy.width,copy.height).data;
      const sums=[0,0,0,0];for(let y=0;y<copy.height;y++)for(let x=0;x<copy.width;x++){
        const i=(y*copy.width+x)*4,pane=(y>=copy.height/2?2:0)+(x>=copy.width/2?1:0);sums[pane]+=data[i]+data[i+1]+data[i+2];
      }return sums;
    })()`);
    if (report.exportPaneRgb.some((sum) => sum < 1000000))
      throw new Error("Exported image lost a comparison pane");
    await fs.writeFile(
      path.join(output, "mesh-bounded.png"),
      Buffer.from(boundedImage.split(",")[1], "base64"),
    );
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
      path.join(output, "mesh-viewer.png"),
      Buffer.from(shot.data, "base64"),
    );
    report.timings = await cdp.evaluate("MeshLab.measurePaths()");
    report.browserErrors = cdp.errors;
    if (cdp.errors.length) throw new Error("Browser exceptions");
    await fs.writeFile(
      path.join(output, "mesh-report.json"),
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
