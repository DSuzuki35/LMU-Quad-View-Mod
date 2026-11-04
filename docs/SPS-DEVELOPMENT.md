# Single-pass stereo development

This integration carries the SPS research from `experiment/sps-0.2.0` at
`61a5940` onto main v0.3.3 at `c38e122`. It does not enable native single-pass
stereo. Older entries below retain their original version targets and source
baselines as historical records. Native eye rendering, preparation and the two
additional focus scene traversals still execute.

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

## First original-invocation classifier capture (2026-11-01)

The reviewed diagnostic now observes the original classifier call, preserving
its arguments, return value and floating-point environment. It copies bounded
inputs before and after the call and evaluates the owned model during export.
The native test suite passed; review also found and corrected a failure-status
race with concurrent capture shutdown. A targeted allocation-failure regression
failed before that correction and passed afterward.

The first two-frame offline Monza capture confirmed the hook operated, but
produced **no available numeric comparisons**. Its 2,048 recorded attempts
comprised 1,024 non-main-view refusals and 1,024 six-plane tests outside the
initial model's zero-to-four-plane domain. Another 7,152 attempts exceeded the
per-slot diagnostic cap. The headset was in standby, so only outer slots ran.
The recorded MXCSR value was `0x1fbf`: default control bits with sticky status
flags set, which the diagnostic preserves.

This is evidence about missing coverage, not agreement with the game. The next
step is to select the expected main view before consuming the record budget
and extend the model to the observed six-plane path. That path includes a
special-plane rejection that returns different flags from those stored on the
candidate, plus a secondary distance test. Native rendering remains unchanged.

## Six-plane original-invocation comparison (2026-11-01)

The owned model now covers zero through six planes, including the special-plane
and secondary-distance checks. The capture selects the expected main view before
consuming its record budget. Independent review passed, as did the full native
build and 1,014 analytic checks at each of the unoptimized and optimized settings.

A subsequent two-frame offline Monza capture with mbucchia QVFR produced
**2,048 available comparisons with zero returned-flag or stored-flag mismatches**.
Each frame supplied 512 comparisons for each outer eye. All sampled calls used
six planes and view mode 1; all used default MXCSR controls with sticky status
flags (`0x1fbf`). Before/after input reads plus the stored-flag read reached at
most 412 bytes per invocation, below the explicit 448-byte limit.

The capture excluded 4,686 other-view calls without spending the main-view
budget. Another 1,964 main-view calls exceeded the per-slot cap, so it correctly
reported incomplete classifier coverage. There were no missing expected-view
identities or refused numeric comparisons. All 53 left and 54 right captured
packets per frame had a candidate with an available comparison; this does not
prove that every candidate or visibility decision was observed.

The headset was in standby, so only the outer path was exercised. This sample
also did not exercise a native return value different from the stored flags;
that distinction remains covered by analytic fixtures. No open scopes remained
at export and the native-pass audit did not abort. LMU was closed normally after
collecting the inputs.

These results support the sampled classifier calculations, not complete source
traversal, object lifetime, paired scheduling or image equivalence. Native calls
and draws remain unchanged. Native SPS and release 0.1.1 are still unfinished;
there is no new performance claim from these diagnostics.

## Owned owner traversal planner (2026-11-01)

`native_stereo_owner_plan.hpp` models owner admission from an entirely owned tree
supplied before either eye's filtering. It performs a complete left DFS followed
by a complete right DFS on shared copied scalar state, retaining ordered owner
admissions, unfiltered opaque source IDs and symbolic state transitions. This
preserves right-only subtrees and cross-eye mutation order without native reads,
calls or writes. Attachments bypass pending/current assignment and refresh work;
the owner flag and all-bits global/view mask gates still apply.

The planner accepts 1..4096 nodes and at most 65536 unique source references,
validates the whole tree and refuses unsupported null-attachment refresh/special
flags even in rejected branches. Traversal uses bounded iterative storage, and
refusals or allocation failures publish no partial plan. Analytic tests run at
O0 and O2, including maximum depth and allocation-failure coverage.

Its immutable-tree contract excludes intervening candidate/consumer mutations
and is not established for LMU integration. Owner admission does not establish
candidate visibility, drawable ownership or native scheduling. Every result
reports `renderingEligible=false`; there is no runtime producer or switch.
VERSION remains 0.1.0, and native SPS and release 0.1.1 remain unfinished.

## Owned transform constants and original-call comparison (2026-11-02)

The next bounded preparation component owns the numeric output of the ordinary
native transform helper at RVA 0x565260. It does not replace that helper, its
caller, material preparation, renderer cache effects, or any draw.

Read-only inspection of the pinned analysis image confirms:

- Preparation RVA 0x5629e0 selects shader flags when the low 31 supplied bits are
  zero. Effective flag 1 invokes transform preparation after helper 0x5623d0.
  That helper can still modify bone-related state; it must remain enabled.
- Ordinary transform selection uses mesh->geometry (+0x80), geometry->owner
  (+0x130), and the owner's matrix at +0x298. Mesh flag 0x8000 enables multiplying
  that matrix by the per-eye derived/view matrices. Flag 0x100000 selects an
  orientation path not modeled here.
- The view matrix starts at view+0x1dc, eye-position scalars at +0x220, and the
  derived matrix at +0x3a0. The matrix products use pairwise four-product sums.
- A nonzero **64-bit** shader+8 enables writing VS registers 4..7 (world), 0..3
  (derived), 8 (position xyz and zero w), and 16..19 (view). The CPU shadow starts
  at image RVA 0x1b13630. PS register 15 and native cache effects are outside this
  comparison; c9..c15 must not be overwritten or treated as transform output.

`native_stereo_transform.hpp` computes these 52 float scalars from owned numeric
inputs. It rejects nonfinite inputs/intermediates and unsupported MXCSR controls,
preserves FP state, and returns no partial payload. Separate eye calls own their
different results. Successful calculation never implies rendering eligibility.

The existing opt-in F8/file-trigger audit now captures a restricted original
invocation: effective flags exactly 1, reset false, enabled shader, nonnull
mesh/geometry/owner, no mesh callback and no orientation flag. It compares copied
before/after inputs and native shadow output only when the input values and
pointer-chain identities remain stable. It never substitutes the prediction.
The original signed return and original entry/post FP environments are preserved;
original exceptions propagate without retry. Numeric evaluation occurs at export.

The `native_transforms` JSON section reports bit-exact agreement/mismatch, refusal
reasons, compared scalar count, first differing register/component, and bytes
read. A strict attempt cap of 128 per frame/pass slot permits at most 1,024
records over the two-frame capture. Non-main views are excluded before that cap.
Each full invocation copies at most 696 native bytes; no raw addresses are
serialized. Closed/reopened scopes, capture resets, duplicate end calls and
shutdown reject stale tokens before reading any invocation pointers.

Owned numeric/capture tests passed with GCC 16 at O0 and O2 (warnings-as-errors,
FP contraction disabled). The integrated WARP audit test passed with GCC and
MSVC, including a regression that first reproduced stale reads when the same
pass slot reopened within one capture. A scope epoch now rejects those tokens.
The complete MSVC native build/tests passed and produced the DLL. All MSVC
consumers use /fp:strict, including the final DLL, so the tests and production
calculation share the floating-point contract. Existing C4459 shadow warnings
remain in the audit/resource-trace sources; no build errors remain.

Local build notes: the Windows PowerShell 5 wrapper split the /Fe output path
containing spaces, so validation used the installed VS x64 environment and
PowerShell 7 to execute the same build-msvc.ps1. The GCC audit check required
-Wno-misleading-indentation for pre-existing compressed statements; that switch
was not added to the repository build. Neither issue required changing gameplay
code. Independent review checked native offsets/math and lifecycle constraints;
both reported issues (scope identity and shared MSVC strict mode) are addressed.

### Next headset-PC test

Use an **awake** headset and an on-track scene with diagnostics/stereo audit
enabled, then capture two frames via Ctrl+Alt+F8 or `logs/stereo-audit.request`.
Check `native_transforms`: distinguish available matches, actual mismatches,
unsupported-domain refusals and dropped attempts. Slots 2/3 and 6/7 are the focus
eyes; outer-only or zero-available captures cannot validate this focus subset.
Do not benchmark with this audit enabled. No live transform match or performance
gain has been measured for this change. A Pimax is unnecessary for the owned
tests, but the intended Pimax/runtime setup still needs an eventual live check.

The original transform work above was tested on the older research baseline.
The integration described at the top brings in main's crop/motion/launcher and
frame-handling work. Before replacing draws, resolve candidate/resource
lifetimes, remaining preparation effects, per-eye visibility/order and the
native consumer schedule. Live transform agreement is still pending.

## Awake VectorXR capture on the integrated baseline (2026-11-04)

Two hotkey captures on integration commit `47493ce` completed four scene frames
with VectorXR, both focus eyes and successful quad submissions. The layer hash
matched the integrated diagnostic build. All 16 native opaque scopes completed,
with no native scope/preparation drops or diagnostic abort. Resource tracing
reported no event/use drops; its dependency coverage remains partial.

The classifier supplied **8,192 available original-invocation comparisons with
zero returned-flag or stored-flag mismatches**, including **4,096 focus-eye
comparisons**. Both captures still reached the per-slot cap and explicitly report
incomplete coverage. This extends the earlier outer-only agreement to sampled
focus calls, not complete visibility, object ownership or production SPS.

The transform diagnostic recorded 2,048 attempts, all unavailable with the reason
`unsupported preparation flags or disabled shader`. It compared **zero scalars**;
3,878 later attempts exceeded the diagnostic cap. Separately, all 15,467 complete
preparation records used flag combinations other than exactly 1. The observed
values were `0xf`, `0x2f`, `0x12f`, `0x292f`, `0x812f` and `0xa92f`.
Thus the restricted whole-preparation comparison domain was not exercised.
Increasing its record limit would not admit these calls, and zero mismatches
with zero available comparisons must not be reported as agreement.

Focus color was 1352x1734 RGBA16_FLOAT with one sample and D32_FLOAT depth. In the
first capture, the two sampled focus pairs had 410/410 and 383/383 packets; their
mesh order differed at 20 and 19 positions. In the second they had 344/345 and
340/341 packets, each with one right-only mesh and much wider order differences.
Each pair reused the observed color/depth banks. The next observed access after
left group 31 was a group-32 draw into those banks, before right focus began.
This again rules out simply delaying left opaque draws until the right eye.

Four outer camera comparisons were available: projection/far-projection errors
were zero, and maximum current/derived matrix-entry error was 0.0001220703125.
Four focus camera comparisons remained unavailable due to unsupported focus
projection input. These are numeric observations, not a pixel-error tolerance.

### Next transform boundary

Investigate observing the ordinary transform helper itself at RVA `0x565260`,
after earlier bone/material preparation and before later preparation helpers.
The pinned wrapper calls it when effective flag bit 0 is set, including the
common observed `0x12f` path; the current diagnostic instead requires the entire
wrapper's flags to equal 1. Broadening that equality to a bit test while retaining
the existing before/after-wrapper comparison would admit unmodeled intervening
effects and is not justified by these captures.

Before implementing the helper probe, verify its calling/return convention,
every admitted call site, mode arguments, target function boundary, and which
outputs are observable at its return. Keep bounded reads, FP preservation,
scope/generation checks and every original helper/preparation/draw. A successful
helper comparison would establish only its numeric payload, not remove the
larger native wrapper or establish scheduling. No SPS speedup was measured.

## Transform-helper boundary diagnostic (2026-11-04)

The runtime comparison now brackets the original `0x565260` helper itself,
instead of the larger preparation wrapper. Read-only inspection verified the
call at `0x562aca`: shader/view/mesh are in RCX/RDX/R8, alternate-owner mode is in
R9b, and the fifth stack argument is the supplied-flags bit-9 mode. The caller
continues at `0x562acf` without consuming a return value. The adapter treats the
verified helper call as void. Its built MSVC unrecognized-caller path forwards
all five arguments and, after the native call, only restores RSP/RBP and returns;
inspection confirmed it does not modify the native RAX/XMM0 return registers.

Hook admission checks the complete 26-byte prologue, the current PE function
boundary and the call plus following flag test. The selected helper/caller bytes
also match the existing private live-code container, and 382 helper instruction
prefixes agree with its stored disassembly. The larger container intentionally
restores old hook prefixes elsewhere, so its whole-range hash is not used as an
unaltered live-memory hash. No game bytes are committed or packaged.

Only the verified caller produces transform records. Both helper mode arguments
must be false; enabled shader, mesh/geometry/owner, ordinary mesh orientation,
callback, stable numeric inputs and FP controls retain their existing checks.
The helper boundary does not require the entire preparation flag word to equal
1: earlier bone/material work has already happened, and later preparation has
not yet run. The old preparation-boundary fixture remains restricted to flags 1;
its admission was not widened. Every original helper, preparation and draw runs.

The helper snapshot copies at most 240 native input bytes. Two input snapshots
and the 208-byte output snapshot total at most 688 bytes per admitted invocation,
below the existing 696-byte bound. Limits remain 128 attempts per slot and 1,024
over two scene frames. Prediction runs only at export. Missing/disabled/idle,
wrong-thread, stale-token and reopened-scope behavior retains the no-read gates.

`native_transforms` now reports hook availability, each record's boundary/modes,
available and focus-available comparisons, refusals, mismatched records, compared
scalars, unfinished calls and incomplete coverage. A mismatch includes the first
register/component and expected/observed float bit patterns. Unavailable records
remain refusals, never matches; scheduling eligibility remains false.

The full MSVC native build/test suite passed. Numeric and capture fixtures passed
at `/Od` and `/O2`, including all six observed shader flag combinations admitted
at the helper boundary while refused by the larger-wrapper boundary. The audit
fixture parsed as valid JSON with 124 available comparisons, four deliberate
refusals and one deliberate mismatch; abandoned invocations stayed unavailable.
Forwarding/FP/exception, caps, lifetime and synthetic hook-validation checks pass.
Existing audit shadow warnings remain. No GCC or new live helper result is
claimed by these tests.

The next step is one awake on-track capture from this revised build. Require
`hook_available=true`, `boundary=transform_helper`, usable records in both focus
eyes (slots 2/3 and 6/7), and inspect matches, mismatches and refusal reasons.
This is still step 2 of the continuation plan. A usable comparison is required
before the ownership/scheduling work can rely on this numeric payload; it does
not itself replace native work or establish a frametime gain.

## Live helper agreement on VectorXR (2026-11-04)

The helper-probe build from `0908632`, layer SHA256
`A3F4FB298F7407565E4C7D760206724E8A53AFFD4F5DFF269E00B3DBD7832553`,
completed two awake on-track hotkey captures on the pinned executable with
VectorXR. Both captures finished two scene frames with no open scopes at save.
The runtime log confirms helper-hook installation, successful engine startup and
quad submissions with both eye captures and `shouldRender=1`.

| Capture (UTC) | Available helper comparisons | Focus comparisons | Compared float scalars | Mismatched scalars | Later attempts beyond cap |
| --- | ---: | ---: | ---: | ---: | ---: |
| 19:07:40 | 1,024 | 512 | 53,248 | 0 | 3,544 |
| 19:07:59 | 1,024 | 512 | 53,248 | 0 | 2,578 |
| Total | 2,048 | 1,024 | 106,496 | 0 | 6,122 |

Every slot 0..7 supplied 128 available comparisons in each capture, including
focus slots 2/3 and 6/7. All records were ended, had boundary `transform_helper`,
both mode arguments false, 688 bytes read and 52 compared scalars. There were no
refusals, mismatched records, unfinished invocations or missing expected views.
The first capture excluded 162 non-main-view calls before spending its budget.
These are invocation counts, not counts of unique objects or shader families.

Both captures explicitly report incomplete coverage because of the per-slot cap.
The result validates the sampled ordinary helper's selected CPU constant output
bit-for-bit, not every invocation/mode/material, GPU images or complete preparation.
It closes the bounded numeric prerequisite in step 2 of the continuation plan.
Every original native call/draw still runs; no speedup is measured here.

The classifier also supplied 8,192 available comparisons with zero returned/stored
flag mismatches in these captures. All 18 native opaque scopes completed, with
no scope/preparation drops or abort. Shader/layout definitions and resource events
reported no drops; resource dependency coverage remains partial.

The four focus pairs contained 570/568, 569/569, 451/451 and 448/448 packets.
The first pair included two left-only meshes; per-eye order also differed. Focus
targets were 1589x1751 RGBA16_FLOAT with one sample and D32_FLOAT depth. Both eyes
reused the observed native banks, and the first later observed access after left
opaque group 31 was a group-32 draw before right focus began in every pair.
Thus the earlier native consumer-order constraint still applies.

Four supported outer camera comparisons were available (maximum matrix-entry
error 0.00018310546875), while focus camera prediction still refused its missing
projection input. The first capture additionally refused two mismatched-scene/view
pairings. Helper agreement uses the actual per-eye view and does not close the
early opposite-eye focus-camera producer contract.

Next work is the [native input/ownership integration contract](SPS-NATIVE-INTEGRATION.md).

## Sampled native CPU cost gate (2026-11-04)

Before implementing the owned producer, the research branch now provides
`-SpsCpuProfiling` / `LMU_QV_SPS_PROFILE=1`. Ctrl+Alt+F10 starts a 120-second
capture of verified opaque group-31 ranges. Every sixteenth scene frame is
sampled, alternating coarse group timing with detailed preparation, ordinary/
other transform, upload, draw API and Map/Unmap timing. Nested exclusive costs
plus the residual partition group wall time without double-counting subcalls.
Group thread-cycle counts remain cycles, separately from elapsed milliseconds.

The adapter observes one immediate context and the verified render thread/view;
unverified or nested ranges are excluded. It collects no native object snapshots,
shader reflection, mapped contents or GPU readbacks/queries. Detailed clock and
accounting overhead remains measurable: coarse controls retain the same hooks
and use different frames, so neither phase is an unhooked baseline or an SPS A/B.

Native entry/call-site/function-boundary guards are retained. All preparation,
helper, uploads and draws still execute. Heavy audit and other profilers are
excluded by the dedicated CLI launch. Default launches leave this mode disabled.
The prepared entry point reads selected working settings without modifying them;
the log analyzer reports per-group units, coverage and accounting consistency.

The full MSVC native suite passed, including deterministic nested accounting,
thread ownership, stale/invalid ranges, bounded capacity, cycle-query failure and
teardown fixtures. The WARP adapter fixture passed Map/Unmap/Draw forwarding,
nested attribution, FP-control preservation, exception completion and hook
removal. CLI configuration/diagnostics-off tests and four analyzer tests passed.
Compiled MSVC inspection confirms the transform hook's unrecognized-caller path
still changes only RSP/RBP after the original call before returning.

At implementation time, no live CPU cost capture, owned producer or performance
gain was established. See [run instructions and decision gate](SPS-CPU-PROFILING.md).

## Live CPU profile: coverage passes, attribution remains open (2026-11-04)

The `af50e53` package completed one 120-second VectorXR run. All four eyes supplied
317 coarse and 317 detailed opaque-group samples, with no drops/invalid ranges or
failed cycle queries. Coarse focus means were 0.424316 and 0.416565 ms/group.
Detailed focus means rose to 0.785400 and 0.780540 ms/group. The 85–87% difference
is too large to use detailed categories as reliable savings forecasts; phases use
different frames and do not provide an automatic exact overhead correction.

Detailed Draw API means were only 0.013590/0.013498 ms per focus group. Setup,
uploads and surrounding state work warrant investigation before assuming a large
benefit from fewer Draw calls. The native work-removal and measured-performance
gates remain open. The analyzer now separates large-control-gap warnings from
capture coverage, with a regression for complete coverage and excessive timing
perturbation. See the [results and next measurement gate](SPS-CPU-PROFILE-RESULTS-20261104.md).

## Implementation-first continuation: owned focus inputs (2026-11-04)

The user chose continued shared-preparation plus portable-instancing work.
Further profiling is deferred. `native_stereo_focus_inputs.hpp` now produces
both focus cameras before left focus rendering from the locked current XR frame
and the restricted native left-camera model. The XR bridge supplies owned flat
views plus session/frame/time/space/thread identity. Both ordinary eye paths can
consume prepared focus projections in order, with exact native depth/view/scene
checks and ordinary per-eye fallback. The predicted right world/view matrices
do not overwrite the native right camera or suppress its draws.

The opt-in `-SpsPreparation` / `LMU_QV_SPS_PREPARE=1` works with diagnostics off;
the development entry point uses normal gameplay without profiling hotkeys.
Default launches retain the existing eye path. Native history, metadata,
frustum/cache updates and all draw/preparation calls remain in place.

The full MSVC native suite and CLI tests passed. New camera-input tests passed at
both optimization levels, and XR snapshot lifecycle checks passed. A private
fixture exercised four existing captured frame pairs using recovered native
focus FOVs: projection differences zero; maximum current entry difference
0.000183105 and derived entry difference 0.000488281. This is bounded numeric
evidence, with no new live image-equivalence or performance claim.

The subsequent live VectorXR run consumed both prepared focus projections for
10,379 pairs, with zero refusals/fallbacks and clean teardown. This confirms the
runtime camera-input connection in that sampled mode, without establishing
image equivalence or a performance saving. See [implementation and remaining
contracts](SPS-FOCUS-INPUTS.md).

## Early owner/source description producer (2026-11-05)

Read-only code capture from the completed on-track run established the actual
owner traversal at `0x5e1140` and source emission at `0x5e0f10`. The downstream
classifier's `0x41c820` unwind fragment is not a standalone hook entry. The saved
capture is sufficient for continued implementation with the game closed.

`native_stereo_owner_inputs.hpp` copies full selected subtrees before their
admission, including right-only branches, ordered children, scalar states,
world matrices and bounded source/mesh descriptions. A frame collector shares
budgets, rejects overlapping roots and mask changes, and publishes only a sealed
selected-root subset. The planner takes two explicit view masks. Copied pointer
values are identity tokens; no native object, array or GPU content is retained.

The opt-in preparation path installs the hook only after prologue, executable
function-entry, four caller and mask/source-layout checks pass. Other callers
and recursive entries forward to native unchanged. No lock spans a native call;
no traversal, preparation, cleanup or draw is suppressed. The compiled hook
restores its arguments and tail-jumps to the original after FP restoration.

The full MSVC suite passed, with new producer fixtures at both optimization
levels for unfiltered trees, right-only admissions, immutable copied values,
strict limits, malformed inputs, allocation failures and maximum-depth trees.
Hook policy and forwarding tests passed. Live owner-producer validation remains
pending. Next establish root coverage, independent candidates/order, actual
constant/resource ownership, native side effects and target/consumer integration
for the paired backend. No additional timing capture is required for this stage.
See [owned descriptions and limits](SPS-OWNER-INPUTS.md).

## Live owner guard result and dependency-aware copying (2026-11-05)

The first owner-input build `b7397ae` ran through 2,844 focus pairs in VectorXR.
Camera inputs were consumed for both eyes without refusals/fallbacks. The owner
hook reached its capture path but refused every frame with `unsupported native
refresh/bounds path`; no owner frame or root was published. This is an uncovered
native domain, not an owner-equivalence or speedup result.

The producer now preserves complete descriptions with explicit pending world-
refresh and bounds-admission requirements. Parent refresh marks descendants'
world values pending. Stale world bits remain descriptions and cannot qualify
for admission planning; ready matrices still require finite values. Native
operations, arrays/resources and both draws remain unchanged. The immutable-tree
planner's rejection remains in force, so copied-input counts cannot be confused
with prepared or renderable coverage. New tests cover separate/combined flags,
propagation, stale NaN bits, finite ready inputs, attachment bypass, frame totals
and refusal to plan unresolved inputs. Resolving the native dependencies and
independent candidate/target integration remain the next implementation work.

## Regular owner bounds admission and collector separation (2026-11-05)

The next `689ed43` live run copied 89 forests with no refusals: 99,146 root
observations, 92,382 ordinary-input root observations, 3,293 world-refresh owner
observations and 3,471 bounds-admission owner observations. The first forest had
1,114 roots. Both focus projections were consumed for all 89 pairs. The user
reported extremely poor CPU frametimes. This collector duplicated tree reads,
per-field memory validation and allocations every scene frame without removing
native work. The user requested continued implementation rather than another
collector test or a replacement package.

`native_stereo_owner_bounds.hpp` now implements the regular null-source bounds
path on owned values: distance equality, ordered six-plane sphere/intersection
checks and the subsequent eight-corner rejection gate. It preserves ordered flag
writes and sticky sign flags. The traversal planner applies those results with
independent eye inputs, preserves right-only subtrees and executes the native
final parent-null clear on bounds rejection while leaving mask rejection alone.
Pending world refresh and transformed-plane paths still refuse. The actual
native helper center and both complete per-eye bounds views must be provided;
the raw collector does not guess them from object origins or XR poses. No native
preparation/draw is suppressed and native equivalence remains unestablished.

The full regression build includes optimized/unoptimized bounds and traversal
fixtures for distance/plane boundaries, sphere-before-corner rejection, ordered
state changes, right-only trees, attachment bypass, invalid inputs/arithmetic,
allocation failure and FP preservation. CLI isolation tests verify camera
preparation does not enable heavy collection, stale environment variables are
cleared and diagnostics-off disables explicit owner capture.

In source, `-SpsPreparation` is camera preparation only. `-SpsOwnerInputs` is a
separate opt-in with diagnostics enabled; a valid frame consumes its single-frame
allowance even if copying refuses, with no repeated retry. The owner hook is not
installed by ordinary preparation. This bounds a diagnostic; it does not make
the collector a production SPS preparation algorithm. No new package or user
capture is requested for this step. See [bounds contract](SPS-OWNER-BOUNDS.md).

The capture body is outlined from the thin owner hook so larger owned data
structures cannot add stack-cookie or object cleanup after native returns.
The final MSVC suite passed, and compiled forwarding was checked to restore
arguments and tail-jump to the original after capture/FP cleanup. DLL load/export
smoke and CLI isolation passed. No live bounds equivalence or speedup is claimed.
