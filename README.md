# LMU Quad View Mod

Experimental Windows x64 OpenXR layer and renderer integration for Le Mans Ultimate. It adds two peripheral views and two gaze-directed focus views. This is an independent project, with no Studio 397 endorsement or anti-cheat approval.

This repository is private. The source is available to authorized recipients under **PolyForm Noncommercial 1.0.0**. That permits reuse, modification and distribution for permitted noncommercial purposes; it is not an OSI open-source license. Third-party dependencies retain their own licenses. This license grants no rights to LMU itself or its assets.

## Run the portable package

1. Extract the complete release ZIP into a writable folder outside the game installation.
2. Enable QuadView Foveated Rendering and eye tracking in Pimax EVO, with native Pimax OpenXR active.
3. Open `LMU-QuadView-Launcher.exe`, select the LMU installation, and launch the game. Use an offline session.
4. Enable the optional green outline to see the runtime's focus region. Change center/edge resolution and horizontal/vertical focus FOV in Pimax EVO. Restart LMU after changing those settings.

Keep the package together. There is no need to copy the DLL into LMU. The launcher enables the OpenXR layer for the launched process; it does not modify the game executable or register a global layer. Close LMU before relaunching. Launching normally without the launcher omits this mod.

Requirements: Windows x64, PowerShell 7, .NET Framework 4.x, D3D11, and an OpenXR runtime exposing the required quad-view support. Native Pimax OpenXR has been visually tested with an RTX 5090 and Dream Air headset. Other runtimes, headsets, GPUs and extra API layers require testing. Signing the DLL does not confer anti-cheat approval.

Supported game executable SHA-256:

```text
66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF
```

The launcher and native code enforce the supported build. A game update needs new validation; do not remove the version checks.

## How it works

The layer presents a stereo interface to LMU while requesting `XR_VIEW_CONFIGURATION_TYPE_PRIMARY_QUAD_VARJO` from the runtime. It uses the runtime's four poses/FOVs and recommended view dimensions. Verified, build-specific D3D11 renderer hooks repeat the scene rendering for the two narrow focus views, preserve the outer scene images, and submit a four-view projection layer.

For supported resource layouts, a separate native render-target bank renders the outer main scene into smaller HDR, depth and final buffers. Bindings and viewport dimensions are restored before focus rendering. This reduces actual outer rendering work; it is not merely resizing a completed full-resolution image. Some SSR and post-processing buffers remain full-sized.

Pimax/runtime settings control the outer dimensions, focus dimensions and focus FOV. The launcher adds no resolution multiplier. The engine uses a common focus buffer with the larger width and height of the two focus recommendations. Each outer buffer retains its own recommended dimensions, which must fit inside that common focus buffer. Legacy experimental CLI overrides remain in the source but are not used by the current GUI.

This is a custom LMU integration, not mbucchia's Quad-Views-Foveated project. It supplies real four-view output through renderer hooks rather than native game support. It does not add DLSS.

## Compatibility and current fix

**Current working tree: crop/eye-identity test candidate, pending confirmation on the two reported tester setups.** See [tester instructions](TEST-CROP-IDENTITY.txt). The earlier freeze-fix confirmation below applies to the previous alpha build.

Auxiliary swapchains such as VectorXR's OSD pass through separately from game-eye buffers. The earlier OSD initialization fix has been confirmed in a live session. In this candidate's runtime-settings mode, the verified native eye-handle vector identifies game-eye swapchains; creation order and matching dimensions alone do not. Regression tests cover an auxiliary buffer with identical dimensions created before the game buffers. The final view-size answer reaching the supported LMU build is normalized to the runtime focus dimensions and logged at both boundaries. An upper API layer that wraps eye handles or rewrites swapchain creation may still be incompatible; the source of the first tester's larger swapchains remains unconfirmed.

This candidate bypasses LMU's horizontal/vertical FOV crop during native eye allocation and projection in runtime-settings quad mode. Saved crop preferences remain unchanged, and Pimax/runtime focus FOV and resolution settings still apply. This avoids treating a cropped native eye texture as a complete uncropped eye. It is a compatibility workaround, not support for rendering with LMU's crop enabled. Proper crop support is tracked in [TODO.md](TODO.md). Existing resource-size validation, UNORM/sRGB format-family handling and color-space behavior remain intact.

The menu-to-track freeze fix separates a recoverable small-bank failure from a fatal rendering-state failure. If small-bank creation is unsupported, live quad rendering continues with a full-resolution outer render resampled to the runtime's requested outer output. **This compatibility fallback does not provide the smaller outer scene's GPU savings.** The log states when fallback is active and records the rejected resource role/stage. Unsafe dimension or restoration failures still block submission.

The fallback regression test reproduces the previous scene-submission suppression and passes with the fix. On 29 October 2026, the reporting user confirmed that the updated build works on the affected setup. This confirms recovery from the freeze, not reduced outer-render GPU work. The precise resource incompatibility remains unconfirmed without a new diagnostic log. Do not assume changing anti-aliasing is a proven solution.

Temporal history is not fully isolated for every engine effect. Performance varies with settings and scene, and no universal FPS gain is claimed. This is an experimental offline build, not an approved online integration. LMU's license and applicable law must be considered independently of this project's license.

Logs and local launcher preferences can contain installation paths and other personal information. They are excluded from the repository and release package. Review logs before sharing them.

## Build and test

Install a Windows x64 MinGW-w64 toolchain providing `gcc` and `g++` with C++17 support, PowerShell 7, and the .NET Framework x64 C# compiler. D3D11 WARP must be available for the graphics tests. Dependencies needed to compile are vendored with their license notices.

From the repository root in PowerShell:

```powershell
./mod/build.ps1
./mod/build-launcher.ps1
./package.ps1
```

`build.ps1` runs native math, hook policy, crop normalization, view-size boundary, eye identity, fallback, timing, statistics, render-target, state-restoration, and fake-OpenXR-runtime tests, then builds the DLL. WARP tests validate graphics helpers without a headset. They do not prove live engine or compositor correctness. The current candidate still requires the menu/track/return/relaunch checks on the affected PCs, including a run with native outer optimization disabled.

The scope-policy test runs without a game dump. An optional private compatible PE image can be supplied with `-AnchorImage <absolute-path>` for additional anchor validation; it is neither required nor distributed. Runtime build and code-anchor checks remain enabled independently.

`package.ps1` creates `dist/LMU-Quad-View-Mod.zip` using an explicit file list, licenses and SHA-256 manifest. It excludes settings, logs and debug artifacts. Use the whole ZIP for testing on another PC.

## License

Original project code: [PolyForm Noncommercial 1.0.0](LICENSE.md).
Dependencies: [third-party notices](THIRD_PARTY_NOTICES.md).
