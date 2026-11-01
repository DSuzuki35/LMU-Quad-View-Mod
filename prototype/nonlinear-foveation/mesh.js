/* global FoveationShaders, FoveationMeshData, FoveationMapping */
(() => {
  "use strict";
  const $ = (id) => document.getElementById(id),
    canvas = $("mesh"),
    S = FoveationShaders,
    G = FoveationMeshData;
  const state = {
      strength: 4,
      level: 0,
      gaze: [0, 0],
      bounded: false,
      tight: false,
      boundDepth: 3,
    },
    width = 640,
    height = 480,
    scene = G.clippedScene(),
    cache = new Map();
  let gl,
    lastMetrics,
    rectanglePixels = 0,
    boundStats = null,
    busy = false;
  try {
    gl = canvas.getContext("webgl2", {
      antialias: false,
      preserveDrawingBuffer: true,
    });
    if (!gl) throw new Error("WebGL2 unavailable");
    const debug = gl.getExtension("WEBGL_debug_renderer_info");
    $("device").textContent = debug
      ? gl.getParameter(debug.UNMASKED_RENDERER_WEBGL)
      : gl.getParameter(gl.RENDERER);
    gl.disable(gl.DITHER);
    gl.disable(gl.BLEND);
    gl.disable(gl.CULL_FACE);
    const screenVao = gl.createVertexArray();
    function compile(type, source) {
      const s = gl.createShader(type);
      gl.shaderSource(s, source);
      gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS))
        throw new Error(gl.getShaderInfoLog(s));
      return s;
    }
    function program(vertex, fragment) {
      const p = gl.createProgram(),
        v = compile(gl.VERTEX_SHADER, vertex),
        f = compile(gl.FRAGMENT_SHADER, fragment);
      gl.attachShader(p, v);
      gl.attachShader(p, f);
      gl.linkProgram(p);
      gl.deleteShader(v);
      gl.deleteShader(f);
      if (!gl.getProgramParameter(p, gl.LINK_STATUS))
        throw new Error(gl.getProgramInfoLog(p));
      return p;
    }
    const fragment = (body) =>
      "#version 300 es\nprecision highp float;\nin vec2 vUv;\nout vec4 outColor;\n" +
      S.mapping +
      body;
    const meshVertex = `#version 300 es
      precision highp float;
      layout(location=0) in vec4 aClip;layout(location=1) in vec2 aUv;layout(location=2) in float aId;
      out vec2 vUv;flat out float vId;uniform bool uWarp;
      ${S.mapping}
      void main(){vec2 p=aClip.xy/aClip.w;if(uWarp)p=warp(p,false);gl_Position=vec4(p*aClip.w,aClip.z,aClip.w);vUv=aUv;vId=aId;}`;
    const material =
      "vec4 material(vec2 uv,float id){return vec4(.15+.75*uv.x,.15+.75*uv.y,.2+.3*id,id/255.0);}";
    const meshProgram = program(
      meshVertex,
      fragment(
        "flat in float vId;" +
          material +
          "void main(){outColor=material(vUv,vId);}",
      ),
    );
    const analyticProgram = program(
      S.vertex,
      fragment(`
      uniform vec4 uA[16],uB[16],uC[16];uniform vec2 uUvA[16],uUvB[16],uUvC[16];uniform float uId[16];uniform int uCount;
      ${material}
      float cross2(vec2 a,vec2 b){return a.x*b.y-a.y*b.x;}
      void main(){
        vec2 p=warp(vUv*2.0-1.0,true);float best=2.0;vec4 color=vec4(0);
        for(int i=0;i<16;i++){
          if(i>=uCount)break;
          vec2 a=uA[i].xy/uA[i].w,b=uB[i].xy/uB[i].w,c=uC[i].xy/uC[i].w;
          float determinant=cross2(b-a,c-a);if(abs(determinant)<1e-8)continue;
          float lb=cross2(p-a,c-a)/determinant,lc=cross2(b-a,p-a)/determinant;vec3 l=vec3(1.0-lb-lc,lb,lc);
          if(any(lessThan(l,vec3(0))))continue;
          float z=dot(l,vec3(uA[i].z/uA[i].w,uB[i].z/uB[i].w,uC[i].z/uC[i].w));
          if(z<-1.0||z>1.0||z>=best)continue;
          vec3 reciprocal=l/vec3(uA[i].w,uB[i].w,uC[i].w);
          vec2 uv=(reciprocal.x*uUvA[i]+reciprocal.y*uUvB[i]+reciprocal.z*uUvC[i])/(reciprocal.x+reciprocal.y+reciprocal.z);
          color=material(uv,uId[i]);best=z;
        }
        outColor=color;
      }`),
    );
    const reconstruct = program(S.vertex, fragment(S.reconstruct));
    const boundVertex = `#version 300 es
      precision highp float;uniform vec4 uBounds[16];flat out int vPrimitive;out vec2 vUv;
      void main(){const vec2 corners[6]=vec2[6](vec2(0,0),vec2(1,0),vec2(0,1),vec2(0,1),vec2(1,0),vec2(1,1));
      vec4 bounds=uBounds[gl_InstanceID];vec2 p=mix(bounds.xy,bounds.zw,corners[gl_VertexID]);
      gl_Position=vec4(p,0,1);vUv=p*.5+.5;vPrimitive=gl_InstanceID;}`;
    const boundProgram = program(
      boundVertex,
      fragment(`
      flat in int vPrimitive;uniform vec2 uSize;
      uniform vec4 uA[16],uB[16],uC[16];uniform vec2 uUvA[16],uUvB[16],uUvC[16];uniform float uId[16];
      ${material}
      float cross2(vec2 a,vec2 b){return a.x*b.y-a.y*b.x;}
      void main(){int i=vPrimitive;vec2 p=warp(gl_FragCoord.xy/uSize*2.0-1.0,true);
      vec2 a=uA[i].xy/uA[i].w,b=uB[i].xy/uB[i].w,c=uC[i].xy/uC[i].w;
      float determinant=cross2(b-a,c-a);if(abs(determinant)<1e-8)discard;
      float lb=cross2(p-a,c-a)/determinant,lc=cross2(b-a,p-a)/determinant;vec3 l=vec3(1.0-lb-lc,lb,lc);
      if(any(lessThan(l,vec3(0))))discard;
      float z=dot(l,vec3(uA[i].z/uA[i].w,uB[i].z/uB[i].w,uC[i].z/uC[i].w));if(z<-1.0||z>1.0)discard;
      vec3 reciprocal=l/vec3(uA[i].w,uB[i].w,uC[i].w);
      vec2 uv=(reciprocal.x*uUvA[i]+reciprocal.y*uUvB[i]+reciprocal.z*uUvC[i])/(reciprocal.x+reciprocal.y+reciprocal.z);
      outColor=material(uv,uId[i]);gl_FragDepth=z*.5+.5;}`),
    );
    const present = program(
      S.vertex,
      fragment(
        "uniform sampler2D uTexture;void main(){outColor=vec4(pow(texture(uTexture,vUv).rgb,vec3(1.0/2.2)),1);}",
      ),
    );
    const heat = program(
      S.vertex,
      fragment(`uniform sampler2D uMesh,uCorrect;
      void main(){vec2 uv=warp(vUv*2.0-1.0,false)*.5+.5;ivec2 size=textureSize(uMesh,0);ivec2 pixel=clamp(ivec2(uv*vec2(size)),ivec2(0),size-1);
      vec4 a=texelFetch(uMesh,pixel,0),b=texelFetch(uCorrect,pixel,0);bool wrong=abs(a.a-b.a)>.5/255.0;
      outColor=vec4(wrong?vec3(1,.04,.65):vec3(.025,length(a.rgb-b.rgb)*4.0+.025,.045),1);}`),
    );
    function target(depth = false) {
      const texture = gl.createTexture();
      gl.bindTexture(gl.TEXTURE_2D, texture);
      gl.texImage2D(
        gl.TEXTURE_2D,
        0,
        gl.RGBA8,
        width,
        height,
        0,
        gl.RGBA,
        gl.UNSIGNED_BYTE,
        null,
      );
      for (const p of [gl.TEXTURE_MIN_FILTER, gl.TEXTURE_MAG_FILTER])
        gl.texParameteri(gl.TEXTURE_2D, p, gl.LINEAR);
      for (const p of [gl.TEXTURE_WRAP_S, gl.TEXTURE_WRAP_T])
        gl.texParameteri(gl.TEXTURE_2D, p, gl.CLAMP_TO_EDGE);
      const framebuffer = gl.createFramebuffer();
      gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
      gl.framebufferTexture2D(
        gl.FRAMEBUFFER,
        gl.COLOR_ATTACHMENT0,
        gl.TEXTURE_2D,
        texture,
        0,
      );
      if (depth) {
        const buffer = gl.createRenderbuffer();
        gl.bindRenderbuffer(gl.RENDERBUFFER, buffer);
        gl.renderbufferStorage(
          gl.RENDERBUFFER,
          gl.DEPTH_COMPONENT24,
          width,
          height,
        );
        gl.framebufferRenderbuffer(
          gl.FRAMEBUFFER,
          gl.DEPTH_ATTACHMENT,
          gl.RENDERBUFFER,
          buffer,
        );
      }
      if (gl.checkFramebufferStatus(gl.FRAMEBUFFER) !== gl.FRAMEBUFFER_COMPLETE)
        throw new Error("Incomplete mesh target");
      return { texture, framebuffer };
    }
    const ordinary = target(true),
      warped = target(true),
      correct = target(),
      warpedOutput = target(),
      correctOutput = target();
    function bind(p, destination, viewport = [0, 0, width, height]) {
      gl.bindFramebuffer(
        gl.FRAMEBUFFER,
        destination ? destination.framebuffer : null,
      );
      gl.viewport(...viewport);
      gl.useProgram(p);
      gl.uniform2fv(gl.getUniformLocation(p, "uGaze"), state.gaze);
      gl.uniform1f(gl.getUniformLocation(p, "uStrength"), state.strength);
    }
    function texture(p, name, value, unit = 0) {
      gl.activeTexture(gl.TEXTURE0 + unit);
      gl.bindTexture(gl.TEXTURE_2D, value.texture);
      gl.uniform1i(gl.getUniformLocation(p, name), unit);
    }
    function mesh(level) {
      if (cache.has(level)) return cache.get(level);
      const triangles = G.subdivide(scene, level),
        data = new Float32Array(triangles.flat(2)),
        vao = gl.createVertexArray(),
        buffer = gl.createBuffer();
      gl.bindVertexArray(vao);
      gl.bindBuffer(gl.ARRAY_BUFFER, buffer);
      gl.bufferData(gl.ARRAY_BUFFER, data, gl.STATIC_DRAW);
      for (const [location, size, offset] of [
        [0, 4, 0],
        [1, 2, 16],
        [2, 1, 24],
      ]) {
        gl.enableVertexAttribArray(location);
        gl.vertexAttribPointer(location, size, gl.FLOAT, false, 28, offset);
      }
      const result = { vao, count: triangles.length * 3 };
      cache.set(level, result);
      return result;
    }
    function drawMesh(destination, level, warp) {
      bind(meshProgram, destination);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LESS);
      gl.clearDepth(1);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.uniform1i(gl.getUniformLocation(meshProgram, "uWarp"), warp ? 1 : 0);
      const data = mesh(level);
      gl.bindVertexArray(data.vao);
      gl.drawArrays(gl.TRIANGLES, 0, data.count);
      gl.disable(gl.DEPTH_TEST);
    }
    function fullscreen(p, destination, textures, viewport) {
      bind(p, destination, viewport);
      gl.bindVertexArray(screenVao);
      let unit = 0;
      for (const [name, value] of Object.entries(textures))
        texture(p, name, value, unit++);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    function uploadScene(p) {
      gl.uniform1i(gl.getUniformLocation(p, "uCount"), scene.length);
      for (const [index, suffix] of [
        [0, "A"],
        [1, "B"],
        [2, "C"],
      ]) {
        gl.uniform4fv(
          gl.getUniformLocation(p, "u" + suffix + "[0]"),
          new Float32Array(scene.flatMap((t) => t[index].slice(0, 4))),
        );
        gl.uniform2fv(
          gl.getUniformLocation(p, "uUv" + suffix + "[0]"),
          new Float32Array(scene.flatMap((t) => t[index].slice(4, 6))),
        );
      }
      gl.uniform1fv(
        gl.getUniformLocation(p, "uId[0]"),
        new Float32Array(scene.map((t) => t[0][6])),
      );
    }
    function analytic() {
      bind(analyticProgram, correct);
      gl.bindVertexArray(screenVao);
      uploadScene(analyticProgram);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
    }
    function prepareBounds(tight, maxDepth = state.boundDepth) {
      const start = performance.now();
      let segments = 0,
        limitedSegments = 0;
      const bounds = scene.map((triangle) => {
        const points = triangle.map((v) => [v[0] / v[3], v[1] / v[3]]);
        if (!tight)
          return FoveationMapping.conservativeBounds(
            points,
            state.gaze,
            state.strength,
          );
        const result = FoveationMapping.refinedBounds(
          points,
          state.gaze,
          state.strength,
          [0.5 / width, 0.5 / height],
          maxDepth,
        );
        segments += result.segments;
        limitedSegments += result.limitedSegments;
        return result.bounds;
      });
      rectanglePixels = 0;
      for (const box of bounds) {
        // One source-pixel guard covers conversion/rasterization rounding.
        box[0] = Math.max(-1, box[0] - 2 / width);
        box[1] = Math.max(-1, box[1] - 2 / height);
        box[2] = Math.min(1, box[2] + 2 / width);
        box[3] = Math.min(1, box[3] + 2 / height);
        rectanglePixels +=
          (box[2] - box[0]) * width * 0.5 * (box[3] - box[1]) * height * 0.5;
      }
      return {
        data: new Float32Array(bounds.flat()),
        areaOverSource: rectanglePixels / (width * height),
        segments,
        limitedSegments,
        cpuMs: performance.now() - start,
      };
    }
    function bounded(prepared = prepareBounds(state.tight)) {
      bind(boundProgram, warped);
      gl.bindVertexArray(screenVao);
      uploadScene(boundProgram);
      boundStats = prepared;
      rectanglePixels = prepared.areaOverSource * width * height;
      gl.uniform4fv(
        gl.getUniformLocation(boundProgram, "uBounds[0]"),
        prepared.data,
      );
      gl.uniform2f(gl.getUniformLocation(boundProgram, "uSize"), width, height);
      gl.enable(gl.DEPTH_TEST);
      gl.depthFunc(gl.LESS);
      gl.clearDepth(1);
      gl.clearColor(0, 0, 0, 0);
      gl.clear(gl.COLOR_BUFFER_BIT | gl.DEPTH_BUFFER_BIT);
      gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, scene.length);
      gl.disable(gl.DEPTH_TEST);
    }
    function read(t) {
      gl.bindFramebuffer(gl.FRAMEBUFFER, t.framebuffer);
      const data = new Uint8Array(width * height * 4);
      gl.readPixels(0, 0, width, height, gl.RGBA, gl.UNSIGNED_BYTE, data);
      return data;
    }
    function metrics() {
      const a = read(warped),
        b = read(correct);
      let totalWrong = 0,
        interiorWrong = 0,
        interiorCount = 0,
        sum = 0,
        colorCount = 0;
      for (let y = 0; y < height; y++)
        for (let x = 0; x < width; x++) {
          const i = (y * width + x) * 4,
            id = b[i + 3],
            wrong = a[i + 3] !== id;
          if (wrong) totalWrong++;
          if (x === 0 || y === 0 || x === width - 1 || y === height - 1)
            continue;
          if (
            [i - 4, i + 4, i - width * 4, i + width * 4].some(
              (n) => b[n + 3] !== id,
            )
          )
            continue;
          interiorCount++;
          if (wrong) interiorWrong++;
          else if (id !== 0)
            for (let c = 0; c < 3; c++) {
              sum += (a[i + c] - b[i + c]) ** 2;
              colorCount++;
            }
        }
      return {
        triangles: state.bounded
          ? scene.length * 2
          : mesh(state.level).count / 3,
        primitiveRectangles: state.bounded ? scene.length : 0,
        rectangleAreaOverSource: state.bounded
          ? rectanglePixels / (width * height)
          : null,
        boundPreparation: state.bounded
          ? {
              segments: boundStats.segments,
              limitedSegments: boundStats.limitedSegments,
              cpuMs: boundStats.cpuMs,
            }
          : null,
        totalWrongSurfacePixels: totalWrong,
        interiorWrongSurfacePixels: interiorWrong,
        interiorPixels: interiorCount,
        interiorWrongPercent: (100 * interiorWrong) / interiorCount,
        matchedSurfaceColorRmse255: Math.sqrt(sum / Math.max(1, colorCount)),
      };
    }
    function render() {
      drawMesh(ordinary, 0, false);
      analytic();
      if (state.bounded) bounded();
      else drawMesh(warped, state.level, true);
      fullscreen(reconstruct, correctOutput, { uTexture: correct });
      fullscreen(reconstruct, warpedOutput, { uTexture: warped });
      fullscreen(present, null, { uTexture: ordinary }, [
        0,
        height,
        width,
        height,
      ]);
      fullscreen(present, null, { uTexture: correctOutput }, [
        width,
        height,
        width,
        height,
      ]);
      fullscreen(present, null, { uTexture: warpedOutput }, [
        0,
        0,
        width,
        height,
      ]);
      fullscreen(heat, null, { uMesh: warped, uCorrect: correct }, [
        width,
        0,
        width,
        height,
      ]);
      lastMetrics = metrics();
      $("strength").value = state.strength;
      $("level").value = state.level;
      $("gx").value = state.gaze[0];
      $("gy").value = state.gaze[1];
      $("bounded").checked = state.bounded;
      $("tight").checked = state.tight;
      $("tight").disabled = !state.bounded;
      $("boundDepth").value = state.boundDepth;
      $("boundDepth").disabled = !state.bounded || !state.tight;
      $("boundDepthValue").textContent = state.boundDepth;
      $("level").disabled = state.bounded;
      $("candidateLabel").textContent = state.bounded
        ? "Primitive rectangles + exact intersection"
        : "Warped hardware mesh + reconstruction";
      $("strengthValue").textContent = state.strength.toFixed(1);
      $("levelValue").textContent =
        state.level +
        " → " +
        lastMetrics.triangles.toLocaleString() +
        " triangles";
      $("stats").textContent =
        `${lastMetrics.interiorWrongPercent.toFixed(3)}% interior source pixels show the wrong surface. Matched-surface color RMSE: ${lastMetrics.matchedSurfaceColorRmse255.toFixed(3)} / 255. ${scene.length} clipped triangles → ${lastMetrics.triangles.toLocaleString()} raster triangles.` +
        (state.bounded
          ? ` Conservative rectangles cover ${lastMetrics.rectangleAreaOverSource.toFixed(2)}× the source area before intersection rejection. Bounds: ${boundStats.cpuMs.toFixed(3)} ms CPU, ${boundStats.segments} refined edge segments (${boundStats.limitedSegments} at depth cap).`
          : "");
      // Diagnostic readback selects a source FBO. Restore the presentation target
      // before returning control to the browser or exporting the canvas.
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      const error = gl.getError();
      if (error !== gl.NO_ERROR) throw new Error("Mesh WebGL error " + error);
      return { ...lastMetrics };
    }
    function configure(values) {
      const next = { ...state, ...values };
      if (
        !Number.isInteger(next.level) ||
        next.level < 0 ||
        next.level > 7 ||
        !Number.isInteger(next.boundDepth) ||
        next.boundDepth < 0 ||
        next.boundDepth > 10 ||
        !Number.isFinite(next.strength) ||
        next.strength < 0 ||
        next.strength > 12 ||
        next.gaze.some((v) => !Number.isFinite(v) || Math.abs(v) > 0.8)
      )
        throw new Error("Invalid mesh controls");
      Object.assign(state, next);
      return render();
    }
    function runTests() {
      const saved = { ...state, gaze: state.gaze.slice() },
        results = [];
      try {
        const identity = configure({
          strength: 0,
          level: 0,
          gaze: [0, 0],
          bounded: false,
        });
        if (gl.getParameter(gl.FRAMEBUFFER_BINDING) !== null)
          throw new Error(
            "Mesh render left a diagnostic framebuffer selected for presentation/export",
          );
        if (
          identity.interiorWrongSurfacePixels !== 0 ||
          identity.matchedSurfaceColorRmse255 > 1
        )
          throw new Error(
            "Analytic mesh reference does not match ordinary rasterization",
          );
        results.push({ identity });
        for (const gaze of [
          [0, 0],
          [0.6, -0.5],
        ]) {
          const levels = [];
          for (const level of [0, 3, 6])
            levels.push({ level, ...configure({ strength: 4, gaze, level }) });
          if (
            levels[0].interiorWrongPercent < 1 ||
            levels[2].interiorWrongPercent >
              levels[0].interiorWrongPercent * 0.05 ||
            levels[2].matchedSurfaceColorRmse255 > 1
          )
            throw new Error(
              "Subdivision did not sufficiently reduce the expected warp errors",
            );
          results.push({ gaze, levels });
        }
        for (const strength of [0.1, 4, 12])
          for (const gaze of [
            [0, 0],
            [0.8, -0.8],
            [-0.7, 0.6],
          ]) {
            for (const boundDepth of [-1, 3, 10]) {
              const tight = boundDepth >= 0;
              const bounded = configure({
                strength,
                gaze,
                level: 0,
                bounded: true,
                tight,
                boundDepth: Math.max(0, boundDepth),
              });
              if (
                bounded.interiorWrongSurfacePixels > 2 ||
                bounded.matchedSurfaceColorRmse255 > 0.1
              )
                throw new Error(
                  "Bounded primitive rasterization differs from the analytic reference",
                );
              results.push({ strength, gaze, tight, boundDepth, bounded });
            }
          }
        return { passed: true, results };
      } finally {
        configure(saved);
      }
    }
    function summarize(values) {
      const sorted = values.slice().sort((a, b) => a - b);
      return {
        medianMs: sorted[Math.floor(sorted.length / 2)],
        minMs: sorted[0],
        maxMs: sorted[sorted.length - 1],
      };
    }
    async function measurePaths() {
      if (busy) throw new Error("A mesh measurement is already running");
      busy = true;
      const controls = [...document.querySelectorAll("button,input")];
      controls.forEach((control) => (control.disabled = true));
      const report = {
        state: { ...state, gaze: state.gaze.slice() },
        width,
        height,
        clippedTriangles: scene.length,
        cpuBounds: [],
        gpu: { available: false },
      };
      try {
        // Time CPU bounds independently. Cached bounds below deliberately isolate
        // GPU rasterization/reconstruction; their preparation is NOT free in use.
        const prepared = new Map();
        for (const depth of [-1, 0, 2, 3, 4, 6, 10]) {
          const times = [];
          let result;
          for (let i = 0; i < 12; i++) {
            const started = performance.now();
            for (let repeat = 0; repeat < 16; repeat++)
              result = prepareBounds(depth >= 0, Math.max(0, depth));
            if (i >= 2) times.push((performance.now() - started) / 16);
          }
          const name = depth < 0 ? "coarse" : "depth" + depth;
          prepared.set(name, result);
          report.cpuBounds.push({
            name,
            batch: 16,
            ...summarize(times),
            areaOverSource: result.areaOverSource,
            segments: result.segments,
            limitedSegments: result.limitedSegments,
          });
        }
        const ext = gl.getExtension("EXT_disjoint_timer_query_webgl2");
        if (!ext) {
          report.gpu.reason = "GPU timer queries unavailable";
          return report;
        }
        // Upload subdivision meshes before timed queries.
        for (const level of [0, 3, 6]) mesh(level);
        const paths = {
          ordinary: () => drawMesh(ordinary, 0, false),
          warpedVertices: () => drawMesh(warped, 0, true),
          subdivision3: () => drawMesh(warped, 3, true),
          subdivision6: () => drawMesh(warped, 6, true),
          coarse: () => bounded(prepared.get("coarse")),
          depth3: () => bounded(prepared.get("depth3")),
          depth10: () => bounded(prepared.get("depth10")),
        };
        const names = Object.keys(paths),
          samples = Object.fromEntries(names.map((n) => [n, []]));
        const iterations = 9,
          batch = 16;
        for (let iteration = 0; iteration < iterations + 2; iteration++) {
          const pending = [];
          try {
            for (let j = 0; j < names.length; j++) {
              const name = names[(iteration + j) % names.length],
                query = gl.createQuery();
              pending.push({ name, query });
              gl.beginQuery(ext.TIME_ELAPSED_EXT, query);
              for (let repeat = 0; repeat < batch; repeat++) {
                paths[name]();
                if (name !== "ordinary")
                  fullscreen(reconstruct, warpedOutput, { uTexture: warped });
              }
              gl.endQuery(ext.TIME_ELAPSED_EXT);
            }
            gl.flush();
            const started = performance.now();
            while (
              pending.some(
                (p) =>
                  !gl.getQueryParameter(p.query, gl.QUERY_RESULT_AVAILABLE),
              )
            ) {
              if (performance.now() - started > 10000)
                throw new Error("Mesh GPU timer timed out");
              await new Promise((resolve) => setTimeout(resolve, 20));
            }
            if (gl.getParameter(ext.GPU_DISJOINT_EXT))
              throw new Error("Disjoint GPU clock; discard mesh timings");
            if (iteration >= 2)
              for (const p of pending)
                samples[p.name].push(
                  gl.getQueryParameter(p.query, gl.QUERY_RESULT) / 1e6 / batch,
                );
          } finally {
            for (const p of pending) gl.deleteQuery(p.query);
          }
        }
        report.gpu = {
          available: true,
          renderer: $("device").textContent,
          iterations,
          batch,
          paths: Object.fromEntries(
            Object.entries(samples).map(([name, values]) => [
              name,
              summarize(values),
            ]),
          ),
          includes:
            "Drawing + depth + color reconstruction for nonlinear paths. Ordinary has no reconstruction. Cached bounds; CPU costs reported separately. Excludes presentation/readback.",
        };
        return report;
      } finally {
        busy = false;
        controls.forEach((control) => (control.disabled = false));
        render();
      }
    }
    $("timings").onclick = async () => {
      $("results").textContent = "Measuring bounds and GPU paths…";
      try {
        $("results").textContent = JSON.stringify(
          await measurePaths(),
          null,
          2,
        );
      } catch (error) {
        $("results").textContent = error.stack;
      }
    };
    for (const id of [
      "strength",
      "level",
      "gx",
      "gy",
      "bounded",
      "tight",
      "boundDepth",
    ])
      $(id).oninput = () =>
        configure({
          strength: +$("strength").value,
          level: +$("level").value,
          gaze: [+$("gx").value, +$("gy").value],
          bounded: $("bounded").checked,
          tight: $("tight").checked,
          boundDepth: +$("boundDepth").value,
        });
    $("tests").onclick = () => {
      try {
        $("results").textContent = JSON.stringify(runTests(), null, 2);
      } catch (error) {
        $("results").textContent = error.stack;
      }
    };
    window.MeshLab = {
      configure,
      runTests,
      measurePaths,
      getMetrics: () => ({ ...lastMetrics }),
    };
    render();
    document.body.dataset.ready = "true";
  } catch (error) {
    $("stats").textContent = error.message;
    document.body.dataset.error = error.message;
    console.error(error);
  }
})();
