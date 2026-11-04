# SPS CPU cost gate

This research build measures existing opaque group-31 work before implementing
shared preparation and portable stereo instancing. It removes no native work.
Matrix agreement and call counts do not establish a frametime improvement.

## Run

Use `Start-SPS-CPU-Profile.cmd` in the prepared diagnostic package. It reads the
saved game/provider, foveation mode, focus offsets and native outer target setting.
It starts a separate CLI launch with upload experiments, broad profiling, GPU
timing, pixel statistics and heavy stereo audit disabled. It does not rewrite
saved launcher settings. Do not also start the game from the GUI.

On track, press **Ctrl+Alt+F10**. Maintain a representative reproducible workload
for 120 seconds. A second press stops early. Session shutdown saves completed
groups. The game must be rendering both focus eyes; menus/standby are unsuitable.
Logs are written under the package's `logs` directory.

Summarize a completed capture with
`python tools/analyze_sps_cpu_profile.py logs/quad-<timestamp>.log`.

Direct CLI alternative:

```powershell
.\launch.ps1 -Mode quad -Backend VectorXR -UseRuntimeSettings -NativeOuterResolution -SpsCpuProfiling -GameDirectory 'E:\SteamLibrary\steamapps\common\Le Mans Ultimate'
```

## Measurements and limits

- Every sixteenth verified scene frame is sampled. Sampled frames alternate
  coarse group timing and detailed nested timing. All frames retain the same
  hooks and native work. These are different frames; differences can include
  changing workload. Coarse is a control for detailed instrumentation, not a
  pristine unhooked baseline or an SPS A/B result.
- Only the verified main-pass caller, group 31 and exact currently rendered view
  qualify. Nested ranges invalidate the outer sample. Other contexts/threads,
  mirrors and unverified native preparation callers are excluded from attribution.
- `wall_mean_ms`, P95 and P99 are elapsed **milliseconds per recorded group
  invocation**, including waits and bridge/profiler overhead. Multiple groups can
  belong to one scene frame. Do not add per-group means and label them frametime.
- `preparation` observes the native wrapper. `transform` is its known helper
  with both mode booleans false; `other_transform` covers other known-call modes.
  This does not apply the numeric audit's full mesh/material eligibility check.
- `upload` includes native binding/upload work. `map` and `unmap` observe API
  invocations, including failed Map attempts. `draw` covers all seven base D3D11
  direct/instanced/indirect draw entry points. It measures CPU API wall time,
  not GPU execution or all driver state-setting work.
- Inclusive categories overlap. Exclusive categories subtract measured nested
  calls. Their sum plus `remainder_mean_ms` partitions detailed group wall time.
  The remainder includes sorting, state changes, unhooked work and instrumentation.
  It is not a measured pool of view-independent/shareable work.
- Group-level thread cycles include user/kernel execution. They are kept as
  cycles, never converted to milliseconds. Query failures are reported. They
  cannot identify driver stalls or distinguish spin waits from productive work.
  Use ETW/WPA if wall/cycle results leave wait attribution unresolved. See
  [Microsoft's QueryThreadCycleTime contract](https://learn.microsoft.com/en-us/windows/win32/api/realtimeapiset/nf-realtimeapiset-querythreadcycletime).
- Detailed timing uses clock reads, short mutex sections and bounded storage.
  No lock spans an original invocation. There are no object snapshots, resource
  content copies, shader reflection, readbacks or GPU timing queries. The timing
  cost is still real; inspect the coarse control before drawing conclusions.
- Both phases report coverage/capacity. Missing focus slots, invalid/dropped
  ranges, insufficient samples or large control differences require investigation.

The native source producer, opposite focus camera, target/consumer integration
and remaining preparation effects remain open. This profile estimates where to
investigate; it cannot prove that a measured category can be removed or shared.
Measure new gathering/copying/retention costs once that producer exists.

## Decision gate

1. Establish representative coverage and instrumentation impact.
2. Compare preparation (including its transform subset), upload, draw API and
   residual costs. Identify actual view-independent work through native evidence.
3. Verify early right-focus input production and native target/consumer costs
   before committing to a large replacement. Compare early left/right preparation,
   same-eye outer/focus pairing and deferred consumer ownership as needed.
4. Keep original eye draws during producer equivalence checks. A neutral CPU-only
   stage can still be useful if paired submissions offer enough total benefit.
5. Accept an optimization only after image/depth correctness and repeated matched
   performance runs with this detailed instrumentation disabled. The 0.3 ms target
   remains an acceptance target, not an estimate derived from these categories.
