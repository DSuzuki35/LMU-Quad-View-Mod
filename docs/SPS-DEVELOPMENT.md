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
and CopyResource/CopySubresourceRegion/ResolveSubresource calls. Copy-region
coordinates and resolve formats/subresources are preserved in the observations.

Bindings describe potential access, including draws that execute no work. Depth
view read-only flags refine the potential-access label; they do not prove that a
depth test ran. No resource contents are read back by this trace, and no reference
survives its immediate snapshot. Events and resource-use entries have explicit
limits, with truncation and exception-abort status in JSON.

Coverage is deliberately reported as partial. Clears, extended resolve APIs, mip generation,
maps/updates, Context1 variants, IA/constant-buffer/stream-output dependencies,
indirect argument contents, other contexts/threads and calls outside the capture
remain unobserved by this trace. Runtime method variants can also bypass installed
entry-point hooks: this occurred in WARP tests after context state changes. A
fresh-device fixture verifies the installed path, not universal interception.
Resource identities can alias reused addresses and do not establish lifetime or
content equivalence. Neither an empty trace nor a first observed read establishes
the absence of earlier consumers. `scheduling_eligible` remains false.

## Second capture: resource trace and MSAA (2026-11-01)

The resource-trace build at `84ff761` recorded two scene frames, 6,657 legacy draw records and
6,711 resource events (6,676 draws, 20 copy regions, 11 whole-resource copies and
4 dispatches), with no resource-event/use drops, truncation or abort. The extra
19 draw events were outside the legacy scene scopes. Coverage remains partial.

This run used 5208x2458 RGBA16_FLOAT focus color with two samples and D32_FLOAT
depth, unlike the previous single-sample run. Settings/scene differences mean
the counts are not an A/B performance comparison. Each sampled focus pair had
44 meshes in the left eye and 60 in the right: 44 shared identities and 16
right-only identities. Left-eye visibility cannot substitute for both eyes.

In each focus scope, the first subsequent observed draw after opaque group 31
was group 32 with the same color/depth resources still bound. In frame 1 the
left group-31 end sequence was 5350 and group-32 draw was 5353; on the right the
corresponding values were 7950 and 7953. This is a concrete later potential
read/write dependency before the other eye's focus work, so simply deferring
group 31 and letting later groups proceed is not a valid scheduler.

There were no observed SRV/copy-source reads of those multisampled intermediate
resources. Base ResolveSubresource was not hooked in this build, so this is a
missing observation, not evidence that post-processing never consumes them.
Add and test resolve source/destination/subresource/format records to follow the
multisampled-to-single-sample transition. Preserve the user's MSAA settings.
The existing private stereo proof is limited to single-sample targets; this
run also establishes a separate MSAA validation requirement before production
SPS can support this configuration.

The expanded read-only native code capture now covers the complete body of the
derived preparation helper at `0x565260` through its return at `0x5658e2`.
It selects transform-owner state, consumes the view's derived matrix at `+0x3A0`,
can calculate additional per-view values, writes several native constant ranges,
and updates shared renderer caches. Identical mesh identity therefore still does
not justify skipping the second eye's preparation. Called helpers and their full
effects remain separate work; having this function's complete bytes is not a
complete model of native state.

The follow-up diagnostic now hooks base ResolveSubresource, with its context
vtable index checked against the SDK. The WARP regression resolves an MSAA2
array slice into a nonzero single-sample mip/array subresource, verifies the
resulting pixels and correlates the source with the native target identity.
This closes the base-resolve observation gap; it neither validates native SPS
with MSAA nor removes the other partial-coverage limitations.

## Owned portable backend MSAA evidence (2026-11-01)

The offscreen `stereo_instancing_test` passes on WARP (Microsoft Basic Render
Driver) and NVIDIA GeForce RTX 5090 with 1x, 2x and 4x samples. Both multisample
counts report one color and depth quality level. The test compares independent
per-eye draws with one paired draw through both geometry-shader and vertex-array
routing, across three instance/base-instance ranges, asymmetric projections,
and full versus sample-zero-only masks. An owned compute shader reads every
RGBA8 color and R32 depth sample from two-slice MSAA arrays into staging buffers.
Checks require exact paired/reference color agreement, depth error at most
1e-6, known near/far depths and foreground colors, clear background, different
eyes, partially covered edge pixels, and clear color/depth in masked samples.
The initial 2x allocation assertion failed against the old 1x fixture before
multisample allocation was implemented. Unsupported required 2x is a failure;
unsupported optional 4x or vertex-array routing is explicitly reported as skipped.
This owned-fixture evidence does not extend the live private proof's MSAA support,
establish native stereo scheduling, or make a performance or production SPS claim.

## Live resolve observation while the headset was in standby (2026-11-01)

The resolve diagnostic recorded two scene frames in an offline Monza practice
session: 5,050 resource events, comprising 5,025 draws, 12 copy regions, 9 whole
resource copies and 4 base resolves. There were no resource-event/use drops,
truncation or abort. The runtime reported `shouldRender=0`; the capture contains
outer-eye scopes only and **no focus scopes**. It cannot answer the outstanding
focus-consumer question or validate visible headset output.

The observed outer color/depth targets were 1578x746 with two samples. All four
resolves used RGBA16_FLOAT (format 10), source/destination subresource zero, and
the same observed source and destination resource identities. Each resolve was
immediately followed in the recorded sequence by a draw with that destination
bound as pixel-shader SRV slot zero. This demonstrates live interception and
identity correlation for this path. It does not prove complete dependency
coverage, native ownership or safe cross-eye scheduling.

## Active focus resolve capture (2026-11-01)

With the headset awake, the follow-up diagnostic captured both focus eyes in
two scene frames. It recorded 5,827 resource events: 5,782 draws, 20 copy regions,
13 whole-resource copies, 4 dispatches and 8 resolves. All ten native group-31
scopes were complete, including four focus scopes; resource events/uses had no
drops, truncation or abort. The corresponding runtime log confirmed quad
submission with both eye captures and `shouldRender=1`.

The focus eyes shared observed 5208x2458 RGBA16_FLOAT color and D32_FLOAT depth
resources with two samples. Each sampled focus group contained 44 meshes. This
does not override the earlier evidence of different per-eye visibility lists.

| Focus scope | Group 31 ends | Next group-32 draw | Color resolve | Next observed SRV use |
| --- | ---: | ---: | ---: | ---: |
| Frame 1, left | 4518 | 4521 | 4636 | 4637 |
| Frame 1, right | 6961 | 6964 | 7079 | 7080 |
| Frame 2, left | 12049 | 12052 | 12167 | 12168 |
| Frame 2, right | 14539 | 14542 | 14657 | 14658 |

These are capture-local submission sequence numbers. Each resolve converted
the shared focus color resource to the same observed resolve destination,
format RGBA16_FLOAT, subresource zero. The next recorded draw bound that
destination at pixel-shader SRV slot zero. Between group 31 and the resolve,
groups 32, 33, 14, 15, 23, 24, 25, 27 and 29 issued more draws into the focus
color/depth resources. Further post-processing reused intermediate targets also
observed in outer rendering, then wrote the resolve destination again before
another shader read. A private HDR/depth pair alone cannot preserve this chain.

The result closes the missing base-resolve observation for this focus path.
It reinforces that delaying only opaque draws until the right eye is available
would let their consumers run too early. Native SPS still needs a verified way
to prepare/own both eyes before those consumers, or an explicitly owned and
ordered consumer replay. The capture remains partial dependency evidence,
not a completed scheduler or performance measurement.

## Native camera and packet preparation findings (2026-11-01)

Read-only inspection of the pinned game's active cockpit camera resolved its
indirect update function, including its split unwind ranges and switch table.
LMU already provides both eyes' projection, far-projection and relative-transform
data in one native camera payload. A numeric opposite-eye prediction can
therefore be investigated without calling the entire camera update twice.
The native camera also applies mode-dependent offsets and optional extra view
rotation; a prediction must reject paths it has not reproduced.

An asynchronous numeric probe found a promising transform relationship for
the sampled equal-orientation eyes, camera mode zero, transform mode one and
zero additional view offsets. Repeated reads were consistent, but the probe
was not synchronized to render frames. It establishes a hypothesis only.
The next check compares owned numeric predictions with the native right eye
within the same captured frame, retaining every native render operation.

Packet inspection also identified the builder for the group bucket/item/mesh
lists. Its membership flags and mesh-pointer arrays live on shared objects;
building a second scene header does not make those lists independent. Item
and bucket retain operations exist, but retaining them alone does not prove
ownership of the upstream mesh-record array. A conditional material/geometry
mutation path is another restriction on any first paired subset. These findings
leave native packet lifetime, independent visibility and consumer ordering as
required work before genuine SPS can remove a draw or preparation pass.

## Same-frame camera comparison (2026-11-01)

The bounded F8 audit now copies numeric camera inputs on the render thread and
compares an outer-left prediction with the native outer-right result from the
same captured frame, scene and main view. It rejects incomplete captures,
duplicate eye scopes, changed payloads and unsupported camera modes instead of
selecting an arbitrary pair. Disabled or idle capture does no new camera reads
or prediction work. Snapshots own their numbers; they never invoke native
methods on copied engine objects.

Five two-frame captures in the offline Monza cockpit produced ten available
outer comparisons. The runtime was awake during these captures and submitted
both quad-view eye images. Projection and far-projection maximum absolute
errors were zero in all ten comparisons. The largest current-transform and
derived-matrix errors were both `6.103515625e-5`. These are absolute matrix-entry
differences, not pixel errors or an established acceptance tolerance. This
short sample supports the restricted camera hypothesis; it does not validate
all head poses, canted views, other camera modes or visible image equivalence.

Each capture recorded ten complete native pass scopes with no native scope
drops or diagnostic abort. Eight main-view camera snapshots were complete;
the two extra outer-left views were correctly excluded as non-main views.
Focus snapshots were recorded, but focus comparisons explicitly reported
`unsupported focus projection input`: the copied native payload supplies outer
FOVs, while focus FOVs come from the separate OpenXR frame state.

The native test suite and affected follow-up fixtures passed, including owned
snapshot tests, analytic matrix-order cases, refusal cases and WARP JSON/draw
checks. The independently reviewed diagnostic preserved native preparation
and drawing. All comparisons still report `scheduling_eligible=false`.
There is no SPS performance claim or 0.1.1 release from this camera result.

## Source-array containment and capture trigger (2026-11-01)

The audit now checks each captured mesh against the source geometry's reported
record array, copying at most 24 bytes per accepted packet. It records the array
identity, count, mesh index, sorting distance and presence of a lazy refresh
state. These are owned observations, not retained engine objects or proof that
the array stays unchanged. The existing 32,768-packet capture limit also bounds
these reads.

A two-frame offline Monza session with mbucchia QVFR produced 73 left-eye and
74 right-eye outer packets in each frame. All 294 packet observations matched
their reported source arrays. The 73 shared meshes had identical array
descriptions between eyes, but every shared mesh had a different sorting
distance. Each right-eye list contained one additional mesh. A null lazy-state
pointer was observed on 68 left and 69 right packets per frame; that excludes
one known refresh path only, and does not establish general staticness.

The capture finished normally with no native scope drops or diagnostic abort.
The headset was in standby, so this result covers the outer-eye path only.
It reinforces the requirement for independently owned membership and sorting
data for each eye. Neither retaining an array address nor reusing the left-eye
list supplies that data.

Opt-in audits can also be requested by changing `logs/stereo-audit.request`
after the diagnostic has initialized. The contents are ignored. The audit
checks the timestamp at most twice a second outside active captures; a stale
startup file does not trigger a capture. The Monza run used this trigger and
completed the existing two-frame capture automatically. Ctrl+Alt+F8 remains
available. Both methods add diagnostic overhead and are unsuitable for
performance measurements.

An independently reviewed, restricted numeric distance/frustum classifier now
has 710 analytic checks passing at both unoptimized and optimized builds.
It owns its inputs and distinguishes returned flags from stored flags. This
models one native calculation, not the whole visibility decision. Comparison
against the original invocation is the next diagnostic step; the model is not
used to alter native rendering.
