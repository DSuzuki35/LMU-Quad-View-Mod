# Changelog

Release numbers use `MAJOR.MINOR.PATCH`. The root `VERSION` file supplies the
launcher title, Windows EXE/DLL metadata and default portable ZIP name.
Versions before 1.0 remain experimental and may change compatibility or settings.

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
