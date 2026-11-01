# Pixel estimate implementation plan

> Execute inline with superpowers:executing-plans; fresh whole-change review at end.

**Goal:** Show useful pixel counts and savings before changing game settings.
**Architecture:** Pure C# calculator and bounded last-run reader; WinForms card.
**Tech stack:** Existing .NET Framework, C# WinForms, PowerShell build.
**Spec:** docs/superpowers/specs/2026-11-01-pixel-estimate-design.md

## Constraints and review focus

No native renderer work, runtime profiling, release replacement or dependencies.
Avoid stale provider measurements, overflow, confusing pixels with GPU time,
incorrect square-box calibration, and low-resolution outer savings when disabled.

- [x] Add arithmetic/parser regression tests, observe failure, implement
  `mod/launcher/PixelEstimate.cs` and register tests in `mod/build-launcher.ps1`.
- [x] Add estimate/comparison/refresh card and value-change notifications in
  `mod/launcher/Launcher.cs`; test actual UI behavior in `GuiLayoutTests.cs`.
- [x] Run launcher suite and inspect generated layout images; document limits
  in README/CHANGELOG. Review whole diff and resolve findings.
- [x] Commit a development branch/build, leaving published 0.1.1 unchanged.

