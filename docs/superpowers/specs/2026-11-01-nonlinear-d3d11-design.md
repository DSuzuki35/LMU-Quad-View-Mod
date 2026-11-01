# Native D3D11 bounded-primitive probe

The browser experiment establishes owned-scene correctness but CPU per-primitive
bounds do not scale. Implement a separate offscreen D3D11 executable, with no
game/runtime hooks. Move the coarse conservative interval bound to a geometry
shader. Each already-clipped triangle emits a four-vertex rectangle strip;
the pixel shader inverse-maps sample coordinates, tests original coverage,
interpolates UV with original reciprocal W and writes original D3D depth.

Use the same two owned triangles as the browser experiment, with CPU homogeneous
clipping once during fixture creation. Compare hardware ordinary rendering at
zero warp and a full-screen analytic reference at nonzero warp. Test depth and
surface/color correctness for centered/corner gaze and strength 0/0.1/4/12. Verify
that the naive vertex-warp path still produces substantial coverage errors.
Use explicit HRESULT checks (not disabled release asserts) and fail the process
on correctness errors. Build with strict compiler warnings. Run WARP separately
as a correctness backend and hardware on the actual reported adapter.

Record pipeline statistics and disjoint timestamp batches. Timings cover source
rasterization only, without reconstruction: this probe tests the bounded stage,
not complete foveation performance. Report GPU shader invocations rather than
equating rectangle area with actual shading. Keep all artifacts in ignored build
output, inspect exported color images, document gates/limitations, commit/push.

This does not implement general GPU near-plane clipping, arbitrary LMU materials,
shader interception, MSAA, derivatives, transparency, reconstructed depth/history
or a standalone replacement for the game's renderer. Native API support and
fixture correctness do not establish engine compatibility or a performance win.

Follow-up: instance the clipped owned scene into a grid of tiles, keeping total
image coverage broadly comparable while varying primitive size/count. Check a
4x4 tiled result against ordinary identity and the independent CPU reference.
On hardware only, measure 1/4/16/64 tiles per axis with light and synthetic costly
materials. Record GS/PS invocation counts and source-only GPU time; bounds remain
on the GPU and all instances are one draw. This intentionally excludes engine
traversal and per-object draw submission. Do not use WARP scaling timings as GPU
evidence. The full-source benchmark remains a cost test, not a foveation speedup.

Next increment: add native color reconstruction from a smaller logical source
viewport into a separate full-sized target. Keep source/depth allocations full
sized for this prototype, explicitly avoiding a memory-saving claim. Verify
zero-warp/full-size reconstruction is byte-exact, export nonidentity outputs, and
measure complete ordinary vs bounded+reconstruction paths at light/heavier shader
costs and geometry counts. Add a hardware-only larger-output preset. Keep the
source-only results separate; no reconstructed depth or matched-quality claim.

Add an owned native quad comparator: a conventional low-resolution outer pass,
a cropped conventional focus pass, and hard-boundary color composition. Match
the nonlinear 55% sample budget and nominal center density with fraction 0.3,
report actual integer dimensions, and include composition in timing. Prepare
constant buffers outside timed draws. Verify full-focus identity, cropped source
coverage/UV/depth against CPU samples, and composition against CPU bilinear
sampling. The comparator has no guard-band blending or engine traversal cost.

Isolated optimization variant: precompute barycentric, depth and perspective
attribute planes once in the geometry shader. Keep the original bounded path as
a comparison. The pixel shader then evaluates dot products and one reciprocal
instead of recalculating triangle divisions. Gate coverage/color/depth against
the independent CPU reference, including tiled geometry, before comparing full
path timings. Preserve the same bounds, source dimensions and reconstruction.

Outcome: global-coordinate planes failed dense depth precision; anchor all planes
at vertex A instead. Keep the unchanged 1e-5 depth and one-code interior color
gates. Add 12,288-triangle direct-path comparisons, reduced-source CPU checks and
CPU reconstruction checks. WARP and both hardware sizes pass. Medium-shading
cases improve modestly, but matched quad remains faster in all native fixtures.
Retain both implementations as research controls, with recorded source hashes
and timing ranges. This does not meet the gate for LMU integration.
