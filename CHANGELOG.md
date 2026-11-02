# Changelog

Release numbers use `MAJOR.MINOR.PATCH`. The root `VERSION` file supplies the
launcher title, Windows EXE/DLL metadata and default portable ZIP name.
Versions before 1.0 remain experimental and may change compatibility or settings.

## Unreleased - benchmark / motion compensation / rendered crop candidate

- Add opt-in head-locked Minimal/Detailed benchmark overlays, Dan Suzuki UI-kit
  styling, live placement, separate DirectInput joystick bindings, and manual
  lap-length captures. Hiding the overlay does not stop recording.
- Save bounded local CSV checkpoints and HTML/JSON reports with frame-time
  percentiles, sparse app GPU spans, actual resolutions, data-loss counts and
  explicit measurement limits. No hardware-utilization or compositor FPS claims.
- Add live and latest-run diagnosis, including missing logs, invalid resources,
  invalid poses, recovered projection errors and overlay-only failures.
- Add native/managed ABI, ownership, sparse GPU, report, input-state and layout
  tests. Real headset/joystick testing remains required.

- Preserve submitted stereo pose corrections in all four views instead of rejecting
  changed reference spaces or overwriting upper-layer compensation.
- Add independent 25–100% rendered crop width/height in the launcher. Crop reduces
  outer FOV and target dimensions together; focus resolution stays unchanged and
  its FOV is kept inside the crop. Settings apply after restarting LMU.
- Add pose, crop geometry, fake-runtime and launcher/layout regressions.
- Fix MSVC build argument quoting for checkout paths containing spaces.
- Qubic hardware and cropped compositor output still require tester validation;
  no measured GPU improvement or live-setting support is claimed.

## 0.1.1 - 2026-11-01

### Added

- Horizontal and vertical focus-region offsets for fixed and eye-tracked
  foveation, available with Native OpenXR, QVFR and VectorXR.
- Saved signed controls and Reset offsets. Positive values move right/up;
  negative values move left/down. Units are percent of each eye's full projected
  view span, from -50 to +50. Existing settings default to zero.
- Equivalent CLI parameters and offset values in diagnostic run metadata.

### Improved

- Sidebar branding shares a consistent left edge across font sizes.
- Numeric controls leave more space between the value and spinner arrows.

### Behavior and scope

- Offsets follow the provider's current focus position without accumulating or
  changing pose, projected box size, render resolution or GPU passes. Rendering,
  the green outline and submitted composition use the same shifted focus FOV.
- Zero preserves the provider placement exactly. Offsets are applied by the mod,
  rather than written to provider profiles. Large offsets can move the region
  beyond the outer-view edges. Save and restart LMU to apply changes.
- Full native single-pass stereo is deferred to a 0.2.0 research target. The
  separate SPS branch's new diagnostics and planning code are not included.
  This release makes no new CPU/FPS improvement claim.

## 0.1.0 - 2026-11-01

First numbered release after the provider and stereo research branches were merged.
This supersedes earlier date-based development builds and `v0.1.0-alpha`.

### Added

- Optional VectorXR provider alongside Pimax/native OpenXR and mbucchia's
  Quad-Views-Foveated. Providers are installed separately.
- Saved diagnostics switch and opt-in CPU/GPU render profiling.
- Contributor guide, architecture overview and performance measurement guidance.
- Consistent release version in the launcher, both binaries and portable package.
  Packaging rejects binaries whose version differs from `VERSION`.

### Improved

- Shared frame submission uses stack storage for up to 16 input layers, removing
  heap allocations from the common conversion path. Larger submissions keep a
  dynamically sized fallback. Layer ordering, projection validation and image
  release behavior are preserved.
- Outer UI drawing uses cached dimensions validated during outer capture,
  eliminating repeated COM resource/descriptor queries. It captures/restores
  D3D11 state only when there is an acquired outer target to draw into.
- Launcher discovery tolerates malformed unrelated implicit-layer manifests while
  continuing to validate explicitly selected provider manifests strictly.
- MinGW build compatibility for the imported stereo research tests.

### Experimental / known limits

- Stereo shader conversion, private paired-draw checks and CPU upload reuse/batching
  remain opt-in research. Full game-renderer single-pass stereo is not implemented;
  original eye draws and additional focus scene traversals still run.
- Synthetic tests confirm lower submission bookkeeping cost. They do **not** prove
  that reported +4-5 ms CPU overhead or spikes are fixed. Matched in-headset QVFR
  and VectorXR captures are still needed. See [performance notes](docs/PERFORMANCE.md).
- No resolution, FOV, gaze behavior, rendering quality or default research settings
  are changed by the performance pass. Headset compatibility remains dependent on
  the provider, runtime and supported LMU build.
