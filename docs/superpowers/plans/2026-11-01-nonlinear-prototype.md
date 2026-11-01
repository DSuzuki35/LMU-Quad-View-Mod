# Nonlinear prototype plan

> Execute inline; the user explicitly requested no side agent.

**Goal:** Make nonlinear sample allocation visible and testable outside LMU.
**Architecture:** Pure JS mapping/math plus WebGL2 owned-scene renderer and inverse
reconstruction, with standard and quad-style comparators.
**Tech stack:** Browser WebGL2, JavaScript, Node built-in test runner/server.
**Spec:** docs/superpowers/specs/2026-11-01-nonlinear-prototype-design.md

## Constraints and review focus

No LMU hooks or settings, no game assets, no changes to SPS/released quad rendering.
Watch for missing image corners, off-center gaze failures, wrong sample-budget
comparisons, uncounted reconstruction costs, misleading software-GPU timing, and
confusing color sampling success with native triangle/depth/temporal integration.

- [x] Test and implement invertible mapping and budget calculation.
- [x] Build interactive owned-scene rendering and the three comparison paths.
- [x] Add GPU correctness/quality readback, optional timing, and automated browser
  checks; inspect source/reconstructed views and scripted gaze/camera cases.
- [x] Document measured results and limits, commit and push the separate research
  branch. Leave a reproducible local demo and clearly scoped next experiments.

