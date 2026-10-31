# LMU Quad View Mod

This research branch adds an opt-in [CPU upload-reuse prototype](CPU-UPLOAD-REUSE.md),
portable vertex/pixel shader conversion and an in-game private paired-draw check.
The current [CPU fast-path candidate](CPU-UPLOAD-FASTPATH.md) combines unchanged
upload filtering and adaptive buffer batching for outer and focus geometry.
The CPU prototype filters byte-identical native uploads within a focus geometry
group. The stereo check compares one paired draw with two original references in
private targets; normal game eye draws remain enabled. Full game-renderer single-
pass stereo is still in development. No live-game CPU/FPS gain has been measured.
See [research status, capture steps and hardware check](PORTABLE-STEREO-RESEARCH.txt).
For continuing the renderer work, read the [maintainer handoff](INSTANCED-STEREO-HANDOFF.md),
which separates completed GPU checks from the unimplemented native pass integration.

Experimental Windows x64 / D3D11 renderer integration for Le Mans Ultimate.
It renders two outer and two focus views, with native OpenXR quad runtimes or
mbucchia's separately installed Quad-Views-Foveated or VectorXR stereo compositor.

## Run

Extract the complete ZIP and open **LMU-QuadView-Launcher.exe**. No PowerShell
installation, setup script or script-unblocking step is required. Select the
game folder in Setup, then provider and mode in Rendering. .NET Framework 4.8
is required. The unsigned EXE may still trigger Windows reputation warnings.
Keep the included `.exe.config` next to the launcher for per-monitor DPI support.

QVFR mode exposes outer/inner resolution and independent box width/height.
Build **2026.10.30.1** adds equal-axis focus controls: after measuring the headset
FOV, 50/50 produces an approximately square region near the center. The launcher
reads the latest local run's outer FOV and converts the displayed values to QVFR's
per-axis fractions. Old settings preserve their existing region. On a fresh
installation, controls explicitly remain per-axis percentages until you run LMU
once and click **Read latest headset FOV**. Re-read after changing headset,
runtime or FOV; calibration is a snapshot, not live headset detection. QVFR can
widen/clip the region near gaze limits. Stored percentages and native rendering
are unchanged. Always free; donations are completely optional.
Save or Launch applies a backed-up LMU-only provider profile. Native mode uses
runtime settings. Optional outer smoothing defaults to off and leaves focus
sharp. It adds GPU work; no measured performance cost is claimed yet.

Read [README.txt](README.txt) and [portable backend instructions](PORTABLE-QVFR.txt).
The Setup page links to the upstream QVFR installation instructions. QVFR is not
bundled. OpenXR Toolkit is not required. Crop work remains paused.

### VectorXR

Install VectorXR separately, enable its Quadviews profile for LMU, and select
**VectorXR** in Rendering. Setup can locate its manifest automatically or accept
an explicit JSON path. Configure resolution, focus size and tracking in VectorXR;
the launcher's QVFR sizing controls are inactive for this backend. Native and QVFR
remain available, and the default provider is unchanged.

The launcher prepares a process-local explicit provider manifest and disables
duplicate or conflicting QVFR/Toolkit layers for the launched game. It preserves
the installed manifests, registry settings and active runtime. Eligible VectorXR
color swapchains request sampled usage to allow direct compositor inputs, with
fallback to original usage if unsupported. See [VectorXR setup](PORTABLE-VECTORXR.txt).

### Diagnostics and render profiling

Setup's **Enable diagnostics and logging** switch disables mod logs, per-run
metadata and diagnostic collection together on the next launch. The preference
is saved and defaults to enabled to retain existing logging behavior. VectorXR
and runtime diagnostics remain separately controlled in their own applications.

When diagnostics is enabled, tick **Enable render profiling**, choose 30-300
seconds (120 default), and press **Ctrl+Alt+F9** while driving. Captures include
CPU scope statistics and sparse asynchronous GPU pass timings. Samples close at
XR frame boundaries, with menu work reported separately. Normal session shutdown
saves a partial capture and flushes the log; checkpoints retain partial means if
normal teardown does not occur. Forced termination cannot guarantee final results.
See [profiling instructions and limitations](RENDER-PROFILING.txt).

## Implementation and limits

The adapter exposes stereo to LMU and submits four rendered views to the provider.
Pinned renderer hooks repeat focus rendering; an optional smaller render-target
bank reduces outer work where supported. Incompatible small banks use a full-size
outer render and resampling, which loses those savings. Auxiliary swapchains are
identified separately using native eye handles. Runtime sizes, formats, poses and
resource state remain validated. Fixed mode requests stationary focus views; Auto
queries provider tracking capability before selecting tracked/fixed requests.

The C# launcher starts the game directly, preserves the build guard, sets only
child environment variables and prepares LMU-before-QVFR layer order. It does not
modify the game EXE, global OpenXR registration or the active runtime. User QVFR
configuration changes are scoped to LMU, atomic and backed up.

Supported LMU executable SHA256:
`66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF`.
A game update requires revalidation. This remains an experimental offline mod;
no Studio397 endorsement or anti-cheat approval is claimed. Broad headset support
is a target, not a guarantee. Live headset compatibility and smoothing cost still
need measurement. Logs/settings can contain personal paths and are not packaged.

## Development

Build native code with the Windows MinGW-w64 toolchain (`./mod/build.ps1`) or
Visual Studio Build Tools with the C++ workload (`./mod/build-msvc.cmd`). Both
scripts run native tests before building the DLL. Build the .NET Framework C#
launcher with `./mod/build-launcher.ps1`, then run `./package.ps1`.
These are developer build scripts; the distributed app does not invoke them.
Graphics tests use D3D11 WARP; native/fake-runtime tests do not replace headset QA.

## License

Original source: [PolyForm Noncommercial 1.0.0](LICENSE.md), source-available for
permitted noncommercial reuse, not OSI open source. [Third-party licenses](THIRD_PARTY_NOTICES.md)
remain separate. No rights to LMU or its assets are granted.
