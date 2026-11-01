# Diagnostic audit file trigger implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development or superpowers:executing-plans. Review before live use.

**Goal:** Start the existing bounded diagnostic capture without relying on a frame-polled keyboard pulse.

**Architecture:** When the existing stereo audit is explicitly enabled, observe a fixed `stereo-audit.request` file in its output directory. A new last-write timestamp starts one normal two-frame capture on the existing owner-thread frame boundary. Keep the hotkey and ordinary rendering unchanged.

**Tech stack:** C++17, Win32 file attributes, existing WARP audit tests.

**Spec:** Inline design under the user's autonomous development instruction. Repeated missed injected hotkeys require human intervention; a local diagnostic request removes that dependency. This is a developer interface, not a new rendering mode or general command execution interface.

## Global constraints

- VERSION stays 0.1.0. All existing capture bounds, owner-thread guards and rendering behavior remain.
- Master-off or disabled audit does no new clock/filesystem work. The file is only armed after successful enabled audit configuration.
- Fixed file name in the configured audit output directory; no file contents, arbitrary commands, environment changes, writes/deletes or additional native hooks.
- Poll file attributes at most once per 500 ms while audit is enabled and no capture is active. Cache the timestamp present at initialization so a stale request does not auto-trigger a new session.
- A changed timestamp starts once. Holding the same timestamp does not repeat. Missing files/directories/failed reads do not trigger. A new request during capture may wait until the capture is complete; it must never stop/truncate an active capture.
- Preserve hotkey rising-edge behavior, including its explicit early-stop behavior. File-triggered captures use the same bounded capture path; label the trigger in diagnostics.

## Review focus

- No extra I/O when diagnostics are disabled, setup fails or on the wrong thread.
- Stale markers and unchanged timestamps cannot cause repeated captures.
- Active captures cannot be cut short by file polling.
- Transient absence/failure cannot replay an already consumed timestamp.
- Cleanup/reconfigure resets polling state and primes the new directory independently.

### Task 1: Bounded request polling and integration

**Files:** Add a small focused helper and standalone fixture if useful (mod/stereo_audit_trigger.hpp, mod/tests/stereo_audit_trigger_test.cpp). Modify mod/stereo_audit.cpp, mod/tests/stereo_audit_test.cpp and mod/build.ps1 as needed. Root owns documentation/live actions/commits.

**Interfaces:** Helper owns configured path, last observed valid timestamp and last poll time. Use GetFileAttributesExW with a non-directory check; combine FILETIME into a uint64 timestamp. A deterministic clock/probe seam for the helper is appropriate to test throttling without sleeping. Keep implementation small; no worker threads, IPC servers or generic command parser.

- [x] Write failing tests for initial existing/absent marker, changed/unchanged timestamp, transient failure/absence, directory refusal, 500 ms rate limit, reset/reconfigure and disabled/unarmed no probe. Implement and make them pass.
- [x] Integrate after current enabled/owner/open-scope guards in stereo_audit_frame. Poll only when no capture is active; combine a request with the existing hotkey start decision without changing the hotkey stop logic. Reset with cleanup/configuration. Clearly log file-triggered starts (and include a capture trigger field if needed for fixture evidence).
- [x] Extend the WARP fixture to touch a real request file and verify a complete two-scene-frame capture starts without keyDown, stale/unchanged markers do not start, and a request does not truncate active work. Retain all existing draw/source/camera/resource/disabled tests.
- [x] Run focused tests, then full native build once; report commands/results and final DLL hash. Leave commit to root.

### Task 2: Review and live validation

- [x] Independently review the implementation.
- [ ] Package separately and verify hashes; launch offline LMU, create/touch the marker through filesystem tooling, and verify a complete native source-containment capture without user keyboard input.
- [ ] Close LMU when finished, publish reviewed source-containment and trigger prerequisites with live evidence. Genuine SPS remains unfinished.
