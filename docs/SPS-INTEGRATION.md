# SPS integration diagnostic — 4 November 2026

This branch integrates main v0.3.3 (`c38e122`) and the complete SPS research from
`experiment/sps-0.2.0` (`61a5940`, including `codex/sps-0.1.1`). Native rendering,
preparation and draws remain enabled. The revised helper probe has now passed
its bounded live numeric check: 2,048 comparisons, including 1,024 focus calls,
matched all 106,496 compared float scalars exactly across four scene frames.
Coverage remains capped and limited to the sampled ordinary path. Step 3 is the
[native input/ownership contract](SPS-NATIVE-INTEGRATION.md); see detailed live
evidence in [SPS-DEVELOPMENT.md](SPS-DEVELOPMENT.md).

## Integration changes

- Preserve main's frame handling, hook checks, VRS viewport eligibility,
  resource handling, launcher and packaging changes.
- Require the diagnostic classifier target to pass the current function-start
  check as well as its prologue and caller checks. A synthetic PE fixture rejects
  matching bytes inside another function, a changed caller/prologue, and a
  non-executable section.
- Register the missing camera, source, classifier/capture, owner planner and
  audit-trigger checks in MSVC. Run the four owned classifier/capture, owner and
  transform fixtures at both `/Od` and `/O2`. Keep `/fp:strict` for all consumers,
  including the diagnostic DLL, and preserve main's response-file quoting.
- Keep GCC's strict FP requirements and main's existing warning/crop checks.
- Make the launch metadata test use its temporary game fixture instead of the
  default Steam installation path. No launcher behavior changes.
- Compare at the transform helper's entry/exit for its verified caller and
  ordinary modes. Keep the larger preparation wrapper and all native work.
- Export hook availability, comparison/refusal counts and first-mismatch bits.

## Validation on this PC

- Full `mod/build-msvc.cmd`: passed, including native/SPS fixtures, audit and
  lifecycle checks, engine-hook policy, late BeginFrame and the XR integration
  cases. Both classifier builds passed 1,014 analytic checks each. The final
  layer DLL was built successfully.
- `mod/build-launcher.ps1`: passed the standard launcher suite and GUI layout
  smoke tests. The separately elevated registry-ownership fixture is compiled
  but intentionally not executed by that build script.
- Portable stereo on WARP and NVIDIA GeForce RTX 4080: passed both VS array
  routing and GS fallback, with 1x/2x/4x MSAA, per-sample color/depth, asymmetric
  views, instance ranges and sample masks.
- Merge conflicts resolved; PowerShell scripts parse and Git whitespace checks
  pass. GCC was not rerun on this PC.
- Revised helper numeric/capture checks pass at `/Od` and `/O2`, including the
  flag combinations from the previous live test, without widening admission
  around the whole preparation wrapper. Full native checks and JSON parsing pass.
- Offline PE metadata confirms the classifier address is a function start in
  the installed executable with the documented pinned hash. On-disk byte probes
  did not match the runtime anchors, also true of the pre-existing render and
  preparation anchors. Actual loaded-code hook admission remains a live check.

The integrated layer SHA256 from this build is
`A3F4FB298F7407565E4C7D760206724E8A53AFFD4F5DFF269E00B3DBD7832553`.
Subsequent live helper comparisons matched the sampled output as described
above. This does not establish native SPS or a frametime improvement. Existing
audit variable-shadow warnings remain.

## Capture procedure

Use this revised helper-probe build. Its prepared local test folder preserves
the last successful VectorXR launcher settings. The previous diagnostic package
remains available separately.

1. Extract this diagnostic package into a separate folder. Run its launcher,
   select your installed game directory and the quad-view provider you normally
   use: Native, QVFR or VectorXR. Use the same render settings and MSAA as your
   working setup. Use the launcher's direct launch for this diagnostic.
2. Enable **Diagnostics** and **Stereo draw audit**. Disable shader capture,
   paired draw proof, render profiling, benchmark recording and all CPU upload
   reuse/batching experiments for this capture. This keeps the original native
   path available to the audit.
3. Start LMU, enter an on-track cockpit scene, and keep the headset awake and
   displaying the scene. Press **Ctrl+Alt+F8** once. Alternatively, after the
   diagnostic initializes, run this from the extracted package directory:

   ```powershell
   Set-Content -LiteralPath .\logs\stereo-audit.request -Value capture
   ```

4. Allow the two-scene-frame capture to finish, then close LMU normally. Keep the
   new `logs/stereo-audit-*.json`, matching `quad-*.log` and `quad-*-run.json`.
   Raw shader/code captures remain private development inputs.

The capture must contain `native_transforms` comparisons for focus slots **2/3
and 6/7**, with `hook_available=true` and record boundary `transform_helper`.
Distinguish available matches, actual mismatches, refusals, dropped
attempts and incomplete scopes. A standby/outer-only or zero-available capture
cannot validate the focus subset. Audit timings include substantial diagnostic
overhead and cannot establish a speedup.

Once that comparison is usable, continue with both eyes' input ownership,
independent visibility/order, remaining preparation/cache effects and scheduling
before native consumers. See [SPS-DEVELOPMENT.md](SPS-DEVELOPMENT.md) for the
existing evidence, numeric restrictions and unresolved replacement boundaries.
