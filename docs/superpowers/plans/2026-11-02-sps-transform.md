# SPS owned transform implementation plan

> **For agentic workers:** Use superpowers:executing-plans.

**Goal:** Own and compare the ordinary native transform constant calculation.
**Architecture:** Pure numeric payload, bounded before/after snapshot, opt-in audit
integration around original preparation. Never replace native work in this step.
**Tech Stack:** C++17, Windows D3D11 fixtures, PowerShell native build.
**Spec:** ../specs/2026-11-02-sps-transform-design.md

## Global constraints

- Preserve every native call/draw and native FP environment; keep VERSION unchanged.
- No retained game resources or public native addresses; no performance claim.
- 128 transform attempts per frame/pass slot; explicit refusal/drop/incomplete status.
- Strict finite numeric model, native pairwise arithmetic, owned outputs only.

## Review focus

- Per-eye matrix layout and pairwise float rounding; asymmetric/cancellation fixtures.
- Unsupported flags and missing inputs must not become matches.
- Output lifetime must outlive source overwrites; before/after drift must refuse.
- Idle/disabled/wrong-thread must perform no reads; cap before any native read.
- Stale/duplicate/end-after-stop tokens must never read released native memory.

### Task 1: Calculation and snapshot/comparison

Create mod/native_stereo_transform.hpp and mod/tests/native_stereo_transform_test.cpp.
Expose NativeStereoTransformInput/Result and prepare_native_stereo_transform;
capture_native_stereo_transform and compare_native_stereo_transform operate on
owned snapshots and explicitly supplied bounded native addresses/read callback.

- [x] Write/run failing analytic and memory-fixture tests.
- [x] Implement finite model and bounded read/comparison; no native mutations.
- [x] Run O0/O2 tests, including unsupported controls and FP preservation.

### Task 2: Diagnostic integration and evidence

Modify mod/stereo_audit.hpp/.cpp, mod/engine_bridge.cpp,
mod/tests/native_stereo_transform_audit_test.cpp and mod/build.ps1. Add the tests
to MSVC too, with strict FP mode shared by tests and the final DLL.

- [x] Add failing audit tests for output mismatch, caps, lifecycle and opt-out.
- [x] Add separate transform begin/end APIs, capture around existing preparation,
      compare at export and serialize bounded results.
- [x] Run focused audit and full native suite; independently review changes.
- [x] Record source findings, actual test results and the headset-PC next step
      in docs/SPS-DEVELOPMENT.md; commit only this task's files locally.
