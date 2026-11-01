# Conformal and nonlinear foveation research

Research date: 2026-11-01. Source baseline:
`45c2440754206e51a04bda873bb70e969b1795b6` (published 0.1.0 plus documentation).
Branch: `codex/conformal-foveated-research`.

This document investigates allocating more eye-texture samples near gaze and
progressively fewer toward the periphery, then reconstructing an ordinary eye
image. It changes no renderer, launcher, runtime, game installation, or settings.
The proposed full D3D11 experiment below has **not** been implemented.
An initial [standalone WebGL2 sampling fixture](../prototype/nonlinear-foveation/README.md)
now implements the invertible mapping, direct nonuniform ray-scene sampling,
color reconstruction and matched-budget quad-style comparison, with CPU/GPU
correctness checks and scoped synthetic RTX 5090 measurements. An owned-mesh
follow-up compares naive vertex warping, subdivision, and conservative primitive
rectangles with exact fragment intersection/original depth. It demonstrates
coverage correctness for its small fixture and the cost of subdivision/bounds;
it does not solve native LMU mesh integration or reconstructed depth/history.
A moving-gaze test now reports spatial error separately from frame-to-frame
error changes. In the current owned scene, nonlinear sampling has more temporal
variation than the quad comparator despite slightly lower spatial RMS. This
reinforces the need to test stability, not just sample counts and still images.
Concurrent single-pass stereo (SPS) work is separate and is not included here.

## Finding and evidence status

**Feasible in principle; potentially better than quad views; not a projection-matrix
replacement for LMU.** A suitable renderer could use one nonuniform render per eye
instead of a context and focus render per eye. A smoother distribution can reduce
overlapping work and abrupt quality transitions. Whether it wins in LMU depends
on geometry processing, shading, reconstruction, postprocessing, and CPU traversal
costs together. No LMU speedup or RTX 5090 support result is established here.

Evidence labels used below:

- **Verified source:** directly present in this repository at the baseline above.
- **Published:** supported by an identified primary publication or API document.
- **Inference/proposal:** engineering reasoning or a suggested experiment, not a result.
- **Unknown:** requires a capability query, controlled rendering test, or further
  engine investigation. A historical successful test is not a new validation.

Google's conformal renderer closely matches the proposal: transform scene
vertices into nonlinear space, rasterize at reduced resolution, then reconstruct
linear space. Its report describes one rasterization pass, smooth acuity falloff,
eye-tracked placement, and remaining peripheral flicker. This establishes prior
art, not drop-in compatibility with arbitrary game shaders. [Google Research][google]

“Conformal” is the name used by that work; this document also considers general
nonlinear and log-polar mappings. These are a family of sampling schemes, not a
claim that every radial compression mathematically preserves angles. Meng et al.'s
Kernel Foveated Rendering is another relevant example, but its mesh pipeline
begins with Cartesian G-buffer data and moves lighting into a smaller log-polar
buffer. It does not demonstrate direct, unrestricted log-polar triangle
rasterization by ordinary fixed-function hardware. [Original KFR paper, Figure 2
and section 3.1][kfr]

## Why an inverse image warp is not enough

Rendering a normal full-resolution frame and then squeezing/unsqueezing it adds
work; it does not avoid the original shading. The sample distribution must change
before the expensive rendering work being targeted.

A single projective 4x4 matrix maps straight lines to straight lines. General
radial/log-polar foveation does not. Moving only a triangle's vertices leaves the
hardware connecting them with straight edges, while its actual nonlinear image
can have curved boundaries. Coverage, depth and attribute interpolation then no
longer describe the intended projection. A later inverse warp cannot correct
missing geometry samples. UCL's Perceptual Rasterization paper explicitly identifies
this problem for large/near primitives and demonstrates a bounded per-fragment
ray/primitive approach. [Author project][ucl]; [paper, section 2][uclpdf]

Possible solutions include adaptive subdivision, a custom visibility/rasterization
path, or piecewise projections that the hardware can process correctly. Each adds
constraints or cost. Subdivision requires consistent shared edges and clipping;
screen-space texture derivatives and depth still require deliberate treatment.
Ray-based sampling naturally permits arbitrary sample positions, but replacing a
proprietary D3D11 game's visibility and material pipeline is a much larger task
than the proposed research.

## Three approaches and what they actually change

| Approach | Sampling and benefit | Main limitation for this project |
| --- | --- | --- |
| True nonlinear/conformal foveation | Smooth sample allocation around gaze; potentially one scene render per eye | Correct curved coverage/interpolation and all dependent passes need support. Highest integration uncertainty. |
| Hardware piecewise projection: MRS/LMS | Several subprojections approximate the desired density while retaining hardware rasterization | Vendor capabilities, quadrant/viewport boundaries, shader routing, reconstruction, and native pipeline integration still need proof. Not arbitrary smooth warping. |
| Variable Rate Shading (VRS) | Conventional projection and coverage; fewer pixel-shader evaluations in selected areas | Does not eliminate the separate focus traversal or all depth, raster, geometry and bandwidth work. It is a comparator, not a conformal implementation. |

NVIDIA MRS uses multiple viewports with different scales. LMS additionally uses
per-quadrant modified clip-space W (`w' = w + A*x + B*y`), viewport/scissor setup,
and geometry broadcasting. These structured projections are not equivalent to
putting an arbitrary radial function in an unchanged vertex shader. Historical
SMP Assist documentation explains a D3D11 integration mechanism; it does not prove
the old package works on a particular current driver. [NVIDIA SMP Assist][smp]

VRS retains native-resolution raster coverage while varying shader invocation
density. Its gaze-dependent rate image can change each frame. The existing
repository already contains NVAPI VRS machinery: this is not a newly implemented
alternative. Current NVAPI documentation still exposes D3D11 VRS, Modified-W,
multiview and support-query APIs. API presence is not evidence that any particular
device/driver combination supports all of them. [NVIDIA VRS explanation][vrs];
[current NVAPI declarations][nvapi]

**Recommendation (proposal):** begin with an owned, one-eye nonlinear rendering
fixture to settle coverage and reconstruction correctness. Retain hardware LMS
as a capability-gated comparison. Use existing VRS knowledge as a performance
reference, without renaming it conformal rendering. Do not couple this research
to SPS scheduling or claim that either optimization automatically provides the other.

## What the LMU source proves, and what remains unknown

Line references below describe the baseline, not subsequent SPS edits. Links use
repository files; symbols remain useful if line numbers move.

| Area | Verified source evidence | Consequence / remaining question |
| --- | --- | --- |
| Scene scheduling | [`renderHook`](../mod/engine_bridge.cpp), lines 699-866, runs `renderOriginal`, copies outer output, then invokes the additional focus scene at RVA `0x5de200`. | Eliminating the repeat could save CPU and GPU work, but a final-image warp cannot remove it correctly. Exactly which native preparation becomes unnecessary remains unknown. |
| Projection | [`quad_math.hpp`](../mod/quad_math.hpp), `replace_fov`, changes coefficients 0, 5, 8, 9 while retaining native depth mapping; `make_engine_projection` builds a tangent-FOV perspective matrix. `renderHook` rejects alternative/oblique views and an unexpected perspective convention. | Existing overrides implement ordinary cropped perspective, not a nonlinear density function. Keep those guards; a new mapping needs its own validated path. |
| View/history | [`ViewSnapshot`](../prototype/lmu_reconstructed.hpp) contains current/previous transforms and a validity flag. `renderHook`, lines 824-865, maintains separate focus history and restores outer state. | A moving sampling map needs previous/current map metadata in addition to camera matrices. Whether each LMU consumer uses TAA, another temporal filter, or only motion history must be established per pass; field names do not prove the algorithm. |
| Intermediate resources | [`NativeOuterBank`](../mod/native_outer_bank.hpp) has HDR, depth, eye, resolve, depth-resolve, motion and upscale roles; it validates associated depth dimensions/sample counts. Its log explicitly says the internal PPFX pool remains unchanged. | Resizing final eye color alone cannot establish a consistent warped pipeline. The complete HDR/depth/motion/PPFX consumer graph remains unknown. |
| Depth | [`STEREO-SHADER-CONVERSION.md`](../STEREO-SHADER-CONVERSION.md) records a sampled native D32 target with GREATER comparison and writes enabled; `replace_fov` deliberately preserves native depth mapping. | This is historical evidence for one sampled path, not every pass. Nonlinear interpolation and LMS modified W must be checked against the actual depth convention, clears, resolves and readers. |
| Shaders | [`stereo_shader.cpp`](../mod/stereo_shader.cpp), `convert_stereo_shader`, accepts a guarded SM4.0 VS/PS subset and rewrites eye-constant selection/routing, retaining original material math. | It does not implement curved rasterization, a nonlinear interpolator, tessellation, or a renderer-wide postprocess rewrite. Extending it is not the first experiment. |
| Resource comparison | The shader-conversion report records per-eye texture differences, a sampled family using PS slot 20, and incomplete equivalence from first-16-slot signatures. | Gaze-space textures and screen-space readers require complete used-resource/dependency validation, not matching draw hashes alone. |
| Existing VRS | [`nv_vrs.cpp`](../mod/nv_vrs.cpp) queries `bVariablePixelRateShadingSupported`, applies uniform rates, and requires explicit ownership. [`vrs_scope_policy.hpp`](../mod/vrs_scope_policy.hpp) restricts outer world-sort scope; its main-pass comment identifies SSR/offscreen work. `engine_start` currently refuses native-outer plus VRS opt-ins together. | Existing VRS is neither a gaze-map implementation nor safe for blanket activation around the entire frame. Preserve scope/state ownership; do not combine experiments merely because both APIs exist. |
| UI | `uiHook` in [`engine_bridge.cpp`](../mod/engine_bridge.cpp) uses conventional `remap_fov` for eye transforms and a separate outer UI path. | Warping world geometry does not automatically put UI in the correct space. Test cockpit displays, overlays and independent layers separately. |
| XR boundary | [`bridge.hpp`](../mod/bridge.hpp) explicitly distinguishes the final focus image lease from native opaque HDR/depth. [`xr_layer.cpp`](../mod/xr_layer.cpp), lines 711-750, converts validated stereo submission to four ordinary pose/FOV views. | Final swapchain access is too late to replace native opaque rendering. A future two-eye mode needs an explicit submission contract and correct acquire/wait/release handling. |

A local native-boundary audit was also consulted as read-only background. Its
high-level conclusion is consistent with the checked-in handoff: native draw
preparation mutates state, packet cleanup can invalidate borrowed pointers, and
no general prepare-both-eyes/resume-consumers contract has been demonstrated.
Private captures, mapped code, pointers and disassembly are not reproduced here.
This audit does not make an otherwise unknown scheduling operation safe. See the
checked-in [maintainer handoff](../INSTANCED-STEREO-HANDOFF.md) for the portable
boundary and ownership discussion.

## OpenXR and lens correction

**Published:** OpenXR's standard projection layer describes planar images rendered
with a standard perspective projection. Its pose, FOV and rectangular subimage do
not encode an arbitrary radial sample map. **Inference for this mod:** reconstruct
ordinary perspective eye images before normal submission, and leave physical
headset lens correction to the runtime. Do not submit the warped image with an
ordinary FOV and expect the compositor to discover the mapping. [OpenXR 1.1.65,
projection composition][openxr]

A runtime with an explicit custom input contract could combine reconstruction
with lens correction and avoid an extra resampling pass. That is separate runtime
integration, not a capability established by this API layer. Neither this source
review nor the OpenXR core contract establishes such support in VectorXR. A final
two-eye image can also remain subject to runtime timewarp/reprojection; its
semantics must still be conventional after reconstruction.

## Eye tracking and temporal behavior

**Inference:** the nonlinear map can follow per-eye gaze. Rendering, depth/motion
generation and reconstruction must use the same frame's map. Retain previous-map
parameters for any temporal reuse. A late change to the inverse map cannot create
fine detail that was rendered sparsely before a saccade.

Guard-region size must cover tracking uncertainty and relevant latency. Specify
behavior for invalid/stale gaze, blinking, edge gaze and mode transitions; avoid
averaging gaze so heavily that the detailed region visibly trails fixation.
Gaze movement alone changes sampling, not geometric visibility at an unchanged
viewpoint. Disocclusions arise from head/scene motion and temporal reuse; new
visibility and newly detailed regions must reject unsuitable history.

Peripheral temporal aliasing and contrast loss remain visible even where fine
detail is unresolved. NVIDIA's research uses filtering, contrast handling and
saccade-aware temporal antialiasing; smooth spatial falloff alone does not solve
these issues. Its published results do not determine suitable LMU thresholds.
[NVIDIA Research][temporal]

## First bounded offline experiment (proposal, not implemented)

Build a standalone D3D11 executable using owned geometry and shaders. No LMU
process, hooks, game shader capture, OpenXR session or provider configuration is
needed. Start with one eye and no temporal accumulation. Keep any generated
images, timing logs and binaries under ignored `mod/build/conformal/`.

1. **Define and validate the mapping.** Use one invertible monotonic radial map
   with an identity setting, explicit gaze point and an inverse. Account for the
   full asymmetric rectangular FOV, including corners and off-center gaze; use
   conservative bounds/padding, not accidental edge clipping. Check finite values,
   positive local area and forward/inverse agreement throughout the domain.
2. **Build the correctness reference.** Use an analytic ray/triangle reference
   at regular output sample positions, plus a supersampled image for filtering
   comparisons. Include large triangles crossing gaze, near-plane clipping,
   overlapping sloped surfaces, thin lines/fences, checker textures and specular
   detail. These are synthetic fixtures, not copied LMU assets.
3. **Test actual nonlinear rendering.** Use conservative warped primitive bounds
   and inverse-mapped per-fragment intersection/attribute evaluation as the first
   correctness path. Include simple vertex-only warping as an explicitly expected
   failure comparison. This isolates the central hypothesis without first
   rewriting a proprietary shader corpus. Record overdraw and shader cost.
4. **Reconstruct color and depth deliberately.** Generate original-eye depth at
   the intersection, including a D32/GREATER case. Treat depth edges separately
   from filtered color; do not bilinearly blend unrelated surfaces and call it
   valid depth. Compare identity and nonidentity output, then add a scripted
   moving gaze/head path without temporal filtering before considering history.
5. **Only after correctness, compare cost.** Add a synthetic two-resolution
   context/focus reference with the same central sample density and total pixel
   budget. Compare complete render plus reconstruction, CPU submission, GPU time,
   shader invocations and memory. Vary both geometry density and shader expense.
   Optional LMS comparison follows capability checks; it is not a substitute for
   evaluating the requested nonlinear method.

This fixture can reject a bad approach or identify costs. It cannot demonstrate
LMU compatibility, eye-tracker latency, headset comfort, or a game-frame speedup.

## Capability probe plan

No new hardware probe was run for this documentation change. The developer's
reported RTX 5090 is the target, not a support guarantee. Use a standalone device
and report the actual adapter/driver; never take ownership of a live game's NVAPI
state for discovery.

| Probe | Concrete operation and result to retain locally | Meaning / next gate |
| --- | --- | --- |
| D3D11 baseline | Create an explicit selected-adapter device; record adapter/vendor/device, driver version, feature level, format support, MSAA quality and optional debug-layer availability. | Confirms the fixture's real device and target formats. WARP is a correctness fallback, not a hardware performance substitute. |
| VRS | Reuse `nv_vrs_detail::Api::supported`; run the owned [`nv_vrs_hardware_test.cpp`](../mod/tests/nv_vrs_hardware_test.cpp) fixture when separately compiled. It checks shader-invocation reduction, restoration, ownership failures and context-state behavior at 1x/2x MSAA. | Separates API return success from observable shading behavior. Existing helper tests do not prove arbitrary LMU pass eligibility. |
| Modified W | Resolve `NvAPI_D3D_QueryModifiedWSupport` and query a versioned `NV_QUERY_MODIFIED_W_SUPPORT_PARAMS`; record status and `bModifiedWSupported`. | Necessary for the LMS branch. If unavailable, record unsupported and keep the generic nonlinear fixture. |
| Geometry broadcasting | Query `NvAPI_D3D_QueryMultiViewSupport` and, if the chosen route uses it, `NvAPI_D3D_QuerySinglePassStereoSupport`; inspect required viewport/FastGS mechanisms for that route. | Positive multiview/SPS bits alone do not establish an LMS pipeline. Do not confuse four views with four independently correct nonlinear projections. |
| Behavioral LMS test | On the private context, use owned triangles spanning quadrants, distinct viewport colors and known depth; enable the selected Modified-W route, reconstruct, compare, disable, and verify the baseline again. | Required even after positive capability bits. Test clipping, coverage and depth under the actual target/MSAA combination. |

Use the pinned headers and function table already documented in
[`mod/vendor/nvapi/provenance.json`](../mod/vendor/nvapi/provenance.json), then compare
against the current [official API reference][nvapi]. The Modified-W and multiview
setters explicitly warn that `NVAPI_OK` alone does not demonstrate hardware support;
query first and verify rendered behavior. The existing VRS helper has explicit
exclusive-state requirements; do not assume ordinary D3D state capture preserves
every vendor extension. No branch of the probe plan changes driver profiles.

## Acceptance and stop gates

The following are proposed engineering gates, not previously achieved results.

- **Mapping:** identity roundtrip works; no NaN, foldover, missing corner or
  unexplained out-of-domain sampling at tested gaze/FOV extremes. Use a proposed
  maximum roundtrip error of 0.1 output pixel for the fixture's supported domain.
- **Geometry/depth:** no interior holes or wrong surface ordering against analytic
  reference samples away from edges. Central contours agree within a proposed
  0.5 output pixel; report filtering/undersampling error separately in the periphery.
  Include large and clipped primitives; a dense-mesh-only success is insufficient.
- **Temporal:** scripted gaze sweeps and jumps do not geometrically swim after
  inverse mapping. Inspect thin-detail flicker and transition frames. If temporal
  history is later added, test newly visible and newly focused regions explicitly.
- **Performance:** measure the entire pipeline, including reconstruction, bounds,
  copies, extra geometry and state changes. Proceed toward LMU only if repeated
  matched synthetic runs show a net benefit in relevant workloads at the accepted
  quality. A lower shaded-pixel count is not sufficient. Record both wins and losses;
  there is no fixed LMU millisecond/FPS forecast from this research.
- **Integration:** before any live replacement, identify each affected native pass,
  its resources and consumers, depth conventions, shader variants and history
  ownership. Establish state restoration and fallback before visible output changes.
  A small opaque fixture is not permission to warp SSR, transparency, UI or the
  full frame indiscriminately.

Stop or narrow the candidate if correctness needs an unbounded geometry increase,
if reconstruction consumes the savings, if unsupported stages cannot be isolated,
or if the resource/temporal contract remains unproven. An LMS capability failure
stops that hardware route, not the general research question. Keep any later
renderer experiment separate from the released quad-view path and the SPS branch.

## Primary references

- [Google Research: Introducing a New Foveation Pipeline for Virtual/Mixed Reality][google]
  (2017): closest explanation of conformal rendering and its flicker limitation.
- [UCL: Perceptual Rasterization for Head-mounted Display Image Synthesis][ucl]
  (SIGGRAPH 2019), with [accessible author manuscript][uclpdf]: coverage problem
  and a nonlinear rasterization solution.
- [NVIDIA: SMP Assist API for VR Programming][smp] (2018): MRS/LMS mechanism.
- [NVIDIA: Turing Variable Rate Shading in VRWorks][vrs] (2018), paired with
  [current NVAPI reference][nvapi]: VRS distinction and current API declarations.
- [Khronos: OpenXR 1.1.65 projection composition][openxr]: standard submission contract.
- [Meng et al.: Kernel Foveated Rendering][kfr] (2018): log-polar deferred shading.
- [NVIDIA Research: Towards Foveated Rendering for Gaze-Tracked Virtual Reality][temporal]
  (2016): temporal and perceptual considerations.

[google]: https://research.google/blog/introducing-a-new-foveation-pipeline-for-virtualmixed-reality/
[ucl]: https://www.homepages.ucl.ac.uk/~ucactri/PerceptualRasterization/
[uclpdf]: https://arxiv.org/pdf/1806.05385
[kfr]: https://www.cs.umd.edu/~varshney/papers/Meng_KernelFoveatedRendering_PACM_2018.pdf
[smp]: https://developer.nvidia.com/blog/nvidia-smp-assist-api-vr-programming/
[vrs]: https://developer.nvidia.com/blog/turing-variable-rate-shading-vrworks/
[nvapi]: https://docs.nvidia.com/nvapi/nvapi_8h.html
[openxr]: https://registry.khronos.org/OpenXR/specs/1.1-khr/html/xrspec.html#XrCompositionLayerProjection
[temporal]: https://research.nvidia.com/labs/rtr/publication/patney2016towards/
