# Native SPS input and scheduling contract

The [owned scene preparation and GPU backend](SPS-OWNED-BACKEND.md) now implements
independent classification/order, resource snapshots, paired constant uploads,
ordered mono/paired execution and per-eye target bridging. WARP and local GPU
tests pass. These components do not yet suppress LMU's original passes; the
native producer and effect/consumer boundaries below remain required.

The bounded ordinary-transform numeric prerequisite has passed on sampled native
VectorXR calls. Step 3 now supplies an early owned focus-camera pair and copies
unfiltered owner/source descriptions from verified selected-root entries. The
camera projection consumer passed live VectorXR runs. The revised description
collector copied 89 frames without refusals, but duplicated native work and
caused a severe CPU regression reported by the user. It is now a separate,
explicit single-frame diagnostic in source; further collector testing is deferred.
The owned planner now implements regular per-eye bounds admission from explicit
owned center/corner and view inputs. Producing/validating those complete native
inputs and resolving world-refresh effects remain open. Full draw-input
ownership and native consumer scheduling remain open. No native preparation or
draws are suppressed.

## Observed scheduling constraint

```mermaid
flowchart LR
  OL[Outer left] --> FL[Focus left opaque group 31]
  FL --> CL[Left group 32 and later consumers]
  CL --> OR[Outer right]
  OR --> FR[Focus right opaque group 31]
  FR --> CR[Right consumers]
```

Both focus eyes reuse observed native color/depth banks. The next group accesses
those banks before the right focus pass. Retaining the final XR image or delaying
only left opaque draws does not preserve the native chain. The current backend
renders into two-slice arrays; compatibility with native target views, initial
contents and downstream consumers must be established before substituting it.

Investigate producing both eyes' inputs before the left opaque range. Replaying
deferred consumers would require owning their full state/resource chain and
accounting for its copies and synchronization. Neither approach is implemented.

## Contracts that remain open

| Input or effect | Existing evidence | Requirement for replacement |
| --- | --- | --- |
| Current per-eye camera data | Early owned current-frame focus pair; both projection consumers used it for 10,379 VectorXR pairs with no fallbacks | Validate remaining native world position/metadata/history and broader camera modes. Predicted right matrices do not overwrite the native view. |
| Owner admission | Owned tree planner with regular distance/sphere/corner gates and verified pre-admission selected-root collection, including right-only subtrees | Supply/validate native bounds centers and both owned bounds views; resolve world refresh, complete root coverage and intervening mutations. Both masks and bounds views are independent. |
| Candidate membership/order | Numeric classifier agreement and late ordered packet observations | Independently valid candidates and sorting for each eye. Neither eye's visible list nor matching draw indices represents the other eye. |
| Source/mesh lifetime | Owned bounded source/mesh descriptions copied at the early owner entry | Own the actual draw payloads and resources. Description fields and identity tokens do not retain mutable native arrays or objects. |
| Draw inputs | Shader proof, binding signatures and resource-use observations | Owned constant payloads and legal lifetimes/content versions for shaders, geometry and material resources. COM references preserve object lifetime, not arbitrary mutable resource contents. |
| Preparation effects | Selected 52-float helper output validated | Remaining shader/material/bone callbacks, PS constants, global derived data, native caches, counters and traversal/cleanup. Do not bypass the whole preparation wrapper or world sort. |
| Native targets/consumers | Reused banks, later group draws and resolve/read traces | Correct per-eye initial HDR/depth state and legal consumer views. Count any copies/resolves needed for array-to-native integration; two private images alone do not solve it. |
| Fallback and teardown | Existing owner-thread/frame/image-lease fixtures | Preflight before suppressing native work or changing visible output; complete native fallback, abort/transition handling and no stale borrowed inputs. |

## First implementation deliverable

The user selected implementation-first continuation: shared preparation followed
by portable stereo instancing. The rough CPU capture is informational. Further
profiling is deferred and is not a prerequisite for implementing the producer.
Correctness, complete fallback and eventual end-to-end performance remain
acceptance checks. A CPU-only intermediate need not independently meet the final
performance target before the paired-submission stage is implemented.

The first runtime component produces [owned focus camera inputs](SPS-FOCUS-INPUTS.md)
before left focus rendering. It includes both actual XR focus FOVs and a restricted
right camera prediction. The ordinary eye path consumes prepared projections when
the frame/scene/view/order/native-depth checks match. Native world position,
history and complete preparation effects remain open. The next component now
produces [owned owner/source/mesh descriptions](SPS-OWNER-INPUTS.md) at verified
native root traversal entries before admission. It seals a bounded selected-root
subset after left focus, with all native work retained.

Continue the restricted **owned focus input producer** toward independent
candidates and draw payloads, checking equivalence against original traversal.

1. Identify and verify the native owner/source builder's entry, exit and cleanup
   boundaries before either eye's admission/filtering. Reuse existing read-only
   code evidence. The selected owner boundary is now verified at `0x5e1140`;
   source emission starts at `0x5e0f10`. The original classifier call site is
   downstream evidence. Its `0x41c820` unwind fragment is not a function entry.
2. Define exactly which values can be copied and which objects/resources require
   native retention. The runtime producer now supplies entirely copied selected
   trees and descriptions to that interface, without retaining native objects.
   Whole-scene coverage and mutable draw-resource ownership remain unestablished.
3. Keep ordered membership, per-eye distances and shared state transitions.
   Record refresh/bounds dependencies in the description producer. Regular bounds
   now have an owned planner implementation, requiring complete native-equivalent
   inputs. Keep unimplemented world refresh and special/callback paths native.
   Never call engine methods on fabricated copied objects.
4. Compare the produced admissions and ordered candidates with both original
   native traversals for the same frame/scene/view identities. Include left-only
   and right-only objects, allocation/refusal paths and frame transitions. Keep
   every original native call and draw during this check.
5. Establish target seeding/consumer compatibility and remaining preparation
   effects before a paired replacement. A shared CPU preparation path with
   separate eye draws remains a valid intermediate result if its total measured
   cost improves; it is not completed GPU single-pass stereo.

Late group-31 bucket/item vectors have already undergone native filtering and
may have undergone cleanup. The source-containment diagnostic deliberately does
not scan the reported native mesh array. Do not extend that late reader into a
producer by assuming those vectors are still valid or unfiltered.

The producer and scheduler must count their snapshots, allocations, copies,
retention, restoration and synchronization costs. Native work removed and net
CPU/GPU/P95/P99 benefit are acceptance results, not consequences of matrix
agreement. Broader camera/material/runtime and AMD/Intel coverage also remain.
