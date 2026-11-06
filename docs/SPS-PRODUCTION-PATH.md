# Owned SPS path

Production SPS is the goal; it has not yet been delivered in LMU. The new owned
renderer and exclusive XR output pass SPS-only tests. The live game producer and
complete game frame graph remain unfinished, so native game rendering is still
active and no SPS CPU frametime improvement has been measured.

Start with [SPS-HANDOFF.md](SPS-HANDOFF.md) for the current component map,
validation and remaining work. The older `Start-SPS-Rendering.cmd` selects the
native one-pair prototype and does not activate this renderer.

## Frame ownership

`SpsFrameCameras` builds all four views from one immutable `QuadFrameInputs`
snapshot and shared `SpsCameraPlacement`. It owns projections, planes, history
and camera cuts. It does not wait for a native right-camera update or compare
against a later native eye.

`SpsScene` owns immutable asset versions and mutable instances. It traverses
instances once, prepares their union, and builds four independent visibility and
ordering lists. Material/animation CBs are shared; explicit camera/transform
patches are prepared for each admitted view. Transparency order can differ
between eyes. `StereoBatch` uses ordered single-eye actions where necessary;
these still belong entirely to the SPS pipeline.

`SpsRenderer` clears its own paired array color/depth targets, preflights both
complete workloads and submits both. It exposes images only after the whole
quad succeeds. Every image carries its exact frame identity and must be consumed
before history commits. There is no native seed or fallback callback.

## Game inputs and CPU work

`read_sps_game_mesh` reads the pinned unfiltered source-array mesh records,
geometry bindings, program handles, list/strip draw ranges and owned bone
palettes. It never invokes a rendering callback or native preparer. Pending
uploads, active mesh callbacks, unsupported streams and unfinalized skeletons
currently refuse; they still need complete owned producers.

`SpsGameParameters` writes owned buffers using the selected game shader ABI,
including ordinary transform fields, skinning, material fields and the PS alias
of the VS common buffer. `sps_game_lighting.hpp` reads source colors and shared
scene/light/environment numbers, then computes ambient, diffuse, directional
and specular constants without mutating native material caches.

`import_sps_game_asset` combines creation-time shader/layout metadata, explicit
material inputs and versioned geometry/textures into owned assets. The live
material resolver must still supply complete textures, samplers, render states
and remaining shader parameters. Test material inputs do not establish that
this resolver exists in LMU.

Resource versions must come from the owning producer. Unchanged snapshots are
retained; a changed version creates fresh owned contents. Bone/material buffers
stay shared across eyes, rather than being duplicated in the packed camera CB.
Recording contexts survive frames. Submission caches shader, target, mesh,
resource, sampler and constant ranges, binding only changes. Immutable state
devices are checked during asset preparation rather than queried per eye draw.
These remove unnecessary CPU work; their in-game frametime benefit remains to
be measured after integration.

## Exclusive XR output

`xr_prepare_sps_output` creates four mod-owned runtime swapchains and arms an
exclusive submission route. `xr_publish_sps_frame` accepts the complete current
SPS quartet, copies and releases its images, consumes renderer outputs and
commits frame history. Application/native projection layers are ignored by this
route, even when the SPS frame is missing or fails.

`SpsXrOutput` acquires/waits the quartet before writes, retains acquisition
ownership on incomplete waits/releases, rejects stale image/frame identities,
and cleans up unpublished frames without permanently disabling later SPS work.
The layer serializes publication, EndFrame and session teardown. It does not
require application focus-image leases or native final eye contents.

The current output contract is resolved single-sample color. The game-specific
frame graph must supply the required output encoding and complete effects/UI.
No engine adapter enables the new route yet.

## Validation

```powershell
$env:LMU_QV_STEREO_HARDWARE_TEST = '1'
.\mod\build-msvc.cmd -SpsOnly
```

The dedicated suite executes no native reference renderer. It tests analytic
four-view pixels and depth through WARP/hardware, VPRT/GS and RGBA8/HDR; imported
mesh/material/texture inputs; shared skinning with a changed pose; per-eye
membership and transparency; resource versions; allocation/context reuse;
camera/history/FP ownership; and the production XR publication/submission code.
Native engine/render/preparation calls are poisoned in the XR bridge fixture.

Passing these fixtures is not production completion. Next work must connect the
complete live producer/frame graph, remove the duplicated original rendering
path, and verify actual LMU output and CPU frametime.
