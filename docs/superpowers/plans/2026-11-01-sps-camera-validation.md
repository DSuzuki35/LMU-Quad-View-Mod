# Native stereo camera validation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. This is one prerequisite within unfinished SPS work, not the 0.1.1 release plan.

**Goal:** Compare a restricted numeric opposite-eye prediction with LMU's native right-eye camera in the same captured frame.

**Architecture:** Extend the existing bounded, opt-in F8 native pass audit with owned numeric camera snapshots. Predict from the left snapshot, compare with the right snapshot in the same frame and pass kind, and report errors without suppressing native work.

**Tech stack:** Existing C++17 native audit and WARP fixtures.

**Spec:** This document records the diagnostic design under the user's standing instruction to develop and test SPS autonomously. Full scheduling and native object ownership remain outside this task.

## Global constraints

- Diagnostics only: preserve all native draws, preparation, resource bindings and camera history. Never invoke native methods on copied objects.
- Keep VERSION at 0.1.0; no production SPS or performance claim. Always serialize scheduling_eligible=false.
- Capture only during the existing bounded F8 audit on its owner thread. Disabled or idle audit performs no new native memory reads or matrix calculations.
- Retain only copied numbers, no native object references, raw pointers, game code or shaders in output or git.
- Finite values and explicit refusal reasons are required; invalid/missing input cannot serialize NaN/Inf or appear as a successful comparison.

## Review focus

Missing/unreadable buffers must refuse before dereference; changed payloads and different frames must not pair; child views and duplicate same-eye scopes must not be paired arbitrarily; non-rigid/canted/unsupported camera paths must refuse; diagnostics disabled must not read these buffers.

### Task 1: Owned camera observations and restricted comparison

**Files:** Add mod/native_stereo_camera.hpp and mod/tests/native_stereo_camera_test.cpp. Modify mod/stereo_audit.hpp, mod/stereo_audit.cpp, mod/engine_bridge.cpp, mod/tests/stereo_audit_test.cpp and mod/build.ps1. Do not edit unrelated documentation or VERSION.

**Interfaces and data:** Append optional raw input addresses to StereoNativePassInfo without breaking current aggregate initializers. Engine bridge supplies native VrCameraState at module+0x1B1BE30, int camera mode at +0x1A51400, and pointer-to-current-view at +0x1A51370; capture verifies the current main-view pointer equals info.view. Reads occur inside the existing active group-31 audit gate. Use its readable predicate and bounded copying with overflow/null rejection. Snapshot owns numbers only. Native layout is pinned by prototype/lmu_reconstructed.hpp; do not instantiate its structures as engine objects.

Copy view flags, eye index, transform mode (view+0x21C), current matrix (+0x1DC), projection (+0x360), far projection (+0x58C), derived matrix (+0x3A0), view translation offset (+0xC4, three floats), extra Euler offset (+0xD0, three floats). Copy both relative-eye matrices from VrCameraState (+0x70/+0xDC), both engine projections (+0x148/+0x1B4), both far projections (+0x220/+0x28C), and both six-float camera scalar blocks (+0x3E0/+0x3F8). All these numbers must be finite.

Prediction is a hypothesis, never native eligibility: row-major C_right = C_left * E_right * inverse(E_left). Use an owned rigid affine inverse; require finite affine orthonormal matrices with determinant near +1 (tolerance 1e-4), equal per-eye 3x3 orientation and first three scalar inputs (tolerance 1e-6), camera mode 0, transform mode 1, flags excluding 0x01000000 and 0xC0000000, and zero view translation/extra Euler offsets. Right projection/far preserve native left depth convention and replace only FOV coefficients using existing math conventions. Predicted derived = predicted current * predicted projection. Do not set a pass/fail error tolerance from asynchronous samples; report maximum absolute errors for current, projection, far projection and derived, plus a reason if comparison is unavailable.

Pairing uses existing capture generation/frame (slot/4), outer versus focus, and captured scene/view identity. Accept exactly one main group-31 snapshot per eye in a candidate pair; duplicates invalidate that pair. Both snapshots must be complete and pass restrictions; the copied native camera payload numbers must match exactly between eyes. Report missing right, mismatched payload, unsupported path, duplicates or incomplete snapshots explicitly. Results may be computed when writing the finished audit from owned copies; comparison uses same-frame right state captured before that group's consumers. Never compare across captures or frames. Restrict numeric prediction to outer pairs: this native payload contains outer FOVs, while focus FOVs come from separate OpenXR frame views. Focus snapshots remain observations; their comparison explicitly refuses with missing focus projection input. Do not add a new OpenXR capture interface in this task.

- [x] Write failing unit tests for analytic translated/rotated rigid inputs, matrix order, asymmetric projections and retained depth, derived products, and every refusal condition above. Exercise source copy ownership, unreadable/null inputs and finite validation with synthetic buffers. Record the intended failing assertion before implementation.
- [x] Implement the bounded snapshot and numeric comparison helper with readable names/comments. Serialize snapshots and comparison errors in the existing native audit JSON; unavailable numeric results are null. Keep raw numbers round-trip precise.
- [x] Extend WARP integration fixture with synthetic valid current-view/camera inputs; verify JSON contains finite copied camera values and comparison result, rejects invalid input, and master-off path does no camera reads. Preserve original draw/resolve behavior checks.
- [x] Run focused tests then mod/build.ps1 once for this changed production audit. Root runs the live capture after review. Record tests, refusals and limitations in the task report; leave commit to root.

### Task 2: Live validation and evidence

- [x] Review Task 1 diff and test evidence independently.
- [x] Package a separate diagnostic directory with hashes, retaining user rendering settings, and launch offline LMU.
- [x] Capture outer same-frame pairs (focus if runtime is awake); report actual errors/refusals. Never substitute asynchronous samples or claim headset visual validation from standby.
- [x] Close LMU after capture and update SPS development evidence. Native scheduling remains unfinished.

Live result: five two-frame captures, ten available outer comparisons; projection
and far-projection errors zero, maximum current and derived errors both
6.103515625e-5. Focus observations refused comparison as designed. No native
scope drops or diagnostic aborts. LMU exited normally after capture. Commit and
push this reviewed prerequisite to codex/sps-0.1.1; retain VERSION 0.1.0.
