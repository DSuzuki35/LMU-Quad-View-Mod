# SPS multisample backend validation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Establish whether the portable paired-draw backend preserves individual color/depth samples with MSAA before integrating it into LMU.

**Architecture:** Extend the owned offscreen stereo fixture with multisampled two-slice targets. Extract samples using an owned compute shader into readable buffers; compare with independent per-eye native-style reference draws. This is a backend prerequisite, not native SPS scheduling.

**Tech Stack:** C++17, D3D11, HLSL, existing Windows native test build.

**Spec:** docs/SPS-DEVELOPMENT.md, Goal and acceptance; Second capture: resource trace and MSAA.

## Global constraints

- Preserve the existing single-sample tests and normal rendering behavior.
- Do not alter engine hooks, private live-game proof limits, user graphics settings or released binaries.
- Use only owned shaders and geometry. No captured game inputs in the repository.
- Verify individual samples, not only resolved colors. No performance or production SPS claim.

## Review focus

- Array routing must keep distinct asymmetric eye projections and parallax.
- Depth tests and sample masks must preserve samples that should remain untouched.
- The sample extraction itself must be checked against known clear/foreground values and edge samples.
- Capability failures must be explicit; a skipped case is not a passing case.
- All COM references and pipeline bindings must be released, including extraction resources.

### Task 1: Offscreen multisample equivalence

**Files:** Modify mod/tests/stereo_instancing_test.cpp; create a focused test-only helper under mod/tests if needed. Existing build invocation remains valid. Update docs/SPS-DEVELOPMENT.md with measured test scope after validation.

**Interfaces:** Existing submit_stereo_indexed and query_stereo_capabilities remain unchanged. Test executable keeps default WARP and --hardware modes.

- [x] Extend the fixture to request 2x MSAA and assert matching color/depth sample counts. Run the new case against the old single-sample allocation and record its failure before implementing multisample allocation.
- [x] Allocate RGBA8 color and typeless R32 depth with D32 DSV and R32 SRV; use TEXTURE2DMSARRAY views. Use an owned compute shader to load each color/depth sample into a structured output buffer for staging readback, unbinding outputs before SRV reads.
- [x] Compare two separate eye draws with one paired draw, both geometry-shader routing and vertex-array routing when supported. Preserve asymmetric projections, existing per-instance inputs and nonzero base-instance coverage.
- [x] Exercise 2x MSAA (required on WARP and current hardware), plus 4x when supported with an explicit skip message otherwise. Exercise full sample mask and a partial mask. Assert exact color agreement, bounded depth error <= 1e-6, visible near/far geometry, clear background, distinct eyes and partially covered edge pixels. Assert masked samples retain clear color and depth.
- [x] Run default WARP and --hardware tests using the existing llvm-mingw toolchain. Record actual adapter and support, commands, failures and results in the report. Do not run the full native suite concurrently with the controller.
- [x] Self-review and report the exact changed files and remaining limitations. Do not commit; controller reviews and commits after acceptance.

