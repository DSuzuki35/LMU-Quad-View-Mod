# Standalone nonlinear foveation laboratory

The user explicitly requests autonomous work after the pixel estimator, inline
without a side agent. SPS retains the 0.2.0 target. This experiment stays on the
existing separate research branch and does not alter LMU or published binaries.

## First increment

Build an interactive WebGL2 fixture with owned procedural geometry/materials.
Render scene samples directly into a smaller, nonuniform image, then reconstruct
an ordinary perspective image. A ray-scene fragment shader is the first
correctness path: it avoids pretending that vertex-only warping preserves
triangle coverage. It is not a replacement for LMU's mesh/material pipeline.

Alternatives considered: direct D3D11 conservative nonlinear rasterization is a
later integration-oriented fixture; a finished-image warp cannot demonstrate
saved scene evaluations. A browser fixture makes sampling, moving gaze and
quality tradeoffs immediately inspectable with no external libraries.

Use a rectangle-preserving radial asinh map around gaze. Radius is normalized by
the ray's distance to the rectangular eye-image boundary. Forward map expands
central space; analytic sinh inverse selects scene rays. Identity strength zero
is exact, the full image boundary remains covered, and tested gaze remains inside
the image. This is nonlinear radial foveation, not a claim of mathematically
conformal angle preservation. Derivative changes along the rectangle's diagonal
sectors are a known limitation to inspect.

Compare full-resolution rendering, nonlinear source/reconstruction, and a
two-resolution context/focus reference. Match central linear sample density and
scene-sample pixel budget where feasible; show rounding mismatch and impossible
settings, not false equivalence. Count complete reconstruction/composition work
separately. Add owned high-frequency road/checker/fence/near geometry, moving
camera and gaze. Include source-buffer view and current sample density.

Tests: identity, inverse roundtrip, corners/edges/off-center gaze, positive area,
finite-domain guards, central density and quad budget math. Browser GPU checks
must identify renderer, check GL errors and compare identity images by readback.
Quality statistics compare reconstructed color with a supersampled reference and
separate center/periphery; no claim of correct reconstructed depth or temporal
history. GPU timings are optional disjoint queries, clearly labeled with renderer
and measured whole render-plus-reconstruction passes. Never label software-GPU
timings as hardware performance. No LMU/FPS/eye-tracker benefit is established.

Deliver source, tests, reproducible launch instructions, inspected images and a
short findings document. Generated images/results stay in ignored build output.
