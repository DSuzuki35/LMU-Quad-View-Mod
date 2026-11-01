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
node --test prototype/nonlinear-foveation/mapping.test.cjs prototype/nonlinear-foveation/mesh-data.test.cjs
node --test prototype/nonlinear-foveation/temporal.test.cjs
```

The UI's correctness button checks GPU identity images, extreme-gaze coverage,
deterministic return after gaze/camera movement, and (when float render targets
are supported) GLSL mapping against the independent JavaScript calculation.
Mapping error is expressed at a 1024-pixel output dimension.

For repeatable automated browser tests, start the local server and a **separate**
Chrome/Edge profile with a local DevTools endpoint on port 19226, then run:

```powershell
node prototype/nonlinear-foveation/gpu-smoke.cjs
node prototype/nonlinear-foveation/mesh-smoke.cjs
node prototype/nonlinear-foveation/temporal-smoke.cjs
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

Chrome/ANGLE reported NVIDIA RTX 5090, D3D11, not a software renderer. The first five CPU
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

The ray-scene fixture sidesteps native triangle rasterization. The separate mesh
experiment below quantifies that problem; its small owned material/geometry
pipeline is not a replacement for LMU's native one.

Depth reconstruction, visibility-aware color filtering, shared-edge handling,
motion vectors/history, disocclusions, transparency, UI, post-processing, gaze
latency and headset comfort remain unproven. The fixture reconstructs color only.
Do not infer native LMU compatibility, CPU traversal savings, or an FPS increase.

## Owned mesh experiment

Open <http://127.0.0.1:18767/mesh.html>. Two overlapping triangles have different
clip W values, smoothly varying UV colors, depth crossings, and near/side-plane
clipping. Ordinary homogeneous clipping produces three triangles. Each source
buffer is 640x480: this experiment deliberately keeps full resolution to isolate
coverage, interpolation and depth from resolution savings.

The analytic reference inverse-maps each source sample and intersects the original
projected triangles. The ordinary hardware path agrees at zero warp. The candidate
can either warp vertices, subdivide before warping, or draw one conservative
rectangle per original clipped primitive. That last path performs original
triangle intersection/perspective interpolation per fragment, discards outside
coverage, and writes original depth for the hardware depth test.

Metrics compare identical source sample positions **before** reconstruction.
Alpha stores a primitive ID. Interior surface errors exclude one-pixel reference
boundaries and the outermost image border. Color RMSE includes only matching,
non-background interior surfaces. The magenta heatmap exposes surface errors;
green exposes attribute errors. Neither metric validates reconstructed depth.

At strength 4 and centered gaze on the same RTX 5090:

| Candidate | Raster triangles | Wrong interior source pixels | Matched color RMSE /255 |
| --- | ---: | ---: | ---: |
| Warp original vertices | 3 | 16.33% | 10.26 |
| Subdivision level 3 | 192 | 0.819% | 1.25 |
| Subdivision level 6 | 12,288 | 0.00233% | 0.105 |
| Conservative rectangles | 6 | 0% | 0.003 or less |

The rectangle path also passed tests with gaze near image corners and strengths
0.1/4/12. Its bounds enclose intervals of the radial scale `f(t)/t`, not merely
the warped vertices. A one-source-pixel guard covers float/raster rounding.
Optional edge refinement shrinks the rectangle: it continuously bounds boundary
segments, retaining conservative intervals at a fixed recursion cap. It never
uses sampled points alone to decide coverage. The desired tightness is a quarter
source pixel; hitting the cap leaves a looser, still conservative bound.

**Measure mesh paths** separates JavaScript CPU bound preparation from GPU drawing
and reconstruction. It caches bounds for the GPU measurement, rotates order,
uses two warmups and nine batches of 16 paths, and rejects disjoint clocks.
Presentation, readback, CPU bounds and initial mesh upload are excluded from GPU
times. The UI reports both costs. Zero CPU readings can mean browser timer
quantization, not free computation.

The initial refinement algorithm reduced centered rectangle area from 2.96x the
source to 2.19x, but needed about 17.5 ms CPU for just three triangles at depth 10.
A depth-3 cap used about 0.2 ms and left 2.50x area. GPU path medians were about
0.001 ms ordinary, 0.007 ms vertex-warp/subdivision-3, and 0.010–0.011 ms for
subdivision-6/rectangles including reconstruction. These tiny light-material,
full-source runs demonstrate overhead, **not a foveation performance gain**.
The unrestricted CPU refinement is unsuitable as a per-triangle game algorithm.

A follow-up prunes segments whose conservative interval cannot enlarge the
polygon's **global** bounds beyond tolerance. The centered depth-10 workload fell
from 6,086 terminal segments to 215, keeping the 2.19x rectangle area. Batched CPU
measurements (16 calculations per sample, ten samples after two warmups) gave
approximately 0.44 ms at depth 10, 0.14 ms at depth 3, and 0.01 ms for coarse bounds.
These JavaScript numbers cover only three primitives and include allocations;
they do not establish a scalable native implementation. Four segments still hit
the depth cap, retaining conservative coverage. Tests cover 40 seeded random and
degenerate CPU triangles plus coarse/depth-3/depth-10 GPU cases across nine
gaze/strength combinations. Pruning changes the work, not the coverage contract.

There is no general-purpose shader rewriting here. LMU integration would still
need primitive extraction, per-material interpolation and derivatives, clipping,
transparency/order, multisampling, motion/depth history, UI/postprocessing, and
an efficient conservative-bound strategy. This prototype supports further owned
renderer research; it does not justify enabling a nonlinear LMU mode.

## Moving-gaze stability

The sampling viewer's **Measure gaze stability** and **Gaze + camera stability**
buttons follow 24 deterministic positions around a closed gaze path. Source
budgets remain fixed. Every position gets a fresh four-sample ordinary reference.
With error `e_t = output_t - reference_t`, the report contains spatial RMS error
and RMS of `e_t - e_(t-1)` in linear RGB (0..255). Heatmaps use one common scale
across ordinary/nonlinear/quad panes. The first and last poses match exactly;
the runner verifies that the returned images are byte-identical.

For a stationary scene, ordinary output has zero change and the heatmap isolates
changes caused by moving the foveation. With camera motion, subtracting the
reference's change removes its image difference, but this is still an image-space
error diagnostic: it does not track world surfaces, model retinal motion, or
represent real eye-tracker noise/latency. The trajectory has no claimed physical
frame rate. No temporal accumulation or antialiasing history is applied.

At 768x576, source scale 0.55, strength 4, focus fraction 0.3, material work 8,
and checker-footprint filtering enabled:

| Trajectory | Ordinary error-change RMS | Nonlinear | Quad-style |
| --- | ---: | ---: | ---: |
| Gaze only | 0.00 | 9.41 | 4.25 |
| Gaze + camera | 9.06 | 11.67 | 10.95 |

In the gaze-only run, spatial RMS was 9.17 nonlinear and 9.80 quad, despite the
opposite ordering in temporal stability. This shows why a lower single-frame
error is insufficient. Moving the nonlinear map moves sample placement across
the entire image; the quad comparator's outer grid stays fixed. That is a plausible
contributor to the measured difference, not a general quality ranking. Fine
checker patterns, fences and silhouette edges dominate the visible heatmaps.

The identity control (zero warp, full-sized source) matched ordinary temporal
behavior with stationary and moving cameras. Four CPU tests validate the metric,
including known alternating errors, static bias, and cancellation of equal
output/reference movement. GPU checks had no browser exceptions and exact
trajectory returns. A temporal filter might reduce this noise, but would need
gaze-aware reprojection, depth/disocclusion rejection and tests for blur/ghosting
before any claim of improved quality.
