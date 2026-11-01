# Standalone nonlinear foveation lab

This lab implements sampling and owned-rasterizer slices of the
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
`q = s*k/asinh(k)`. A quad focus covering fraction `f` of each axis with density `q` consumes
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

## Native D3D11 source-rasterization probe

`native.cpp` / `native.hlsl` move coarse primitive bounds to a geometry shader,
avoiding the JavaScript per-primitive calculation. This is a separate offscreen
executable. It uses no game assets, OpenXR session, injection or NVAPI state.
Build with a Windows x64 MinGW-w64 C++17 toolchain (tested with LLVM-MinGW):

```powershell
./prototype/nonlinear-foveation/build-native.ps1
# Or: ./prototype/nonlinear-foveation/build-native.ps1 -Compiler C:/path/to/g++.exe
# Add -Large to also run the 2048x1536 hardware preset.
```

The script runs WARP correctness first, then hardware on D3D11's default adapter.
It records the actual adapter, JSON results and BMP source images in ignored
`mod/build/nonlinear/native/`. It does not fall back silently from hardware to WARP.
The compiler/runtime DLLs, if needed by a different toolchain, must be available
on PATH. No executable is installed or copied into LMU.

Geometry is clipped once on the CPU when creating this fixed scene. A geometry
shader computes conservative interval bounds and emits a rectangle strip; the
pixel shader reconstructs original sample coverage, attributes and D32/LESS
depth. An independent double-precision CPU reference checks the HLSL path, in
addition to the full-screen GPU reference and ordinary hardware identity case.
The expected-failure vertex-only path has 49,032 wrong interior pixels at
strength 4/center gaze, versus zero for the bounded path in the tested cases.

On 2026-11-01, all 12 gaze/strength combinations passed on Microsoft WARP and
NVIDIA GeForce RTX 5090. Bounded/reference GPU source colors and depths matched
in the hardware run. Against the independent CPU reference, hardware had zero
wrong interior surfaces, a maximum one-code-value RGB difference, and maximum
depth error below 0.0000018. The CPU comparison permits one UNORM8 code value,
not exact byte equality: native floating arithmetic and quantization can differ
at rounding boundaries. D3D11 explicitly permits conversion tolerance.
[D3D11 specification, float-to-UNORM](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.2.3.6%20FLOAT%20-%3E%20UNORM)

The hardware pipeline reported 3 GS invocations, 6 emitted triangles and 909,120
pixel-shader invocations for bounded rendering, versus 214,164 pixel-shader
invocations for ordinary rendering. Invocations include work that rejects a
fragment; they are not the number of expensive material evaluations or visible
pixels. Source-only medians were roughly 0.0012 ms ordinary and 0.0053 ms bounded,
using two warmups and nine rotated batches of 32. **No reconstruction is included
in these native timings.** They demonstrate source-stage overhead in a tiny
light-material scene, not a complete rendering speedup.

The native probe does not implement arbitrary GPU clipping, game materials,
MSAA, transparency, texture derivatives, output depth reconstruction or history.
The [geometry shader API](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-geometry-shader)
supports the required mechanism; the tested owned pipeline does not establish
that LMU's existing shader interfaces can be converted safely.

The native probe also instances the owned scene into 1/4/16/64 tiles per axis
(3/48/768/12,288 input triangles). Three 4x4 cases pass the independent CPU
coverage/color/depth check, and zero warp also matches ordinary hardware
rendering. A controlled material loop adds 32 sine/cosine terms per shaded sample.
Each benchmark uses one instanced draw, so this excludes per-object engine work.

Representative RTX 5090 **source-only** medians, in milliseconds:

| Input triangles | Ordinary, light | Bounded, light | Ordinary, work 32 | Bounded, work 32 |
| --- | ---: | ---: | ---: | ---: |
| 3 | 0.0013 | 0.0053 | 0.0023 | 0.0084 |
| 48 | 0.0013 | 0.0049 | 0.0028 | 0.0085 |
| 768 | 0.0022 | 0.0066 | 0.0032 | 0.0119 |
| 12,288 | 0.0072 | 0.0145 | 0.0074 | 0.0247 |

The full-size source still has no resolution saving. At 12,288 triangles,
bounded pixel-shader invocations reached 2,014,929 vs 217,088 ordinary. Small
primitive guards and raster scheduling contribute additional work; these counts
alone do not isolate each cause. The scaling test confirms the GPU mechanism
works across many primitives, while showing why it must be measured as a complete
reduced-resolution pipeline before it can be called an optimization.

### Complete native color path

The probe now also reconstructs color into a separate full-sized output from a
55%-width/height logical source viewport. It clamps filtering to the logical
source's texel centers, including off-center gaze. Source and depth allocations
remain full-sized in this first implementation; their clears are included in
the measured path. **No memory saving or reconstructed depth is claimed.**

Zero-warp/full-size reconstruction is byte-exact on WARP and hardware. At 55%,
three gaze positions pass an independent double-precision CPU forward-map and
bilinear-filter comparison with at most two UNORM8 code values of difference
(sampler fractional precision and color quantization are included). These checks
also pass in the 2048x1536 hardware preset. Returned ordinary geometry is visually
aligned in the inspected exports; edge filtering and reduced peripheral samples
still change quality.

The JSON keeps `pipeline` / `scaling` source-only timings separate from
`completePaths`, which includes source rendering and color reconstruction.
At 2048x1536, strength 4, centered gaze, 55% source uses 951,470 logical source
pixels vs 3,145,728 full output pixels, with nominal center linear density 105%.
Representative RTX 5090 medians (ms):

| Input triangles | Material work | Ordinary | Reduced source + reconstruction |
| --- | ---: | ---: | ---: |
| 3 | 0 | 0.0060 | 0.0212 |
| 3 | 32 | 0.0203 | 0.0285 |
| 3 | 128 | 0.0769 | 0.0645 |
| 768 | 128 | 0.0860 | 0.0809 |
| 12,288 | 128 | 0.1012 | 0.1495 |

Only the expensive, less geometry-heavy cases won in this run. Work 128 is an
artificial ALU loop, not a characterization of LMU's materials. Sampling quality
is different, and sub-millisecond
fixture timings are sensitive to clocks/load. This is evidence to continue
targeted research, not evidence of an LMU FPS gain. Correct reconstructed depth,
temporal stability and a realistic material/pass model remain required gates.

### Matched native quad comparison

The native comparator now renders ordinary outer and cropped focus passes, then
composes their colors with a hard boundary. Both paths use the same owned scene,
material and output size. Quad matches the nonlinear nominal center density and
scene-sample budget at fraction 0.3. At 2048x1536 its outer image is 923x692 and
focus is 645x484: **950,896 samples vs 951,470 nonlinear**. This is a sampling
budget match, not an equal-perceptual-quality guarantee. There is no blending or
extra focus guard band. Source allocations remain full-sized for both methods.

Constant buffers are prepared before timed batches. Quad timing includes two
instanced scene draws plus composition; nonlinear includes one bounded scene
draw plus reconstruction. Neither simulates LMU's CPU traversal or draw-call
count. Full-focus identity is byte-exact on both backends. Three gaze positions
pass CPU checks of outer/focus source coverage, attributes and original depth,
plus independent CPU bilinear composition (at most two code values allowed).
Native color-error reports use a single-sample CPU reference; the browser lab's
four-sample quality reference is a separate measurement.

Representative **complete path** RTX 5090 medians at 2048x1536, in milliseconds:

| Input triangles | Material work | Ordinary | Nonlinear 55% | Matched quad |
| --- | ---: | ---: | ---: | ---: |
| 3 | 0 | 0.0060 | 0.0213 | 0.0139 |
| 3 | 32 | 0.0203 | 0.0286 | 0.0208 |
| 3 | 128 | 0.0777 | 0.0654 | 0.0497 |
| 768 | 128 | 0.0860 | 0.0811 | 0.0408 |
| 12,288 | 128 | 0.1014 | 0.1484 | 0.0508 |

**The current bounded nonlinear prototype lost to matched quad in every native
benchmark case tested.** Its occasional gain over ordinary rendering is not a
reason to replace quad views. Bounds/intersection overhead, lost ordinary
rasterization efficiencies and geometry amplification need more work; the
moving-gaze test also exposes temporal variation. These are scoped synthetic
results, not a verdict on every nonlinear renderer.

The [recorded native summary](results/2026-11-01-native-summary.json) retains
adapter/preset details, source-file hashes, correctness maxima and timing ranges.
Full transient images/logs remain under ignored build output.

For the product, keep quad views and the separate SPS 0.2.0 work as the main path.
For research, the next useful gates are tighter GPU bounds / cheaper primitive
evaluation, depth-aware reconstruction and temporal reprojection, followed by a
repeat of matched-budget quality and complete-path measurements. Do not insert
this fixture into LMU until it has a demonstrated advantage and a safe material,
pass-state and history integration plan.
