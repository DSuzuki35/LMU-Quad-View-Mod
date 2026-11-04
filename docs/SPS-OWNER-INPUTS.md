# Native owner/source inputs for SPS

This stage connects the verified native owner traversal to an owned input
producer. It supports the shared-preparation plus portable-instancing path.
Further timing work remains deferred.

## Verified native boundary

Read-only inspection of the running pinned executable identified the recursive
owner traversal at `0x5e1140`. Its arguments are owner, view, bucket collection
and float bias. It updates pending/current state, applies owner flags and the
all-bits mask gate, handles restricted refresh/bounds paths, emits sources and
then traverses children. The source-emission entry is `0x5e0f10`.

| Input | Native field |
| --- | --- |
| Owner flags / admission mask | `+0` / `+0x74` |
| Attachment | `+0x80` |
| Source geometry vector | `+0x88` |
| Ordered child vector | `+0xa0` |
| World matrix | `+0x298` |
| Pending / current state | `+0x3d8` / `+0x3e0` |
| View mask / parent | `+0x10c` / `+0x4b0` |
| Global mask | RVA `0x1b12c28` |
| Source owner / lazy state | `+0x130` / `+0x138` |
| Source mesh count / array | `+0x168` / `+0x170`, record stride `0xe0` |

The classifier caller at `0x41c8af` is downstream. Its unwind segment beginning
at `0x41c820` starts within an already executing function, with live registers;
it is not a suitable standalone producer entry. No hook is installed there.

The new traversal hook requires the complete selected prologue, a function-start
check, four verified external call sites, the mask helper's field layout and
the source-emission call. Unrelated/recursive callers forward to the original
unchanged. Root capture is restricted to the expected left focus view, frame,
owner thread and `scene+0x70` bucket collection. No mutex spans the original call.

## Owned output

`capture_native_stereo_owner_inputs` copies a selected root's entire unfiltered
tree before that root's admission. It follows ordered children even when the
left eye would reject the parent. It owns scalar states, world matrices, source
descriptions and mesh descriptions; copied addresses are identity tokens only.
It never calls native refresh, retain, classifier, callback or cleanup methods.

The producer validates pointer spans/overflow, vector headers, tree structure,
source-to-owner correspondence and mesh record bounds. Its maximums are 4,096
owners, 65,536 source references, 32,768 mesh descriptions and 8 MiB of attempted
native reads. Invalid structure/read/allocation failure returns empty owned
payloads. These are admitted-domain limits, not engine limits.

Description completeness and admission readiness are separate. Null-attachment
flag `0x400000` records pending native world refresh; it also marks descendants'
world values pending because the native refresh traverses children. Flag
`0x80000` records a pending native bounds-admission path. Attachment bypass is
preserved. Dirty world bytes may be copied but are explicitly stale; they are
never treated as numeric prepared transforms. Ready world matrices must remain
finite. Source/mesh descriptions are values at capture time, not post-refresh
payloads. The snapshot does not perform or simulate either native operation.

The frame collector shares those budgets across selected roots and rejects
overlapping trees or changed view/global masks. It publishes after the left
focus scope completes and seals successfully, never as a partially built forest.
This is a selected-root subset, not proof of complete scene/root coverage.
The existing owner planner accepts a ready copied tree with two explicitly
supplied view masks, preserving left/right admissions, right-only branches and
scalar transition order. It refuses snapshots with any pending refresh/bounds
dependency. It does not silently assume both eyes use the left mask.

## First live run and continuation

The `b7397ae` VectorXR run completed 2,844 focus pairs, with both prepared camera
projections consumed and zero camera fallbacks. The new owner hook ran, but its
conservative whole-tree refresh/bounds guard refused all 2,844 frames. Zero
owner frames/roots were published. This proves the hook reaches the path; it
does not validate owner inputs or demonstrate SPS/performance gains.

The revised producer collects descriptions for those trees and records their
native requirements instead of discarding them solely for their flags. This
extends snapshot collection, not the rendering planner's supported domain.
Readiness and shutdown messages distinguish copied roots, admission-ready roots,
world-refresh owners and bounds-admission owners. Shutdown totals count repeated
frame observations, not unique objects. The revision still needs live validation.

## Remaining work

The full MSVC native regression suite passed. New producer tests passed at
`/Od` and `/O2`, including right-only branches, strict count/read limits,
malformed pointers/vectors, allocation failure at each allocation, a 4,096-level
tree, no native mutation and frame sealing. Hook tests cover changed native
anchors and disabled/unknown-caller forwarding. Compiled forwarding restores
the original arguments and tail-jumps to the native function after the input
guard has restored floating-point state. This is static/synthetic evidence;
the revised owner producer has not yet been run in the game. Additional fixtures
cover the two requirements separately/together, inherited refresh requirements,
stale NaN bit preservation, ready-matrix refusal, attachment bypass and planner
refusal until native dependencies are resolved.

The descriptions do not retain native mesh arrays, geometry/material objects,
shader resources or mutable GPU contents. A copied callback address is not a
frozen callback result. Concurrent changes/coherence and intervening native
candidate/cache effects are not established by copying. Both the producer and
planner therefore remain ineligible for native draw/preparation suppression.

Next resolve the recorded native refresh/bounds dependencies, validate selected-root
coverage and connect independent candidate sorting,
owned constant payloads and resource versions/lifetimes. Establish complete
native side effects and legal HDR/depth target seeding/consumers, then connect
the paired D3D11 backend. Native traversal, cleanup and both eye draws remain
active through this input stage. Live correctness validation of the new owner
producer is still required; no performance capture is needed for that check.
