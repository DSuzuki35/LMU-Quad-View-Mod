# SPS maintainer handoff — 6 November 2026

## Objective and current state

The requested deliverable is production SPS: prepare compatible scene work once,
render both eyes through a complete SPS pass, and suppress the corresponding
duplicated native eye work. Reuse the components already implemented here.
Do not treat another one-draw demonstration as delivery of that objective.

**Production integration remains unfinished. Native rendering is still active
in LMU, and no SPS CPU frametime gain has been verified in the game.**

The latest continuation implements a separate, owned four-view renderer and an
exclusive XR submission route. Continue that path, described below and in
[SPS-PRODUCTION-PATH.md](SPS-PRODUCTION-PATH.md). Do not resume native-right
prediction/comparison troubleshooting or treat another one-pair run as delivery.

The existing `Start-SPS-Rendering.cmd` still selects the older one-pair native
adapter. It does not activate the new owned renderer. The ordinary launcher
keeps that prototype disabled.

## Latest owned SPS implementation

The following source is implemented and passes the dedicated SPS-only suite:

| Component | Source |
| --- | --- |
| Authoritative quad camera snapshot and history | `mod/sps_frame_cameras.hpp` |
| Owned, cleared paired color/depth targets without native seeds | `mod/sps_frame_targets.hpp` |
| Persistent assets/instances, one union traversal, four independent visibility/order lists | `mod/sps_scene.hpp`, `mod/sps_draw_list.hpp` |
| Complete two-pair preflight/execution and four-view output ownership | `mod/sps_renderer.hpp` |
| Direct game mesh/geometry/shader-handle and bone-palette inputs | `mod/sps_game_mesh_inputs.hpp` |
| Owned game constant-buffer ABI and transform patches | `mod/sps_game_parameters.hpp` |
| Shared owned ambient/diffuse/directional/specular calculations and raw input readers | `mod/sps_game_lighting.hpp` |
| Creation-metadata shader conversion, versioned resource snapshots and owned asset import | `mod/sps_game_asset.hpp`, `mod/stereo_native_registry.hpp` |
| Four owned XR swapchains, complete-quad publication and acquisition/release recovery | `mod/sps_xr_output.hpp` |
| Exclusive production output bridge and EndFrame route | `mod/bridge.hpp`, `mod/xr_layer.cpp` |
| Changed-state/range submission, persistent recording contexts and cached binding validation | `mod/stereo_command_state.hpp`, `mod/stereo_batch.hpp`, `mod/stereo_draw.hpp` |

The owned path has no native render/preparation/fallback callback. It shares
material and animation data across views, packs only the view-dependent CBs,
supports indexed lists/strips and ordered paired or single-eye actions, and
reuses unchanged owned resource versions. Its exclusive XR route ignores all
application/native projection layers; an incomplete SPS quartet submits no
scene. Teardown and EndFrame can be serialized from another application thread.

**These are owned fixtures and real D3D/XR-adapter tests, not an LMU runtime
success.** The engine does not yet call `xr_prepare_sps_output` or feed a complete
game scene to `SpsRenderer`.

Still required for the user's production goal:

1. Complete the live unfiltered game scene/material producer: texture and sampler
   resolution, render-state/pass inputs, mutable geometry uploads, skeletal
   finalization and other source effects. The direct mesh reader explicitly
   refuses unsupported pending uploads/callbacks/unfinalized poses.
2. Supply shared world/tracking placement and the complete owned game frame graph,
   including required shadows, postprocessing, depth consumers and UI. Current
   owned scene execution covers one color/depth target per pair; XR output takes
   resolved single-sample color.
3. Connect that producer and renderer at the game render entry, publish its four
   outputs, and suppress the original eye rendering/preparation/consumers. Do not
   make production errors fall through to the native renderer.
4. Verify complete on-track output and actual CPU frametime reduction. Operation
   counters and synthetic GPU tests do not establish an in-game speedup.

Run only the SPS suite for this continuation:

```powershell
$env:LMU_QV_STEREO_HARDWARE_TEST = '1'
.\mod\build-msvc.cmd -SpsOnly
```

It returns before the older native-pipeline tests. Camera, mesh, parameter and
lighting models run at Od/O2. GPU checks cover WARP/hardware, VPRT/GS and
RGBA8/HDR, complete four-view pixels/depth, membership/transparency, imported
textures, changed bone poses, immutable resource versions, recording-context
reuse, bounded state calls, and the actual exclusive XR bridge. Native engine
entry points assert if entered in the XR bridge fixture, and native projection
view pointers are deliberately invalid. Full production DLL/game validation
has not been performed for the new route; no new playable package is claimed.

## Earlier support models and native prototype

| Component | Source and status |
| --- | --- |
| Current-frame input identity and camera numbers | `xr_frame_inputs.hpp`, `native_stereo_focus_inputs.hpp`, `native_stereo_world_pose.hpp`, `native_stereo_pose_adapter.hpp`, `native_stereo_view_producer.hpp`. Owned paired camera/constant/classifier/bounds inputs, native operation order and original-eye comparisons. The native right camera is still updated independently; the owned pair is not yet authoritative for both eye renders. |
| Independent visibility and ordering models | `native_stereo_owner_plan.hpp`, `native_stereo_scene_plan.hpp`, `native_stereo_owner_inputs.hpp`, `native_stereo_scene_inputs.hpp`. Actual unfiltered-root readers, per-eye admission and source classification, shared copied state and emission-time world snapshots. Complete live scene production and bucket ordering are not established. |
| Ordinary world refresh and source effects | `native_stereo_world_refresh.hpp`, `native_stereo_source_effects.hpp`. Local scale/attached world products, corners, AABB/radius, source-position caches, finalizer no-ops and empty/bypassed light scans. Active unsupported effects still refuse. |
| Constant and resource ownership | `native_stereo_constants.hpp`, `native_stereo_draw_inputs.hpp`, `stereo_gpu_resources.hpp`, `stereo_draw_capture.hpp`. Verified native CPU shadows, exact CB slots/ranges, immutable retention or GPU snapshots of mutable DEFAULT resources, complete supported draw packets. Mappable geometry refuses. |
| Shader and GPU execution backend | `stereo_shader.cpp`, `stereo_pipeline.hpp`, `stereo_native_registry.hpp`, `stereo_constant_arena.hpp`, `stereo_batch.hpp`, `stereo_batch_plan.hpp`. Cached conversion, native creation metadata, packed CB uploads, ordered paired/mono execution and context restoration. Full owned workloads pass GPU tests. |
| Native target handoff | `stereo_targets.hpp`, `stereo_opaque_layer.hpp`. Independently seeded arrays, plus a late strict-depth opaque composite against each eye's actual native contents. |
| Native integration prototype | `native_sps.cpp`, `native_sps_runtime.hpp`, `stereo_native_replacement.hpp`, `engine_bridge.cpp`, `xr_layer.cpp`. Early D3D creation hooks, native prepare/helper observation, focus opaque group-31 scopes, mutation/query tracking, paired submission and suppression after validation. Native scene traversal and material/bone preparation remain. |

`renderingEligible=false` in the independent scene models is intentional.
`sourceEffectsComplete` covers source effects only; it does not prove complete
material, bone or native-cache preparation. Do not enable native pass suppression
by changing those booleans without implementing their outstanding contracts.

## Historical live findings and fixes

1. The first integration run prepared 13,783 projection pairs but produced zero
   native world-pose pairs. The image was native fallback output.
2. A concrete input-reader bug was found: the four middle world-rotation
   coefficients were read from `rsp+0x70`, a temporary matrix changed by native
   head-angle rotations. The native eye-position path uses preserved `XMM15`.
   The reader now copies the unwind-recovered `CONTEXT.Xmm15`. A regression makes
   the stack copy deliberately differ and verifies all four XMM15 lanes.
3. The camera-fix live run then produced nine paired view inputs: nine left and
   eight right comparisons matched. One right mismatch disabled the prototype
   for the remaining 9,930 frames. The session-wide refusal was too aggressive.
4. Recovery now requires a fresh verified native left-camera boundary, then
   repeats exact comparisons for the new pair. A bad right pair stays native;
   it cannot authorize a stale right result. Every right mismatch is counted by
   mode, position, basis and translation; at most eight detailed examples log
   frame/time and expected/actual numeric and bit values.
5. The recovery live run continued processing later frames. Sampled positions
   matched exactly; some right bases differed, including small bit differences.
   Their underlying cause is not yet established. Do not label every difference
   a harmless tolerance issue or loosen checks to force activation.
6. That run's early stage summary recorded 112/111 focus groups, 3,584 prepared
   left candidates and 3,584 query refusals, with zero submitted pairs. Thus
   camera recovery alone did not reach the GPU replacement path.
7. The latest source corrects a query-tracking defect: an opaque completed command
   list invalidates predicted resource contents but does not permanently destroy
   immediate-query provenance. `FinishCommandList` implicitly ends deferred query
   ranges. Separately begun immediate queries remain tracked. Replay is excluded
   from native SPS interception. Real D3D tests cover an implicitly ended deferred
   query, execution between eyes, next-frame recovery and a distinct immediate
   result query spanning execution. See [Microsoft's command-list contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-command-lists).

The last query correction has passed owned WARP/hardware tests but has not been
revalidated in LMU. Stage logs now include query-known/foreign flags, active
result-query counts and executed command-list totals. The original cause of
every live query refusal has not been conclusively identified.

Sparse sampled on-track cadence was about 74 FPS before removing disabled-audit
FP bookkeeping and about 86 FPS afterwards. These were not controlled A/B runs,
and neither demonstrated SPS acceleration. No private game captures, shaders,
logs or personal settings are required to build the committed tests.

## Historical native-prototype integration notes

The notes below describe the older adapter. The active production direction is
the owned path above; these notes do not authorize keeping a native fallback or
restarting serial native-right comparison experiments.

1. Make the existing owned camera pair authoritative for both eyes of a rendered
   frame. The current read/predict/compare path still allows the native right
   update to differ. Preserve frame/session/view identity, camera history and
   downstream derived data; avoid relocalizing XR views from camera hooks.
2. Connect complete unfiltered scene production and independently valid per-eye
   membership/order to the existing scene and batch planners. Resolve outstanding
   source/preparation effects and real bucket comparator/tie behavior. A left-eye
   visible list cannot supply right-only objects.
3. Connect shared native material/geometry/skinning preparation and owned draw
   resources to the complete pass. The existing constant preparation, version
   checks, capture code and arena should be reused, not rebuilt.
4. Replace the one-pair controller with a whole-pass submission/handoff protocol
   and suppress the corresponding native work after complete preflight. Native
   preparation and effects must not be silently skipped. Merely increasing
   `frameAttempted_` is insufficient: the controller also retains one pending
   right result and rejects a second left submission with the same frame key.
5. Validate the complete supported path: both eyes, actual removed native work,
   consumers, transitions, failure/teardown, and matched CPU/GPU performance.
   Document remaining native passes explicitly; a launch flag or owned GPU test
   does not establish production SPS coverage.

Start with `nativeCameraStateHook`, `nativeCameraPoseHook` and `renderHook` in
`engine_bridge.cpp` for camera ownership. The native pass boundary is
`worldSortHook` (focus group 31), leading to `NativeSpsGroupScope` and
`NativeSpsRuntime::indexed`. `StereoBatch::prepare` already accepts complete
owned per-eye sequences; the one-pair native controller is the limiting adapter.

The owner-input collector caused a severe CPU regression when run continuously
in an earlier diagnostic. It is now explicitly opt-in and single-frame. Reuse
the input readers without turning continuous collection into the production
path. Native preparation flags observed in game include `0x12f` and `0x2f`;
do not reintroduce a requirement that flags equal 1 at the helper boundary.

## Native scheduling constraint

Observed order is outer-left, focus-left opaque group 31, left group 32 and later
consumers, outer-right, focus-right opaque group 31, right consumers. Both focus
eyes reuse native banks. Left consumers run before right focus preparation.
Therefore waiting for the native right draw list before completing the left image
does not preserve the frame. A final XR image lease does not solve this boundary.

The late opaque layer solves initial contents for its restricted domain:
single-sample D32 depth, full linear color output, strict LESS/GREATER depth with
writes, and no stencil/blending/graphics UAVs/result-query scope. Its tests cover
different late eye seeds, ties and physical-bank reuse. Broader pass coverage
needs its own complete output/state contract.

## Build and validation

Use `mod\build-msvc.cmd` for the full native suite and production DLL. Standalone
launcher checks include `mod\tests\performance_launch_test.ps1`. Windows D3D11
tests use WARP; set `LMU_QV_STEREO_HARDWARE_TEST=1` for the local hardware route.
No launcher GUI code changed in this continuation.

The complete MSVC suite and final DLL build passed with all committed
camera/recovery/query changes. Targeted hardware and WARP runs also passed with
RGBA8/HDR, mutation, worker, deferred-list and query cases. Numeric models pass
at Od/O2. Performance, portable-provider and focus-offset launch checks passed.

Tests named “native SPS live D3D adapter” use owned fixtures and real D3D hooks;
they do not execute the LMU renderer. MinGW build registration is updated, but
MinGW was not available in the current environment.
