# Native SPS input and scheduling contract

The bounded ordinary-transform numeric prerequisite has passed on sampled native
VectorXR calls. Step 3 now needs a valid producer of both focus eyes' inputs and
a scheduling boundary before their native consumers. The tested helper still
uses the real view for each eye at its normal time; it does not supply the right
eye early. No native work may be skipped based on this numeric result alone.

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
| Current per-eye camera data | Restricted outer camera prediction; actual-view helper output matches | Current-frame focus poses/projections and depth coefficients for both eyes before the selected range. Preserve rendered-view metadata and native history; do not substitute an outer FOV or previous-frame view. |
| Owner admission | Owned tree planner with left-then-right scalar transitions | A verified native producer before either eye's filtering, including right-only subtrees and any intervening mutations excluded by the planner's immutable-tree contract. |
| Candidate membership/order | Numeric classifier agreement and late ordered packet observations | Independently valid candidates and sorting for each eye. Neither eye's visible list nor matching draw indices represents the other eye. |
| Source/mesh lifetime | Geometry-reported array containment and bounded scalar copies | An actual ownership or complete-copy contract. Containment, retaining a bucket/item, or saving an array address does not retain its contents. |
| Draw inputs | Shader proof, binding signatures and resource-use observations | Owned constant payloads and legal lifetimes/content versions for shaders, geometry and material resources. COM references preserve object lifetime, not arbitrary mutable resource contents. |
| Preparation effects | Selected 52-float helper output validated | Remaining shader/material/bone callbacks, PS constants, global derived data, native caches, counters and traversal/cleanup. Do not bypass the whole preparation wrapper or world sort. |
| Native targets/consumers | Reused banks, later group draws and resolve/read traces | Correct per-eye initial HDR/depth state and legal consumer views. Count any copies/resolves needed for array-to-native integration; two private images alone do not solve it. |
| Fallback and teardown | Existing owner-thread/frame/image-lease fixtures | Preflight before suppressing native work or changing visible output; complete native fallback, abort/transition handling and no stale borrowed inputs. |

## First implementation deliverable

Before the replacement producer, collect the [sampled CPU cost breakdown](SPS-CPU-PROFILING.md)
with the heavy audit disabled and compare coarse/detailed controls. Establish
whether remaining preparation/submission cost and target/input feasibility justify
the route. A neutral CPU-only intermediate does not rule out a benefit from paired
submissions; its copying/ownership cost still belongs in the final comparison.

Establish a restricted **owned focus input producer** and an equivalence check
against the original traversal. This is the first component needed by the
schedule, rather than another numeric transform implementation.

1. Identify and verify the native owner/source builder's entry, exit and cleanup
   boundaries before either eye's admission/filtering. Reuse existing read-only
   code evidence. The original classifier call site is downstream evidence, not
   proof that the whole unfiltered owner tree is available there.
2. Define exactly which values can be copied and which objects/resources require
   native retention. The existing owner planner consumes an entirely owned tree;
   no runtime producer currently establishes that contract.
3. Keep ordered membership, per-eye distances and shared state transitions.
   Explicitly reject unsupported refresh/special/callback paths before producing
   a partial plan. Never call engine methods on fabricated copied objects.
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
