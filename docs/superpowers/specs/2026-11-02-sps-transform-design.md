# Owned native transform preparation for SPS 0.2.0

The user authorized continuing the limited SPS integration work. This machine
has no connected Pimax. Implement and test a bounded preparation component and
its live comparison diagnostic; no headset is needed to build or run fixtures.
Actual agreement with LMU and performance remain a separate headset-PC test.

## Approach

Replacing native preparation now would discard unmodeled material/cache effects.
Deferring native draws would violate the known consumer order. Instead, produce
owned transform constants and compare them around the existing native preparation
call, while preserving every original call and draw. This closes one concrete
numeric input contract rather than claiming a complete scheduler.

Use the existing C++17 implementation, pinned native image and opt-in audit.
The first comparison domain is deliberately small: effective preparation flags
exactly 1, reset false, enabled shader (+8), nonnull mesh/geometry/transform owner,
no mesh callback and no mesh orientation flag 0x100000. The flags force ordinary
transform mode and exclude later preparation helpers. These restrictions do not
establish that geometry is static or safe to share between eyes.

## Owned calculation

Take a copied 4x4 world matrix, per-eye derived matrix, view matrix, eye position,
and the mesh's local-transform bit 0x8000. For each eye return VS registers 0..3
(derived or world*derived), 4..7 (world), 8 (position xyz, zero w), and 16..19
(view or world*view). Do not overwrite the unused registers 9..15. Matrices use
the native row-major layout. Multiply with the native pairwise sum of four
products, no contraction/reassociation; reject nonfinite inputs/intermediates.
Return no partial payload on failure. Successful calculation is not rendering
eligibility. No pointers, native writes, D3D calls, allocations, or caches in the
numeric model. Two eyes are independent invocations on owned inputs.

## Capture and comparison

Read only inside an active owner-thread group-31 audit and the existing native
preparation hook. Separate transform begin/end APIs preserve older callers.
Capture before the original: bounded scalar flags, selected world at owner+0x298,
view matrix at view+0x1dc, position at +0x220, derived matrix at +0x3a0.
Capture afterward: the same inputs and native VS shadow registers at image RVA
0x1b13630. Require stable inputs and supported entry/post MXCSR controls before
numeric comparison. Preserve MXCSR around diagnostic bookkeeping. Evaluate math
only during export under a restored default environment.

At most 128 attempts per frame/eye/pass slot (8 slots, 1024 records); count drops
and refusals. Select the expected main view before consuming the budget. Each
input snapshot reads at most 244 bytes; input/output/input totals at most 696.
Export reason, bit-exact matched/mismatched status, compared scalar count and
first differing register/component; never export native addresses.
Require the same capture generation and scope epoch when ending a token.
Missing/changed/unreadable inputs or an unclosed invocation
must not be reported as agreement. Every result stays scheduling-ineligible.
Native source files/captures remain private; publish only implementation, owned
fixtures and factual findings. Leave release VERSION and main unchanged.

## Acceptance

Independent literal matrix examples, asymmetric eyes, cancellation that separates
pairwise from serial addition, nonfinite/overflow and input-lifetime tests.
Capture fixtures cover offsets, callback/domain refusal, malformed/unreadable
pointers, byte bounds, changed inputs, output mismatch and original-call FP state.
Audit tests cover opt-out/idle/wrong-thread, bounded attempts, stale/duplicate end
and clean JSON. Run focused tests at O0/O2 and the native suite; report live
validation as pending, never infer it from synthetic passing results.
