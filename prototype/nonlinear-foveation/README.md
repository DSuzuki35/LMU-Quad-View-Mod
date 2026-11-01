# Standalone nonlinear foveation lab

This is the first implemented slice of the
[nonlinear/conformal research](../../docs/CONFORMAL-FOVEATION-RESEARCH.md).
It uses an owned procedural scene and WebGL2. It does not load LMU, modify game
files, install an XR layer, or use recorded game shaders. SPS remains separate.

## Run

From the repository root, with Node 18 or newer:

```powershell
node prototype/nonlinear-foveation/server.cjs
```

Open <http://127.0.0.1:18767>. An optional positional port overrides the default.
The viewer can also be opened directly from `index.html`; it has no dependencies,
external assets, telemetry or build step. Hardware-accelerated WebGL2 is required.

The four panes show ordinary full-resolution rendering, nonlinear rendering plus
reconstruction, a quad-style context/focus comparator, and the distorted source
buffer. Move the pointer to move gaze, or use the sliders/scripted motion. Gaze is
simulated: there is no headset/eye-tracker connection. The source pane is stretched
to the same display size for inspection; its true dimensions are listed above it.

**Match 100% center density** chooses the source scale that gives the same local
linear sample density at gaze as a 100% ordinary render. Strength increases the
central expansion and peripheral compression. The density-map toggle exposes that
tradeoff. Filtering analytically averages the procedural checker patterns over
their local pixel footprints; thin geometric edges can still alias.

## Mapping and comparison contract

For eye-image position `p`, gaze `g`, and displacement `d=p-g`, compute `t` as the
maximum of the absolute X/Y displacement divided by the corresponding distance
from gaze to that image edge. Thus `t=1` anywhere on the rectangular boundary.

Forward radius: `f(t) = asinh(k*t) / asinh(k)`.
Inverse radius: `f^-1(t) = sinh(asinh(k)*t) / k`.
Map position: `g + d * f(t)/t`; handle zero strength and zero radius explicitly.

The full boundary is preserved, including with off-center gaze. Inside each
directional sector, area scale is `f'(t)*f(t)/t > 0`. Derivative seams remain
between rectangle sectors. This is **not strictly conformal**: it does not
preserve local angles everywhere.

If source linear scale is `s`, its central linear density is
`q = s*k/asinh(k)`. A square quad focus fraction `f` with density `q` consumes
`(f*q)^2` of the full image's scene samples. Choose outer scale
`sqrt(s^2-(f*q)^2)` to match total scene-sample budget. Impossible budgets are
explicitly marked. Actual integer dimensions cause a small reported mismatch.
This matches a point density and sample count, **not equal perceptual quality**.
The quad region is clamped inside the image near edge gaze. Blending and guard
bands are deliberately absent; adding them costs more samples/passes.

The scene is evaluated at inverse-mapped rays *before* shading the small source
image. A full-resolution pass then reconstructs color. The quad comparator shades
two images and composes them in another output pass. Budget counters show scene
samples and output-pass pixels separately. The standard reference has no extra
reconstruction pass.

## Tests and measurements

```powershell
node --test prototype/nonlinear-foveation/mapping.test.cjs
```

The UI's correctness button checks GPU identity images, extreme-gaze coverage,
deterministic return after gaze/camera movement, and (when float render targets
are supported) GLSL mapping against the independent JavaScript calculation.
Mapping error is expressed at a 1024-pixel output dimension.

For repeatable automated browser tests, start the local server and a **separate**
Chrome/Edge profile with a local DevTools endpoint on port 19226, then run:

```powershell
node prototype/nonlinear-foveation/gpu-smoke.cjs
```

`CDP_URL` and `LAB_URL` override those endpoints. Node 22+ is needed for this
optional runner's built-in WebSocket. The runner creates/closes only its own tab.
It writes screenshots and JSON under ignored `mod/build/nonlinear/`. Do not point
it at a browser containing sensitive user tabs; use an isolated test profile.

**Measure color error** compares each path with four ordinary scene samples per
output pixel. RMSE is linear RGB in 0..255 units, separated into a small circle
around gaze and the remaining image. Lower error can result from blur; it is not a
perceptual quality verdict. The four-sample reference is finite, not ground truth.

**Measure GPU paths** uses `EXT_disjoint_timer_query_webgl2`, checks disjoint
clocks, rotates path order, warms up twice and batches 32 complete paths per query.
Numbers are per-path time after division by the batch length. Scene evaluation
and reconstruction/composition are included; presentation/readback are excluded.
The UI identifies the renderer and labels software rendering. Browser/GPU clocks,
background load, repeated draws and this tiny scene limit interpretation.

## Observations on 2026-11-01

Chrome/ANGLE reported NVIDIA RTX 5090, D3D11, not a software renderer. Five CPU
test groups passed; six identity image cases were byte-exact; extreme-gaze
opaque-scene checks had no black/uncovered pixels. A gaze/camera excursion then
return reproduced the original image exactly. Nine floating-point GPU probes had
less than **0.0021 output pixel** error at a 1024-pixel dimension, below the 0.1
pixel gate. Browser exception collection was empty.

At 2048x1536, source scale 0.55, strength 4 and focus fraction 0.3:

- Nonlinear: 951,470 scene samples; quad: 950,896; ordinary: 3,145,728.
- Nominal central linear density: 105.0% of ordinary rendering.
- One batched run with material-work setting 48 measured medians of approximately
  **0.057 ms ordinary, 0.029 ms nonlinear including reconstruction, and 0.033 ms
  quad including composition**. These are synthetic fixture timings only.
- The smaller 768x576/light-material case was slower than ordinary rendering
  after reconstruction cost. Fewer scene samples alone do not ensure a win.
- In the 2048x1536 color comparison, quad had lower center RMSE (4.69 vs 5.40),
  while nonlinear had lower peripheral RMSE (5.85 vs 6.99). The paths are not
  equivalent-quality replacements. Inspect fences, silhouette edges and motion.

The original unbatched measurements were too variable to interpret; the runner
now batches paths. Raw local results are reproducible artifacts, not committed
game captures or a general GPU benchmark.

## Remaining engineering work

This ray-scene fixture sidesteps native triangle rasterization. Warping only mesh
vertices does not correctly rasterize curved projected edges or interpolate all
attributes/depth. A subsequent owned-mesh test must quantify that problem and the
cost of subdivision/conservative nonlinear rasterization before any LMU work.

Depth reconstruction, visibility-aware color filtering, shared-edge handling,
motion vectors/history, disocclusions, transparency, UI, post-processing, gaze
latency and headset comfort remain unproven. The fixture reconstructs color only.
Do not infer native LMU compatibility, CPU traversal savings, or an FPS increase.
