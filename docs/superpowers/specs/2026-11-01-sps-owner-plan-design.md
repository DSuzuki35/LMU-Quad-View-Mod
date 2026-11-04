# Owned stereo owner traversal

Build the next reusable SPS preparation component from the now-understood owner
admission rules. A left-visible list cannot represent right-only subtrees.
The component accepts an entirely owned tree from before either eye's filtering
and produces ordered owner admissions and unfiltered source references for each
eye. It does not produce drawable packets or call the game.

This is a controller-selected design under the user's autonomous-work request.
An early native scheduler remains unsuitable: source lifetime, candidate-wrapper
effects, material preparation and right initial targets are not established.
Another generic capture would not close these contracts. A pure owner planner
lets us implement the known portion without pretending the missing parts exist.

Use C++17 and existing native test conventions. Keep the component separate from
the numeric candidate classifier: owner admission is not candidate visibility.
Its inputs contain scalar owner state, opaque numeric source IDs, ordered child
indices, two view masks/null-parent conditions and a global mask. No native
pointers, callbacks, resources, memory readers or runtime producer are added.

The supported contract is an immutable owned rooted tree with no intervening
candidate/consumer mutation of the modeled owner fields. That contract is NOT
established for LMU integration. Execute a complete left DFS followed by a complete
right DFS on the same owned mutable state; two independently initialized copies
would lose native cross-eye mutation order. Keep ordered transition records and
per-eye source order, including right-only subtrees. Do not sort by distance.

At each visited owner: if its attachment is null, copy pending value to current
value when unequal, even before rejection. Then reject the eye's whole subtree
for flag 0x20000 or failure of all-bits inclusion of owner mask in global and view
masks. If attachment is null, refuse unsupported refresh/special branches
(0x400000/0x80000), otherwise set flag 0x10000. Emit admission and the owner's
source IDs, recurse in child order, then clear flag 0x40000000 when the eye's view
parent is null. Early rejection skips that final clear. Nonnull attachment skips
the null-attachment predicate and refresh. Preserve other flag bits.

For an intentionally conservative first domain, reject any null-attachment owner
with unsupported flags anywhere in the supplied tree, including rejected branches.
Reject malformed structure, cycles, shared children, duplicate child references,
unreachable nodes, invalid root/indices and duplicate source IDs. Require 1..4096
nodes and at most 65536 source references in total. Validate bounds and structure
before output creation, avoid recursion proportional to input depth, and return
no partial plan on refusal or allocation failure. These are planner bounds, not
claims about LMU limits. Inputs stay unchanged.

Success means only that the supplied owner traversal is modeled. Every result
must explicitly report rendering eligibility false. The output owns source IDs,
per-eye admission/source order, final shared owner state and ordered symbolic
state transitions. It never applies native writes or substitutes a source ID for
owned draw inputs. VERSION remains 0.1.0 and no runtime switch is introduced.

Tests must independently pin right-only parent masks, all-bits inclusion, shared
left/right mutation ordering, pre-rejection assignments, post-child flag clears,
attachment bypass, domain refusals, malformed graphs, maximum-depth bounds and
input immutability. Compile at O0 and O2 with project warnings-as-errors. Add the
tests to the native build and run the complete suite once after focused passes.
