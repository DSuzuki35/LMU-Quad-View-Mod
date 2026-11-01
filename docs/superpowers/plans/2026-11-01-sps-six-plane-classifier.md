# Six-plane classifier and main-view capture plan

**Goal:** Make the owned classifier diagnostic cover the actual six-plane path observed in LMU, and reserve its bounded record budget for the expected main view.

**Spec:** Inline design under autonomous SPS authorization. First live original-call capture had zero comparisons: left slot budget filled by non-main views and all sampled main right calls had planeCount6. Mapped classifier special branch is complete; verified view-mode getter returns view+0x3E8. No native invocation or rendering is replaced.

## Global constraints

- VERSION remains0.1.0. All native calls/draws and floating-point state remain intact. No scheduling eligibility or performance claim.
- Owned model supports planeCount0..6, skipDistanceBounds=false, strict scalar float order and explicit conservative finite/domain refusal. Counts outside0..6 remain unsupported.
- Per-slot512/total4096 record limits unchanged. Read budget grows explicitly from384 to448 bytes per invocation to cover the additional before/after inputs; measured maximum must fit. Main-view pointer preselection performs no new native reads.
- Original exactlyonce, bounded owner-thread capture, current-main global/parent rechecks, generation protection, exception transaction locking and export-only math remain mandatory.
- No pointers serialized or retained beyond a synchronous scope/invocation. No raw native code/shader capture committed.

### Task 1: Extend owned model and select the scoped main view

- [x] Extend numeric model/analytic tests for special-plane and secondary-distance checks before the existing plane loop; preserve all old0..4 behavior.
- [x] Extend bounded snapshots/stability/JSON with the new numeric fields and copied-field bits; update byte-bound tests.
- [x] Pass the already-read main-view identity into the audit scope; reject other views before charging attempt budgets or reading native inputs. Keep snapshot current-global/parent checks.
- [x] Add focused model/snapshot/WARP tests, including first512foreign calls not starving later qualified main calls, no reads on foreign/unknown scopes, lifecycle/reset, exact return-vs-store distinction, strict boundaries, nonfinite/overflow behavior. Full native build once after focused tests.

### Task 2: Review and live differential evidence

- [x] Independent scoped review and resolution of blocking findings.
- [ ] Separate verified diagnostic package; offline Monza capture; report available/refused/mismatch counts, both outer eyes, caps and all limitations. Close game when inputs collected.
- [ ] Document and push reviewed prerequisite. Continue native SPS only with actual evidence; conformal prototype remains on hold.
