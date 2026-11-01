# Single-pass stereo development

This is work toward a future 0.1.1 implementation, not a claim that the game
currently renders in single-pass stereo. The released 0.1.0 path still executes
native eye rendering and the two additional focus scene traversals.

## Goal and acceptance

Prepare compatible scene work once and submit it for both eyes, while preserving
the separate eye images, focus projections, depth, post-processing and existing
outer-resolution GPU savings. A successful private shader comparison is one
prerequisite; production SPS must also replace duplicated native work safely.

Acceptance requires matched on-track CPU/GPU and P95/P99 measurements, image and
depth comparisons, correct eye tracking, and working failure/teardown paths.
Unsupported work must use the complete native path before visible output is
changed. No stale views, previous-frame eye inputs or left-only visibility lists
may stand in for valid current-frame per-eye inputs.

## Findings from the supported live renderer

The executable guard remains SHA256
`66942337813b301b87d12edfa330f33354a2a4b8cd9b048885cbf77eb6f940cf`.
The following addresses are RVAs in that build, not general engine APIs.
Names describe observed behavior; they are not recovered source symbols.
Local mapped-code inspection was read-only. Game binaries and code captures are
private development inputs and must not be committed.

| Boundary | Verified observation | Consequence |
| --- | --- | --- |
| `0x51f040`, group 31 | Walks ordered bucket, item and mesh vectors | Record the ordered inputs before native cleanup |
| `0x5ddb80` | Computes a bucket's group-vector address | No native call is needed just to inspect that header |
| `0x5629e0` | Performs conditional preparation helpers and returns one | Its return value alone does not make preparation removable |
| Returns `0x51f67e` / `0x51f5c4` | A zero preparation result skips the associated draw and retains surrounding traversal | A possible future suppression point, after required side effects are replaced |
| `0x5e0d80` / `0x5e0e60` | Last-pass cleanup releases entries and empties native vectors | Copied pointers do not own deferred packets |
| Group-31 caller return `0x51df1d` | Native state transitions and group 32 follow | Later consumers must be understood before delaying this pass |

For group 31, the observed pointer-vector hierarchy is:

```text
scene + 0xDB0 -> buckets
bucket + 0x4F0 -> items
item + 0x490 -> meshes
mesh + 0x80 -> geometry
```

Each vector header consists of begin, end and capacity addresses. These describe
native storage; they must never be treated as an owning C++ `std::vector`.
Mesh callbacks at `+0xCC`, special mesh flags and shader callbacks selected by
preparation flags need explicit handling. Preparation also consumes derived view
state at `view+0x3A0` and changes native caches and retained material/resource
state. Restoring constant-buffer bytes cannot replace all those effects.

The current focus order is left scene plus consumers, followed later by right
scene plus consumers. Holding a final XR focus image does not preserve the
native HDR/depth banks or undo post-processing that already ran.

## Implementation sequence

1. Extend the existing opt-in two-frame audit with bounded native group-31 input
   snapshots, preparation metadata and target identities at entry/exit. Copy
   scalar observations into owned records, anonymize native identities, and
   mark truncated or invalid captures incomplete. Keep every original draw.
2. Use those captures and resource-consumer observations to choose a real native
   scheduling boundary. Establish ownership of both eyes' packet inputs and
   intermediate targets before either output is consumed.
3. Integrate the existing converted stereo shaders for one complete compatible
   packet range. Preserve material callbacks, cache state, counters, traversal
   and cleanup. Drain or fall back correctly on missing eyes, abandoned frames,
   resource changes and teardown.
4. Compare the resulting images and timing with unchanged native rendering,
   separately for QVFR and VectorXR, before exposing a normal-use SPS option.

The audit in step 1 is diagnostic work, not an optimization. Target identities
before and after a scope do not identify every intervening consumer or prove
resource-content equivalence. It cannot authorize draw suppression on its own.

Two scheduling approaches remain under investigation: prepare both eyes before
rendering the selected range, or preserve per-eye native banks and replay delayed
consumers. The latter needs more state and can add enough copying to erase its
benefit. Pairing outer and focus views of the same eye is a different multiview
approach; it must not be presented as left/right SPS.

See the [existing shader/ownership handoff](../INSTANCED-STEREO-HANDOFF.md) and
[profiling instructions](../RENDER-PROFILING.txt) for current capabilities and limits.

## First native packet capture (2026-11-01)

The opt-in audit completed two on-track scene frames with 10,542 draw records,
10 complete native group-31 scopes, 3,492 packet observations and 8,558 preparation
records. No draw, native scope, preparation, shader-definition or layout-definition
records were dropped. These diagnostic frames are not a CPU/GPU benchmark.

| Focus comparison | Frame 1 | Frame 2 |
| --- | ---: | ---: |
| Packets / draws per eye in the observed group | 398 | 397 |
| Mesh identities present in both eyes | 398 | 397 |
| Draw positions with different mesh identities | 21 | 21 |
| Matching geometry/input signatures, paired by mesh identity | 393 | 392 |
| Matching shader IDs | 398 | 397 |
| Matching recorded PS resource/sampler signatures | 384 | 382 |

Geometry/input signature here means draw kind/arguments, vertex/index signatures,
input layout and topology. Resource signatures cover the audit's recorded slots,
not all pipeline bindings or resource contents. These matches do not establish
replay safety or ownership.

The primary passes reused the same observed view object. Both outer eyes used
one observed 1578x746 intermediate color/depth pair; both focus eyes used one
3156x1490 pair. Color view format was RGBA16_FLOAT, with one sample. The native
order was outer-left, focus-left, outer-right, focus-right. Target identities
were unchanged at each observed scope's entry/exit; intervening writes and
consumers remain unknown. An additional 2560x160 pass appeared inside each
outer-left scope, so enclosing eye labels alone cannot classify every draw.

Each focus scope contained 40 preparations with a shader callback and 47 packets
with the observed dynamic-preparation flag. Special-bit packets numbered 353/352
per eye in frames 1/2. Filtering those flags and callbacks leaves 45 observations
per eye for further inspection, not 45 approved stereo draws. The special bit
also needs interpretation against the caller flags before it can be treated as
an unconditional exclusion.

This confirms substantial shared object identity, but also real per-eye order,
input and resource differences. Do not pair by draw index or reuse the left-eye
packet list wholesale. The next investigation must correlate bound intermediate
resources with subsequent reads/writes, establish their first consumers, and
identify a native scheduling point that preserves both eyes' inputs and targets.
Only then can a paired draw replace native work. Raw game captures stay local.

### Follow-up: conditional special draw path

Inspection of the pinned renderer establishes that the extra draw path tests
both caller flags bit 1 (`flags & 2`) and mesh bit `0x400000`. The caller bit is
decoded at RVAs `0x51f0c3` through `0x51f0e1`, then checked before the mesh bit at
`0x51f58f` through `0x51f5a0`. The ordinary path separately tests caller bit 0.
All four captured focus scopes have caller flags 1; their preparations have no
supplied high bit and no reset request, consistent with taking the ordinary path.

Consequently, excluding every mesh with `0x400000` was an intentionally strict
initial filter, not a demonstrated requirement for this observed pass. Excluding
mesh callbacks, dynamic-preparation flags and shader callbacks instead leaves
311 observations per eye in frame 1 and 310 in frame 2. These are candidates for
further investigation only: other preparation, visibility, state, resource and
ordering requirements still apply. Do not generalize this result to caller flags
2/3 or turn it into a production skip rule.

## Resource-use trace

The next diagnostic adds `resource_uses` to the same local F8 JSON. Events share
the native target resource IDs and submission sequence, including selected-context
calls between native scene scopes (`slot: -1`). It observes all 128 SRV slots in
each graphics/compute stage, bound color/depth/UAV targets, compute dispatches,
and the existing CopyResource/CopySubresourceRegion hooks. Copy-region coordinates
and subresources are preserved in the observations.

Bindings describe potential access, including draws that execute no work. Depth
view read-only flags refine the potential-access label; they do not prove that a
depth test ran. No resource contents are read back by this trace, and no reference
survives its immediate snapshot. Events and resource-use entries have explicit
limits, with truncation and exception-abort status in JSON.

Coverage is deliberately reported as partial. Clears, resolves, mip generation,
maps/updates, Context1 variants, IA/constant-buffer/stream-output dependencies,
indirect argument contents, other contexts/threads and calls outside the capture
remain unobserved by this trace. Runtime method variants can also bypass installed
entry-point hooks: this occurred in WARP tests after context state changes. A
fresh-device fixture verifies the installed path, not universal interception.
Resource identities can alias reused addresses and do not establish lifetime or
content equivalence. Neither an empty trace nor a first observed read establishes
the absence of earlier consumers. `scheduling_eligible` remains false.
