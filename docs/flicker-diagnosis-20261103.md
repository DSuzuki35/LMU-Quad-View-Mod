# Track flicker and missing gaze: 2026-11-03

Observed setup: Dream Air SE / SBoys3 1.3.0 / SteamVR 2.18.4 / QVFR 1.1.3.
Run `quad-20261103-091752-4e90ba67`: dynamic FOV off, outer 25%, focus
250%, native outer targets on, green border on. Actual targets were outer
806x754 and focus 3578x3540 per eye. Provider focus width was 44.44332107%
(UI-calibrated display approximately 46%), height 47%.

## Evidence

- QVFR reports no eye-tracker data received after 60 seconds.
- OpenVR read-only Background client reports `dashboardVisible=1`,
  `inputAvailable=0`, scene focus PID equal to LMU PID 52060. Headset activity
  also reached UserInteraction while input remained unavailable. SteamVR's
  OpenXR session reaches VISIBLE but never FOCUSED in this run. This blocks
  active OpenXR action input; there may still be further gaze problems after
  dismissing the dashboard.
- Desktop UI attempts and a temporary enableDashboard off/on toggle did not
  dismiss the dashboard. The setting was read back as restored to true. No VR
  services were stopped. User ended LMU; subsequent scene PID was zero.
- Live shared-memory snapshot: 3514 submitted scene samples, mean application
  interval 15.82 ms (p95 21.79), app span 13.79 ms (p95 19.44), provider CPU
  submission 0.55 ms. 439 GPU samples averaged 11.21 ms (p95 17.89).
  GPU span excludes compositor work. These are one window, not a benchmark of
  every reported flicker event, and do not establish GPU load as sole cause.
- Sampled frame 15000 locates views, renders both focus eyes and copies both
  outers, releases both app images, then locates again at the same display
  time before submitting. Previously every locate overwrote `s.views`, even
  after rendering consumed those FOVs.

## Correction and validation

The first successful renderer read locks that frame's views, uncropped outer
FOV, locate time/space and dynamic scale. Subsequent calls still return fresh
tracking to their caller but cannot replace the rendered image metadata. New
frames unlock the snapshot. Late failed or invalid queries also cannot erase
an already rendered frame. Sparse `render_views preserved` diagnostics report
FOV and position changes between these queries.

The real-adapter regression fails before the correction and passes afterwards:
moving gaze, fresh caller output, both-eye consistency, failed/invalid late
queries, different time/space, uncropped FOV, stable dynamic scale, composition
submission, and next-frame refresh. Existing invalid-FOV tests now explicitly
exercise failures before the renderer consumes its snapshot. An independent
review found no important issue. Actual symptom resolution is not yet verified.

## Next capture

`tools/start-frame-capture.cmd` launches with the package's saved settings and
the usual launcher executable/provider checks. It arms a single four-image
capture in a unique `logs/capture-*` directory, without creating its trigger.
After loading the track, create the empty `capture.trigger` file named by
`capture-run.json`. Do not measure performance during this one readback frame.
The log records all four captured FOVs/poses and successMask=15 on success.
This can distinguish misalignment already present in the input textures from
provider composition/reprojection. No files are uploaded.

References: OpenXR xrSyncActions specification and Valve OpenVR public headers.
https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrSyncActions.html
https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h
