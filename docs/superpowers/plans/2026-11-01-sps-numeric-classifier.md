# Owned native classifier model implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. This task builds an offline numeric model, not an enabled optimization.

**Goal:** Reproduce a restricted native distance/frustum classifier on owned finite numbers without mutating engine objects.

**Architecture:** A pure C++17 helper models the observed 0x5306A0 branch order for plane counts 0..4 and normal distance testing. Its output owns both returned flags and stored flags, with explicit refusal status. Standalone analytic tests establish the model; later same-invocation native comparisons remain required.

**Tech stack:** C++17 scalar float arithmetic, existing native test style.

**Spec:** Inline design under autonomous SPS authorization. Read-only mapped inspection verified classifier 0x5306A0 and distance accessors 0x530100/0x530130 in the pinned executable. This is a mathematical prerequisite for independent eye preparation, not all visibility logic or a source lifetime contract.

## Global constraints

- VERSION remains 0.1.0. No integration into game rendering, native memory reads/calls, hooks, scheduling or draw suppression.
- All inputs are owned scalars/arrays. No pointer to a native object, no reconstructed engine object, no external dependency.
- Support only planeCount 0..4 and skipDistanceBounds=false. Refuse other modes rather than approximate.
- Validate all scalar/vector inputs and planes[0..planeCount) upfront for finiteness, even if an early native branch would bypass some planes; ignore unused planes beyond planeCount. Require nonnegative radius and near coefficient, positive far coefficient, ownerScale/viewScale positive, and nonnegative bias. Require effective near < effective far after final float multiplication by viewScale, refusing underflow collapse. This is the helper's conservative restricted domain, not a claim the native engine forbids other values. Compute later plane arithmetic only if the branch reaches it.
- Reject nonfinite intermediate arithmetic, including overflow in distance squares, dot products or scaled thresholds. Preserve unrelated flag bits and native inequality/early-exit order. Disable floating-point contraction for this helper in the tested toolchain; do not use fast-math.
- Output availability/reason plus returnedFlags and storedCandidateFlags. Unavailable output must not be mistaken for a classification. Never label a result scheduling-eligible or fully visible/invisible.

## Exact restricted algorithm

Inputs: uint32 candidateFlags and ownerFlags; float ownerScale, nearCoefficient,
farCoefficient, radius; int32 planeCount; float3 position, viewPosition;
float viewScale, bias; bool skipDistanceBounds; array of four float4 planes.
These are already copied numbers. Future capture must copy position from the
classifier's R8 pointer (normally the pointer stored at candidate+0x110), not
substitute inline candidate+0xEC coordinates used by a different sorting helper.

1. F = candidateFlags & 0xFFF8FFFF; storedCandidateFlags starts F.
2. dx/dy/dz = position - viewPosition; distance = sqrt((dy*dy + dx*dx) + dz*dz) in scalar float operation order. D = max(distance, viewScale*bias).
3. far = farCoefficient when ((F & 1) && (ownerFlags & 4)), otherwise ownerScale*farCoefficient. near = ownerScale*nearCoefficient. Apply the explicit restricted-domain checks above. If D > far*viewScale, store/return F|0x50000. Else if D <= near*viewScale, store/return F|0x50000.
4. If ownerFlags interpreted as int32 is negative, or planeCount==0, return/store F.
5. Visit planes in order. dot = (position.y*plane.y + position.x*plane.x) + (position.z*plane.z + plane.w), in scalar float order. If dot > radius, store/return F|0x40000. Else if dot > -radius, store/return F|0x20000 IMMEDIATELY. Otherwise continue. Exhaustion returns/stores F.

The first intersecting plane ends the native loop even if a later plane would
reject. Model this observed behavior; do not replace it with a conventional
aggregate frustum test. Plane counts above four include separate special-plane
behavior and distinct returned/stored flags, deliberately unsupported here.

## Review focus

- Strict far and inclusive near comparisons at exact boundaries.
- First-intersection early return versus a later outside plane.
- Negative owner flags bypass plane work after distance checks.
- Candidate/owner special far-threshold flags and unrelated flag preservation.
- NaN/Inf and finite-input overflow cannot become an available result.

### Task 1: Numeric helper and analytic tests

**Files:** Add mod/native_stereo_classify.hpp and mod/tests/native_stereo_classify_test.cpp only. Do not edit mod/build.ps1 while the separate trigger task is building. Root will integrate the test after review. No other production edits.

- [x] Define the owned input/result types and write failing standalone analytic tests before implementation. Cover known inside/outside/intersection positions, plane order, exact near/far/radius boundaries, special far scaling, owner-sign bypass, unrelated flags, invalid modes/domain, every used nonfinite input and intermediate overflow, input immutability and deterministic repeated evaluation.
- [x] Implement the minimal helper, explicitly readable scalar operation order and documented restricted domain. Make tests pass with standard strict warning flags and contraction disabled. Test at both O0 and O2 to catch optimization-sensitive behavior; no full native suite needed for this disconnected helper.
- [x] Self-review and report exact commands/results and numerical assumptions to root. No game actions, native-memory access, commits, pushes or subagents.

### Task 2: Review and future differential validation

- [x] Independently review the owned helper and tests against the verified restricted contract.
- [x] Add the standalone fixture to the native build after the separate trigger task completes; document that native differential validation and live integration remain pending.
- [ ] Develop a subsequent bounded capture at the original classification invocation to compare matching inputs/results. Group31 accepted-source observations alone cannot prove the earlier native call's return or right-eye completeness. Do not enable the helper in rendering from synthetic tests alone.
