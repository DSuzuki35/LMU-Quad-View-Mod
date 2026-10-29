# TODO

- [ ] Support LMU's in-game horizontal and vertical FOV cropping in quad mode.
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
