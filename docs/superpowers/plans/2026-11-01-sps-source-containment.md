# Native mesh source containment implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans, with tests and independent review before live use.

**Goal:** Determine whether each audited mesh belongs to the record array reported by its geometry, without treating containment as ownership or changing rendering.

**Architecture:** Extend the existing active group-31 audit with a bounded scalar snapshot per emitted packet. Validate geometry-array containment using copied count/base values, anonymize the base immediately, and save only owned numbers. No new native hook is needed.

**Tech stack:** Existing C++17 checked reader, native audit and WARP fixtures.

**Spec:** The design is recorded here under the user's instruction to continue SPS autonomously. The existing native bucketizer reads a signed 32-bit count at source+0x168 and array base at +0x170, with 0xE0-byte records. The audited mesh has geometry at +0x80. Their relationship is a hypothesis to verify by exact containment, not an assumed back-pointer contract. Native source-record vectors may already have been cleaned before group 31 and must not be scanned.

## Global constraints

- Keep VERSION 0.1.0 and scheduling_eligible=false. Preserve all native calls, preparation, bindings and draws.
- Only read at the existing enabled, active-capture, owner-thread, group-31, caller-qualified gate, after existing packet/global limits. Disabled and idle paths do no new reads.
- No new hooks, native method calls, process writes, retained native resources, visibility classification, or draw suppression.
- Copy at most 24 additional bytes per packet: geometry+0x168 int32 count, +0x170 uint64 base, +0xF8 float squared-sort-distance, +0x138 uint64 lazy-state pointer (retain only presence).
- Limit count to 16384; validate all address arithmetic and exact record alignment. Do not dereference the reported array, scan capacity, or claim that containment proves allocation lifetime.
- Invalid/unreadable/non-finite input produces an explicit reason and null values. It must not invalidate an otherwise valid ordinary packet observation or affect rendering.
- Publish no raw native addresses, game code, shaders or live captures. Array address is an ephemeral lookup key, immediately converted through the audit's existing bounded identity map.

## Review focus

- Negative/zero/excessive counts, overflowing end addresses and interior/end pointers never qualify.
- Failed source reads leave ordinary packet capture intact and cannot leak partially copied values.
- Reused array addresses are identity observations, not content versions or ownership proofs.
- Global packet cap prevents additional source reads; disabled/idle/wrong-thread paths remain unchanged.
- A null lazy-state pointer excludes one known refresh path only; it cannot label a packet generally static or SPS-eligible.

### Task 1: Owned containment snapshot and audit integration

**Files:** Add mod/native_stereo_source.hpp and mod/tests/native_stereo_source_test.cpp. Modify mod/stereo_audit.cpp, mod/tests/stereo_audit_test.cpp and mod/build.ps1. Do not change engine_bridge.cpp, existing packet-reader behavior, VERSION, or unrelated files.

**Interface:** Provide capture_native_stereo_source(mesh, geometry, readable), using uintptr_t or const void* inputs consistently with existing helpers. Result owns complete/reason/bytesRead, signed count, record index, finite sort-distance, lazy-state-present and ephemeral array identity. Require mesh!=0, geometry!=0, valid reader, count in 1..16384, nonzero array base, nonoverflowing base+count*0xE0, and mesh within that half-open interval at a 0xE0 multiple. The helper reads no mesh/array contents and invokes no native code. Use existing checked Reader without introducing unchecked pointer additions. Catch reader exceptions. Returning a failed result must not expose values to JSON.

**Integration:** Call the helper for each packet actually accepted into NativePassRecord, before identities are anonymized. Add an owned source record on NativePacketRecord; replace the ephemeral array key with an ID using nativeObjects. Identity-map exhaustion follows the existing diagnostic abort path. Serialize packet.source_containment as complete, reason, bytes_read, and values (null when incomplete). Complete values contain array ID, count, index, sort_distance, lazy_state_present. Existing geometry ID/flags already identify the associated geometry; do not duplicate them. Use current round-trip numeric serialization. Document a 24-byte per-packet bound and 32768-packet capture bound in the native audit JSON. No new comparison/eligibility claims.

- [x] Write and run failing standalone tests: first/last valid records, nonzero index, null/missing/unreadable reader, throwing reader, null/zero/negative/excessive counts, overflow, before/base+end/interior/misaligned mesh, NaN/Inf distance, copied-source mutation and exact 24-byte maximum. Do not dereference synthetic mesh addresses.
- [x] Implement the helper, integration and build entry; make focused tests pass.
- [x] Extend WARP fixture with a valid source array containing an audited mesh, verify owned source values and anonymized IDs in JSON after source mutation. Verify an invalid source retains ordinary packet data with null source values. Preserve gate/read-count and draw/resolve checks, including master-off.
- [x] Run focused fixtures then the native build once. Report exact commands/results and final DLL hash. Root reviews and performs live validation; leave commit to root.

### Task 2: Live evidence and publication

- [x] Independently review implementation and tests.
- [x] Package a separate diagnostic build with the user's settings; capture offline native packets and summarize resolved/unresolved containment, per-eye array identities/counts and scalar differences. Do not call these ownership or visibility proofs.
- [x] Close LMU when live inputs are no longer needed. Update development evidence, commit and push the reviewed prerequisite. Continue native SPS integration; conformal implementation remains on hold.
