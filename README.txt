LMU QUAD VIEW MOD — v0.1.0

Free experimental Windows x64 / D3D11 quad-view integration for Le Mans Ultimate.
Always free; donations are completely optional. Use offline. No Studio397
endorsement or anti-cheat approval is claimed.

INSTALL
Extract the WHOLE ZIP to a writable folder outside LMU. Open
LMU-QuadView-Launcher.exe and keep its .exe.config file beside it.
Requires .NET Framework 4.8. No PowerShell installation or script unblocking is
needed to run the distributed launcher. The EXE is unsigned.

In Setup select the LMU folder. Ensure the headset's OpenXR runtime is active.
In Rendering select Native OpenXR, Quad-Views-Foveated (QVFR), or VectorXR.
Select Auto or Fixed, save, then launch LMU offline. Restart after settings changes.

PROVIDERS
Native requires a quad-view runtime and uses its resolution/FOV settings.
QVFR must be installed separately; Setup links to upstream instructions and
accepts an optional manifest path. See PORTABLE-QVFR.txt.
VectorXR must be installed separately with its LMU Quadviews profile enabled.
Select VectorXR and configure resolution/focus/tracking in its app; the launcher's
QVFR sizing controls are inactive. See PORTABLE-VECTORXR.txt.
OpenXR Toolkit is not required. The game EXE, global OpenXR registration and active
runtime are not modified. LMU-only QVFR profile changes are backed up.

SETTINGS
Resolution percentages scale width and height: 200% means four times the pixels.
QVFR focus dimensions support 10–90%. Run LMU once, then use Read latest headset
FOV for equal-axis controls: 50/50 is approximately square near the center.
Before calibration fresh controls show per-axis percentages. Old settings retain
their region. Re-read after changing headset, runtime or FOV; this is a snapshot.
QVFR may widen/clip focus near gaze limits. Fixed requests stationary focus;
Auto depends on provider tracking capability. Use the green outline to inspect it.
Outer smoothing defaults off; it leaves focus sharp but adds unmeasured GPU cost.
The smaller outer-target optimization can be disabled in Setup for diagnosis.
Crop support is deferred. Keep the pinned game-build guard enabled.

LIMITS
Original eye draws plus two focus scene traversals remain. Shared-path CPU
housekeeping improvements are not full single-pass stereo or a confirmed 4–5 ms
fix. Experimental upload reuse/batching and paired-draw checks default off.
Compatibility and performance still require headset testing.

SUPPORT AND PROFILING
Report version, game build, headset/GPU/driver, runtime/provider, settings and
reproduction steps in GitHub Issues. Include logs/quad-*.log, not only *-run.json.
Review personal paths before sharing logs or settings.
Setup -> Enable diagnostics and logging controls mod logs, metadata and profiling
on the next launch; provider/runtime logging is separate. For captures enable
render profiling, select 30–300 seconds and press Ctrl+Alt+F9 while driving.
Close normally to save partial results. See RENDER-PROFILING.txt for limitations.

See README.md for the full overview and CONTRIBUTING.md in the source repository
for build/test and contribution guidance. Source license: PolyForm Noncommercial
1.0.0 (LICENSE.md), source-available, not OSI open source. Third-party licenses
remain separate. No game or asset rights are granted.
