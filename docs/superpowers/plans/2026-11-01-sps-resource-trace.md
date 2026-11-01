# SPS resource-use trace implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Correlate observed native focus targets with later D3D resource uses without changing native rendering.

**Architecture:** Extend the existing opt-in, two-frame audit with bounded resource-use records, sharing the native target identity namespace and sequence counter. Record known reads/writes and explicit coverage limits. A binding is a potential use, not proof that a shader accessed it; absence of an observed use is not proof of no dependency.

**Tech Stack:** C++17, D3D11, existing MinHook audit and WARP tests.

**Spec:** docs/SPS-DEVELOPMENT.md, especially the first native capture and intermediate ownership requirements.

## Constraints

- No draw suppression/reordering, shader conversion, native scheduling changes, game process edits or release-file modifications.
- Disabled/noncapturing/wrong-thread/foreign-context calls perform no trace work.
- Anonymized resource IDs must correlate directly with native_passes targets.
- Explicitly bound event counts and references; contain diagnostic allocation failures and always forward original calls.
- No raw resource contents or pointers serialized. No ownership/replay eligibility claim.
- Local captures and proprietary shader/code inputs stay ignored.

## Task: Bounded observational trace

- [x] Add meaningful WARP integration tests first: bind a previously observed native target as a shader resource after its scope, observe copy source/destination, and dispatch reads/writes. Check ordered records, shared IDs, original rendering/readback, reset, disabled paths and bounds.
- [x] Implement a focused helper/header if needed to keep stereo_audit.cpp readable. Graphics snapshots identify resources behind bound SRVs across VS/HS/DS/GS/PS and OM color/depth/UAV targets; dispatch snapshots identify CS SRVs/UAVs. Inspect all legal SRV slots, not only the legacy first sixteen. Record read/write as potential access; read-only DSV and blending cannot be inferred from a target pointer alone.
- [x] Trace existing CopyResource/CopySubresourceRegion hooks with source and destination identities and ordered submission events. Preserve exact original parameters, results and call count.
- [x] Observe calls between native scene scopes on the selected owner context during active capture, using an explicit outside-scope slot. Do not rely on a native mesh ID for these events.
- [x] Keep coverage explicitly partial unless every relevant hook is implemented and tested. Disclose unobserved clears/resolves, Context1 variants, indirect arguments, deferred/foreign context activity and resource-content/lifetime limitations. Do not label a first observed read as the first actual consumer.
- [x] Run audit default, diagnostics-off, shader-export and pair-proof tests, parse JSON, then full native suite. Independent review before committing.

## Review focus

- Exceptions after acquiring COM references must release them and forward native work.
- New context queries must not alter bindings or recurse into audit hooks.
- End-of-frame/stop must not leave stale records, tokens or retained resources.
- Dropped/aborted/incomplete traces must be visibly unfit for scheduling decisions.
- Identity equality and equal descriptors do not prove equal contents or native lifetimes.

## Follow-up

Analyze a live trace for focus target uses and choose the next native scheduling investigation from evidence. This task is not an SPS implementation or a performance benchmark.
