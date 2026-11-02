# LMU Quad View Mod

A free Windows x64 / D3D11 mod that adds two outer and two focus views to
**Le Mans Ultimate** through OpenXR. Use a native quad-view runtime, or install
Quad-Views-Foveated (QVFR) or VectorXR separately to provide quad views on a
stereo runtime.

**v0.1.1 is an experimental offline release.** Headset and runtime compatibility
still needs broader testing. Always free; donations are completely optional.

## About this project

I built this project in my spare time with substantial help from AI. I'm an
engineer with a good understanding of technology, but I'm not a software engineer
and I don't claim to be one.

It started as an experiment: could this be done, and could I make it work? It
gradually became something that reduces GPU load so much on my own setup that
I want to make it work reliably and keep optimizing it. Those results are my
motivation, not a promise of the same improvement on every system.

If AI-assisted code isn't your thing, you're welcome to skip this project.
If you'd like to help improve it, thoughtful bug reports, testing and code
contributions are welcome. See [the contribution guide](CONTRIBUTING.md).

## Install and launch

1. Download `LMU-Quad-View-Mod-0.1.1.zip` from [GitHub Releases](https://github.com/DSuzuki35/LMU-Quad-View-Mod/releases/tag/v0.1.1).
2. Extract the **complete ZIP** to a writable folder outside the LMU installation.
3. Open **LMU-QuadView-Launcher.exe**. Keep its `.exe.config` file beside it.
4. In **Setup**, select the LMU game folder. Ensure your headset's OpenXR runtime
   is active, and choose your provider in **Rendering**.
5. Choose Auto or Fixed, save settings, and launch LMU offline. Restart LMU after
   changing rendering settings.

Requires Windows x64, D3D11 and .NET Framework 4.8. The distributed launcher does
not require PowerShell or script unblocking. The EXE is unsigned, so Windows may
show reputation warnings. OpenXR Toolkit is not required.

| Provider | Setup and settings |
| --- | --- |
| Native OpenXR | Requires a runtime exposing quad views. Resolution and FOV come from runtime settings. |
| Quad-Views-Foveated | Install mbucchia's QVFR separately; Setup links to upstream instructions and can select its manifest. Configure resolution and focus size in the launcher. See [QVFR instructions](PORTABLE-QVFR.txt). |
| VectorXR | Install separately, enable its LMU Quadviews profile, and select VectorXR. Setup can locate its manifest or accept a JSON path. Configure resolution, focus and tracking in VectorXR; launcher QVFR sizing controls are inactive. See [VectorXR instructions](PORTABLE-VECTORXR.txt). |

The launcher starts LMU with process-local provider settings and layer ordering.
It does not modify the game EXE, global OpenXR registration or active runtime.
QVFR profile changes are scoped to LMU, backed up and applied atomically. VectorXR
launch preparation preserves installed manifests and disables conflicting layers
for the launched process.

## Settings and calibration

Auto queries the provider's tracking capability before requesting tracked or
fixed focus views; working gaze tracking depends on the provider. Fixed requests
stationary focus views. Use the green outline to inspect focus placement.

**Focus position** adds horizontal and vertical offsets for all three providers,
in both Auto and Fixed modes. Positive horizontal moves right; positive vertical
moves up. Negative values move left/down. Values range from -50% to +50% of the
full view on each axis, measured in projection space. For example, +5% horizontal
shifts the region right by 5% of that eye's full projected view width.

In Auto, the offset is relative to the provider's current gaze position; eye
tracking continues to move the region. The shift does not change its projected
size or render resolution. It is not clamped to the outer-view edges, so large
offsets can put part of the region outside the visible view when crop is off. **Reset offsets**
sets both to zero, restoring provider placement exactly. Save and restart LMU
after changing offsets. Existing settings default to zero.

QVFR exposes outer/inner resolution and independent focus width/height. Resolution
percentages scale each dimension: 200% means twice the width and height, or four
times the pixels. Focus dimensions use QVFR's supported 10–90% range.

For equal-axis focus controls, run LMU once, then click **Read latest headset
FOV**. With a measured FOV, 50/50 produces an approximately square region near the
center. Before calibration, a fresh installation explicitly displays per-axis
percentages. Existing settings preserve their region. Calibration reads the latest
local run; it is a snapshot, not live headset detection. Re-read after changing
headset, runtime or FOV. QVFR can widen or clip the region near gaze limits.
Stored percentages and native rendering are unchanged.

Outer smoothing defaults to off and leaves the focus image sharp. Start with
0.5–1 pixel if desired. It adds GPU work; headset cost has not been measured.
It does not replace temporal anti-aliasing. The smaller outer-target optimization
can be disabled in Setup for diagnosis. In-game crop support remains deferred.

**Rendered FOV / crop** controls the mod's own crop independently of LMU's saved
crop settings. Width and height each range from 25–100%; 100/100 is the full view.
Lower values trim the centered projected view without zoom, leaving uncovered
edges black. Outer target dimensions shrink with the retained tangent-space FOV;
75% width and 50% height use approximately 37.5% of the original outer pixels.
This is not a total GPU/frame-time saving: focus textures keep their resolution,
PPFX intermediates may remain full-size, and the full-size outer fallback loses
the native outer-render saving. Focus follows gaze until the crop boundary, where
it is kept inside; a focus region larger than the crop is narrowed to fit.
Save and restart LMU to apply. Resolution and FOV controls are not live controls.

The motion-compensation candidate preserves incoming projection poses and transfers
their correction to the focus views, including when a layer changes reference space.
Fake-runtime regressions pass; actual Qubic compensation and cropped composition
still need headset validation. Disable crop (100/100) for the first Qubic A/B test.

## What this release does

The adapter presents stereo to LMU and submits four rendered views downstream.
The game still performs its original eye draws plus **two additional focus scene
traversals**. A smaller native outer render-target bank reduces outer work where
compatible; otherwise full-size outer rendering and resampling provide a fallback.
Auxiliary swapchains are identified using native eye handles rather than size or
creation order.

Shared-path CPU housekeeping avoids per-frame heap allocations for submissions
of up to 16 layers (larger lists use a fallback), borrows the outer RTV with cached
dimensions, and skips D3D state capture when there is no outer UI draw. These are
not full single-pass stereo, and no confirmed 4–5 ms frametime improvement is
claimed. See the [measured results and comparison procedure](docs/PERFORMANCE.md).

Experimental upload batching/reuse and private paired-draw checks remain disabled
by default. They require explicit opt-in and do not remove normal eye draws.
See [CPU fast-path research](CPU-UPLOAD-FASTPATH.md),
[upload reuse](CPU-UPLOAD-REUSE.md), [stereo research](PORTABLE-STEREO-RESEARCH.txt)
and the [renderer handoff](INSTANCED-STEREO-HANDOFF.md) for their scope and limits.

Full single-pass stereo is a **0.2.0 research target**, not part of 0.1.1 or a
guaranteed release date. It still needs shared native preparation, correct
per-eye intermediate buffers and render order, image/depth equivalence and
matched CPU/GPU measurements. The [SPS research branch](https://github.com/DSuzuki35/LMU-Quad-View-Mod/tree/codex/sps-0.1.1)
retains its historical name; its additional diagnostics are excluded from this
offset release.

Supported LMU executable SHA256:
`66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF`.
The build guard and resource checks must remain enabled. A game update needs
revalidation. No Studio397 endorsement or anti-cheat approval is claimed.
Broad headset support is a target, not a guarantee.

## Diagnostics and support

Report bugs through this repository's GitHub Issues. Include the mod version,
LMU build, headset, GPU/driver, runtime/provider versions, rendering settings,
reproduction steps and expected versus actual result. Attach the actual
`logs/quad-*.log`, not just the matching `*-run.json` launch summary. Review files
for personal paths before sharing. A started process alone does not establish
correct VR rendering.

**Setup → Enable diagnostics and logging** controls mod logs, per-run metadata
and diagnostic collection on the next launch. It defaults to enabled. Provider
and runtime diagnostics are controlled separately.

For a performance report, enable diagnostics and **Enable render profiling**,
choose 30–300 seconds (120 default), then press **Ctrl+Alt+F9** while driving.
Exit normally to save partial captures. Forced termination may lose final results.
CPU scopes overlap and include waits; sparse GPU timings omit some work. Follow
[profiling instructions](RENDER-PROFILING.txt) for matched comparisons.

## Build and contribute

See [CONTRIBUTING.md](CONTRIBUTING.md) for Windows x64 prerequisites, exact build
and test commands, review expectations and release steps. The
[architecture guide](docs/ARCHITECTURE.md) maps the launcher, XR adapter, hooks and
resource ownership. Native builds run tests; graphics tests use D3D11 WARP and
cannot replace headset QA. Release versions come from the root `VERSION` file.

## License

Original source is licensed under [PolyForm Noncommercial 1.0.0](LICENSE.md):
source-available for permitted noncommercial reuse, **not OSI open source**.
[Third-party licenses](THIRD_PARTY_NOTICES.md) remain separate. No rights to LMU or
its assets are granted. The mod is always free; donations are optional.
