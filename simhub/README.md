# SimHub plugin (experimental)

`LmuQuadView.SimHub.dll` publishes the mod's live frame timing, resolution and FOV
as SimHub properties, and adds two SimHub actions that can be bound to wheel or
button-box inputs.

## Requirements

- In the launcher: **Settings > Enable diagnostics and logging** and **Benchmark >
  Enable benchmark on next launch**. The data channel only exists with both on.
  The in-headset overlay can stay hidden.
- For the two actions, keep the launcher open (as for the launcher's own
  joystick bindings).

## Build and install

SimHub's plugin assemblies are part of your SimHub installation, so the plugin is
built on your PC. Close SimHub, then run:

```
powershell -ExecutionPolicy Bypass -File build-simhub-plugin.ps1 -Install
```

Start SimHub and enable **LMU Quad View** when it reports the new plugin.

## Properties

All appear under `LmuQuadView.` (for example `[LmuQuadView.Fps]`):

| Property | Meaning |
| --- | --- |
| `Connected`, `Stale` | Data channel found; no heartbeat for more than 3 s |
| `Mode` | `Quad views`, `Stereo VRS` or empty |
| `SceneActive` | A VR scene is being submitted |
| `Fps`, `FrameMsAvg`, `FrameMsMax` | Rolling one-second app frame rate and frame times |
| `FrameMs`, `AppMs`, `ProviderMs`, `GpuMs` | Milliseconds; GPU is the latest valid measurement within 2 s of sample time, cleared on leaving a scene |
| `OuterLeftResolution`, `OuterRightResolution`, `FocusResolution` | Render sizes such as `1200x1100` |
| `FovValid`, `FovLeftHorizontal`, `FovLeftVertical`, `FovRightHorizontal`, `FovRightVertical` | Rendered FOV in degrees |
| `ProjectionCount`, `NativeStatus`, `LastXrResult`, `DroppedSamples` | Diagnostics |
| `OverlayVisible`, `Recording` | Benchmark overlay and recording state |

Values without a measurement are empty (null), not zero.

## Actions

- `LmuQuadView.ToggleOverlay`: show or hide the in-headset benchmark overlay.
- `LmuQuadView.ToggleRecording`: start or stop a benchmark recording.

## Safety

The plugin opens the game's data channel **read-only** and never writes into the
game process. Actions are plain signals to the launcher, which performs them like
its own joystick bindings.
Command events allow only the current Windows user and SYSTEM, and the launcher
rejects pre-created channels. Both applications should run under the same user.

## Tests

In a source checkout, run `powershell -ExecutionPolicy Bypass -File test-telemetry.ps1`
from this folder. The test runner and native fixture are not included in the portable ZIP.
The runner needs g++ on PATH or an MSVC developer shell and .NET Framework 4.x.
It checks the reader against a fixture written by the native C++ telemetry writer,
including timing units, torn slots, invalid values, stale data and game-exit
reconnection, plus launcher/plugin action signaling. It does not launch LMU or SimHub.

## Limitations

- Settings and profiles cannot be switched from SimHub: rendering changes need an
  LMU restart.
- This mod does not draw SimHub dashboards into the headset image. Showing them
  in VR still depends on SimHub's own options or a separate overlay tool.
- Compiled during integration against the installed SimHub SDK. In-game telemetry,
  dashboards and physical button bindings still need validation in LMU and SimHub.
