# TODO

- [ ] Validate motion-compensation candidate on Qubic with crop off, then test
  mod-owned crop independently (menu/track, gaze edges, outline, Native/QVFR/VectorXR).
- [ ] Investigate frame-boundary live FOV controls and coordinated resolution
  reallocation. Current launcher settings require an LMU restart.

- [ ] Research full native single-pass stereo for a possible 0.2.0 release.
  Prepare compatible work once for both eyes while preserving visibility,
  ordering, intermediate color/depth buffers, focus tracking and GPU savings.
  Require image/depth comparisons and matched CPU/GPU/P95/P99 measurements before
  enabling it for normal play. Version 0.1.1 adds focus offsets independently.

- [ ] Deferred: support LMU's in-game horizontal and vertical FOV cropping in quad mode.
  The mod now provides independent crop controls; this item concerns LMU settings only.
  The current crop-compatibility candidate bypasses crop values for native render
  allocation and projection while preserving the saved setting. Proper support
  must map cropped render extents, per-eye offsets, and projection/subImage FOVs
  consistently for outer and focus views. Validate asymmetric crops, eye tracking,
  menu/track transitions, and Pimax resolution changes. Do not stretch cropped
  content into an uncropped FOV or change the intended color-space handling.

- [ ] Validate the crop/identity candidate on both reported tester setups.
  Log 1 still needs evidence identifying which boundary introduced the larger
  swapchain dimensions. Upper layers that wrap native eye handles or rewrite
  swapchain creation remain unqualified.
