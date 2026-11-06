# SPS rendering

For current source status and the remaining production integration, read the
[maintainer handoff](SPS-HANDOFF.md) and [owned production path](SPS-PRODUCTION-PATH.md).
The launcher entry point below enables the older one-pair prototype; it does not
activate the new owned renderer or a complete production SPS pass.

Run `Start-SPS-Rendering.cmd` from the portable folder. It uses the saved game,
provider and foveation settings in `launcher-settings.xml` and enables the native
SPS adapter. Close the game before starting this mode so the adapter can observe
device, shader and input-layout creation from the beginning.

The source launcher also accepts `launch.ps1 -SpsRendering` in quad mode. This
implies SPS camera preparation. Profiling, audit, owner collection and CPU upload
reuse must be disabled. The ordinary launcher remains available for normal runs.

The adapter currently attempts one eligible opaque draw pair per frame. It keeps
native scene traversal, visibility, material/bone preparation and both original
consumer boundaries. Cached converted shaders render the two eyes in one paired
submission. The left result is composited at the original left draw. The right
original is suppressed only when its actual inputs match the prediction and no
tracked resource write invalidated it. Refused or unmatched right draws render
natively.

Supported output is single-sample linear color with D32 depth, depth writes and
strict LESS or GREATER comparison. Blending, stencil, graphics UAVs, stream
output, predicates and result-query scopes retain native rendering. The adapter
captures mutable DEFAULT resources on the GPU and copies constants from verified
native CPU shadows. It does not read mapped WRITE memory or compile shaders in
the draw hook.

The normal layer log records successful activation once:

```text
sps_render active: both original native focus draws replaced by one paired submission; actual right inputs matched
```

On shutdown, `sps_render native paired=... left_removed=... right_removed=...`
reports actual replacement counts, right fallbacks, captures and refusals. A
launch option being enabled alone does not prove that eligible draws were found.

Automated tests cover real D3D11 interception on WARP and the local GPU, exact
RGBA8/HDR color and depth, reused native banks, resource changes between eyes,
worker-update recovery, query preservation and teardown. Actual LMU activation,
visual correctness and net CPU/GPU gain still require an in-game run. This
bounded stage does not yet replace the entire opaque pass.

The first live run on 6 November reported `paired=0`, with the early native
world-pose producer unavailable on all 13,783 scene frames. The displayed image
was native fallback output. The updated build avoids audit FP bookkeeping when
auditing is disabled and reports a bounded camera refusal message after 60 scene
frames. Successful in-game SPS activation and a performance gain remain open.
