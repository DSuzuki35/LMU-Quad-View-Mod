# Same-invocation native classifier audit plan

**Goal:** Compare the restricted owned numeric classifier with the original native invocation, keeping native rendering intact. This is an SPS prerequisite, not SPS execution.

**Spec:** Autonomous SPS authorization; verified mapped classifier and caller ABI. Pure model is in mod/native_stereo_classify.hpp. The private task brief records exact native offsets and minimal ABI anchors.

## Global constraints

- VERSION remains 0.1.0. No draw suppression, replay, native extra calls, reconstructed engine objects, or scheduling eligibility.
- Classifier detour calls original exactly once for every case, preserving all arguments, returned flags and native floating-point environment.
- Opt-in diagnostics only: inactive/disabled/wrong-thread/wrong-caller/wrong-view work performs no native snapshot reads. No group-31 requirement because classification precedes it.
- Bounded owner-thread capture: 512 attempts per slot, 4096 total, 384 read bytes per attempt including before/after/main-view reads. Every cap/read failure is reported as incomplete coverage.
- Snapshots own numeric data. Export only anonymized identities; no process pointers, executable dump or native shaders committed.
- Numeric model evaluated only during export under default floating-point controls with environment restored, not in the native hot hook.

### Task 1: Capture original classifier inputs/results

- [x] Add focused owned bounded snapshot/comparison helper and analytic tests. Capture pre/post fields, require stable non-output inputs, distinguish original returned flags and stored flags. Preserve FP environment on every path and refuse unsupported captured control modes.
- [x] Integrate begin/end API with existing audit, per-slot/global bounds and explicit JSON comparisons/refusals/mismatches. Keep all scheduling_eligible=false. Include bounded owned input records for interpreting mismatches.
- [x] Add optional pinned-build/anchor/caller-checked engine detour, install/cleanup following existing native-preparation hook. Failure leaves native classifier untouched and logs unavailable status.
- [x] Extend WARP diagnostics fixture for gating, ownership, native-output match/mismatch, limits, reset, FP preservation and aborted/incomplete captures. Run focused tests then full native build once.

### Task 2: Review, live evidence and publication

- [x] Independent scoped review of implementation/test evidence; resolve blocking findings.
- [ ] Stage separate diagnostic build, verify hashes, run offline Monza file-trigger capture and inspect available/refused/mismatched records for each observed eye. Close LMU when inputs collected.
- [ ] Document exact limitations and evidence, commit and push prerequisite to the SPS branch. Continue native preparation/ownership; no release claim.
