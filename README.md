# LMU Quad View Mod

Experimental Windows x64 / D3D11 renderer integration for Le Mans Ultimate.
It renders two outer and two focus views, with native OpenXR quad runtimes or
mbucchia's separately installed Quad-Views-Foveated stereo compositor.

## Run

Extract the complete ZIP and open **LMU-QuadView-Launcher.exe**. No PowerShell
installation, setup script or script-unblocking step is required. Select the
game folder in Setup, then provider and mode in Rendering. .NET Framework 4.8
is required. The unsigned EXE may still trigger Windows reputation warnings.
Keep the included `.exe.config` next to the launcher for per-monitor DPI support.

QVFR mode exposes outer/inner resolution and independent box width/height.
Save or Launch applies a backed-up LMU-only provider profile. Native mode uses
runtime settings. Optional outer smoothing defaults to off and leaves focus
sharp. It adds GPU work; no measured performance cost is claimed yet.

Read [README.txt](README.txt) and [portable backend instructions](PORTABLE-QVFR.txt).
The Setup page links to the upstream QVFR installation instructions. QVFR is not
bundled. OpenXR Toolkit is not required. Crop work remains paused.

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

Build with the existing Windows MinGW-w64 toolchain and .NET Framework C# compiler:
`./mod/build.ps1`, `./mod/build-launcher.ps1`, then `./package.ps1`.
These are developer build scripts; the distributed app does not invoke them.
Graphics tests use D3D11 WARP; native/fake-runtime tests do not replace headset QA.

## License

Original source: [PolyForm Noncommercial 1.0.0](LICENSE.md), source-available for
permitted noncommercial reuse, not OSI open source. [Third-party licenses](THIRD_PARTY_NOTICES.md)
remain separate. No rights to LMU or its assets are granted.
