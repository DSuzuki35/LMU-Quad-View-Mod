# Changelog

Release numbers use `MAJOR.MINOR.PATCH`. The root `VERSION` file supplies the
launcher title, Windows EXE/DLL metadata and default portable ZIP name.
Versions before 1.0 remain experimental and may change compatibility or settings.

## Unreleased - dynamic focus test

- Optional automatic center FOV scaling with configurable min/max and GPU reserve.
- Fixed resolution; asynchronous GPU feedback with smoothing, hysteresis, scene,
  failure and staleness guards, and a stable scale per display time.
- Share benchmark timing; pause adaptation while other GPU profiling is armed.
- Accept valid QVFR handle 1 when uniquely identified in the native eye vector.
- Headset compatibility and performance gains still require hardware testing.

## 0.2.0 - 2026-11-02

- **Rendered crop is still highly experimental.** Please test and report image,
  menu/HUD, head-motion or performance issues through GitHub Issues. Include your
  headset/runtime/provider, crop settings and reproduction steps. Reset crop to
  100/100 to disable it. No reliable FPS or CPU improvement has been demonstrated.
- Keep menus and HUD at their uncropped physical size when vertical crop changes
  LMU's UI mesh. Preserve native curvature, depth and saved scale preferences.
- Fix rendered crop with QVFR: embed the smaller peripheral render into a black
  full-FOV output so QVFR does not stretch it against the focus views. Preserve
  submitted pose compensation and focus geometry. This adds one peripheral blit
  per eye when cropping; full-size output swapchains remain allocated.
- Allow the independently configured VectorXR overlay alongside mbucchia QVFR;
  remove the blanket VectorXR layer block in QVFR GUI and CLI launches.
  Keep VectorXR's own Quadviews feature off when QVFR is the selected provider.

- Add experimental NVIDIA Stereo VRS as an alternative to Quad Views. It keeps
  two original eye renders, with 1x1 shading inside the focus rectangle and
  selectable 1x1, 2x2 or 4x4 peripheral shading in verified world-sort scopes.
- Auto gaze uses OpenXR eye-gaze input when available; missing, inactive or
  invalid gaze falls back to the fixed focus region. Focus size/offsets apply
  per eye. Unsupported VRS hardware retains full-rate stereo.
- Preserve all 0.1.2 quad crop, pose compensation, offset, benchmark overlay,
  joystick capture, report and diagnostic features. Quad remains the default.
  VRS uses ordinary stereo resolution/in-game crop rather than quad-only sizing.
- Add explanatory hover help, an explicit experimental VRS selector and shading
  rate control. Existing experimental CPU upload reuse/fast-path switches remain
  available in Quad Views mode.
- Integrate `experiment/performance`: optional outer CPU upload reuse and smaller
  outer SSR resources, with dependency-aware Experimental toggles and hover help.
  Both are off by default and Quad Views-only; SSR can soften reflections.
- Full native SPS remains unfinished and is not included as a 0.2.0 feature.
  VRS can reduce shading work; removing extra quad traversals may reduce CPU work.
  No measured LMU FPS or frametime improvement is claimed.

## 0.1.2 - 2026-11-02

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
