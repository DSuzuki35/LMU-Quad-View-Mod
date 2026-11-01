VECTORXR AND DIAGNOSTICS

VectorXR is an optional provider alongside Native and QVFR. Install it separately,
enable its LMU Quadviews profile, select VectorXR in Rendering, and configure
resolution/focus in its app. See PORTABLE-VECTORXR.txt.

Setup -> Enable diagnostics and logging: untick and relaunch to stop all mod logs,
run metadata and profiling. The setting is saved. Provider/runtime logging is
separate. Re-enable it for performance captures; see RENDER-PROFILING.txt.

LMU QUAD VIEW MOD - DIRECT LAUNCHER TEST BUILD

Extract the WHOLE ZIP into a new writable folder outside LMU.
Open LMU-QuadView-Launcher.exe. PowerShell 7 and script unblocking are not needed.
Windows x64 / .NET Framework 4.8 and D3D11 are required. The EXE is unsigned;
Windows reputation or download warnings may still appear.
Keep LMU-QuadView-Launcher.exe.config beside the EXE for per-monitor DPI support.

Setup: select the LMU folder and the headset's active OpenXR runtime.
Rendering: choose Native OpenXR or Quad-Views-Foveated, then Auto or Fixed.
Native uses the runtime's resolution/FOV settings (such as Pimax EVO).
QVFR requires mbucchia's separately installed Quad-Views-Foveated. The Setup page
has its official download/instructions link and optional manifest file selector.
OpenXR Toolkit is not required.

In QVFR mode adjust outer/inner resolution and independent focus width/height
inside this app. Save settings and Launch apply an LMU-only profile and back up
existing QVFR settings before changes. Other application sections are preserved.
No manual configuration file or setup script is required.
Resolution percentages scale each dimension: 200% means 2x width and height,
therefore 4x pixels. Focus dimensions use QVFR's supported 10-90% range.
Use the green outline to see focus placement. Auto needs a working gaze provider;
Fixed keeps focus stationary. Close and restart LMU after changing settings.

Outer smoothing is optional (0 = off). It softens only the peripheral scene,
leaving the high-resolution focus image sharp. Start with 0.5-1 pixel. It adds
texture sampling and may add a GPU pass; its performance cost has NOT yet been
benchmarked in a headset. It is not a replacement for temporal anti-aliasing.

The smaller outer-target optimization can be disabled in Setup for diagnosis.
In-game crop support is still deferred; the crop-bypass workaround remains.
The pinned game-build guard, resource checks and gamma handling remain enabled.
Launch LMU offline. No game executable is modified and no global layer or runtime
registration is changed. No anti-cheat approval is claimed.

For support include the actual quad-*.log from logs/, not only the *-run.json
launch summary. Logs/settings/per-run manifests may contain personal paths;
review before sharing. A started process is not proof of valid VR rendering.

See PORTABLE-QVFR.txt for backend compatibility and troubleshooting.
Original code: PolyForm Noncommercial 1.0.0 (LICENSE.md).
Third-party notices and licenses are included. QVFR is a separate MIT project.
