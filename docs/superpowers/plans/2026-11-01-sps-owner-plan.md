# Owned stereo owner plan implementation

> **For agentic workers:** Use superpowers:subagent-driven-development.

**Goal:** Implement the closed owner-traversal portion of future SPS preparation.
**Architecture:** Pure owned tree, complete left then right traversal on shared
copied state, ordered admissions/transitions, explicit non-renderable result.
**Tech Stack:** C++17, existing llvm-mingw and PowerShell native tests.
**Spec:** docs/superpowers/specs/2026-11-01-sps-owner-plan-design.md

## Global constraints

- No native reads/calls/writes, callbacks, pointer retention or runtime wiring.
- VERSION remains 0.1.0; every result reports rendering eligibility false.
- Limits: 1..4096 nodes, at most 65536 source references, unique source IDs.
- Complete left traversal precedes right on shared copied state. Preserve input.
- Refusal yields no partial output; unsupported null-attachment flags are refused
  throughout the tree, including subtrees both eyes would reject.

## Review focus

- All-bits inclusion, including zero and high-bit masks.
- Shared cross-eye state and pre-rejection versus post-child write order.
- Invalid graphs hidden behind rejected parents and maximum-depth trees.
- Bounded allocations and failure returning empty results.
- Names and docs distinguishing owner admission from drawable visibility.

### Task 1: Owned owner planner and tests

Files: create mod/native_stereo_owner_plan.hpp and
mod/tests/native_stereo_owner_plan_test.cpp; modify mod/build.ps1 only to compile
and run the new test at O0/O2. Add a concise section to docs/SPS-DEVELOPMENT.md.

Interface: namespace lmuqv, NativeStereoOwnerPlanInput and
NativeStereoOwnerPlanResult; build_native_stereo_owner_plan(const
NativeStereoOwnerPlanInput&) noexcept. Implementer chooses clear member names and
small supporting structs/enums matching the spec, documenting their ownership.
Source IDs are opaque uint32_t values, never interpreted as native addresses.
Owner pending/current values are opaque uint64_t tokens; attachment is a bool.
Result contains completion/refusal reason, renderingEligible=false, owned final
states, per-eye owner/source order and ordered transition records with eye,
owner index, field, before and after. Equal-value assignments need no event.

- [ ] Write analytic focused tests and run a failing baseline before implementation.
- [ ] Implement bounded graph validation and iterative left-then-right traversal.
- [ ] Cover right-only root/child admission; zero/partial/high-bit masks; assignment
  before flag/mask rejection; no final clear on early rejection; parent final clear
  after child; left write visible to right; nonnull attachment bypass; special/
  refresh refusal even when hidden; root/index/cycle/shared/duplicate/unreachable
  errors; duplicate IDs and count limits; 4096-deep success; input immutability.
- [ ] Compile/run focused test O0/O2, integrate in build.ps1 and run full native
  suite once. Use existing warning/error flags; no new dependency.
- [ ] Self-review, commit only task files, report exact tests and known limitations.

### Task 2: Independent review and publish prerequisite

- [ ] Review frozen Task 1 diff for spec and quality; resolve blocking findings.
- [ ] Verify source/build evidence, document and push. No live session is needed:
  a native producer/lifetime contract is explicitly outside this component.
