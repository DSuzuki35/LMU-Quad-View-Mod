# Owned mesh follow-up

The sampling lab passed its first gates. Continue the user's autonomous inline
research with a separate mesh page on the same research branch.

Compare hardware rendering of a small owned mesh with ordinary projection,
direct analytic sampling of its original projected triangles at inverse-mapped
rays, and vertex-warped hardware triangles at uniform subdivision levels 0..7.
Use varying clip W, overlapping depth, smooth UV colors and a near-plane crossing.
Clip in homogeneous space before the nonlinear map. Count surface-ID errors in
the warped source grid against the analytic reference, excluding a one-pixel
boundary neighborhood; report color interpolation error separately. Use full-size
source buffers to isolate geometry errors from reduced-resolution sampling.

This answers whether vertex rewriting alone is correct and quantifies one simple
repair's triangle growth. It does not implement adaptive shared-edge refinement
or correct reconstructed depth/history for LMU.
Uniform subdivision is intentionally bounded and documented as a diagnostic.

Follow-up within this owned scene: draw a conservative rectangle for each original
clipped primitive. In the fragment shader, inverse-map the source sample, test
original triangle coverage, interpolate attributes with original reciprocal W,
and write original depth. Compare against the independent full-screen analytic
visibility path. Derive conservative bounds from intervals of the monotone radial
scale, not just warped vertices. Add an optional tighter bound by recursively
bounding original edge segments; coordinate extrema of this invertible map's
polygon image occur on its boundary. Keep interval bounds at a fixed recursion
cap, so failure to reach the target tightness cannot omit coverage. Measure CPU
bound construction separately from GPU drawing, and report rectangle area.

Implementation sequence: establish GPU identity and subdivision failure/convergence;
test conservative containment in CPU math; verify bounded fragment coverage/depth;
test tighter bounds against dense interior samples and coarse bounds; compare
GPU results at extreme gaze/strength; measure costs; document limitations.

Test clipping, subdivision counts/attributes, GPU identity, large naive error and
improvement with subdivision. Inspect images and retain local metrics. Add a link
from the first viewer, document findings, commit/push after checks. No game hooks.
