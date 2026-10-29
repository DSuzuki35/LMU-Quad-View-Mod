LMU Quad View Mod - experimental offline build

Extract the whole ZIP into a writable folder outside LMU.
Run LMU-QuadView-Launcher.exe, choose the LMU installation, then launch.
PowerShell 7 and Windows x64/.NET Framework 4.x are required.
Do not copy the DLL into LMU; the launcher enables it for the game process.

Enable QuadView Foveated Rendering and eye tracking in Pimax EVO, using native
Pimax OpenXR. Set center/edge resolution and horizontal/vertical FOV in EVO.
Restart LMU after changing these settings. The launcher adds no extra scaling.
The optional green outline shows the focus region. Close LMU before relaunching.

CROP / EYE IDENTITY TEST CANDIDATE
This package is a new candidate awaiting confirmation on the affected PCs.
See TEST-CROP-IDENTITY.txt for the test sequence and diagnostic launch script.
LMU's saved FOV crop is bypassed during runtime-settings quad rendering; the
saved preference is unchanged. Proper in-game crop support remains a TODO.
Pimax/runtime focus FOV and resolution settings continue to apply.
Native eye handles now identify game buffers independently of creation order
and size. Logs trace the final view sizes reaching the game and eye resources.
Upper layers that rewrite swapchain sizes or wrap handles still need testing.

EARLIER MENU-TO-TRACK FREEZE UPDATE
Unsupported small render-target layouts now fall back to a full-resolution
outer render, resampled to the runtime's outer output. Quad scene submission
continues. This fallback loses the small outer render's performance savings.
Logs identify fallback and the rejected resource role/stage. This update has
regression-test coverage and was confirmed working on the affected PC on
29 October 2026. This confirms freeze recovery, not a performance gain.
If testing this issue, share the new quad log after reaching the track, after
reviewing it for personal paths. Do not assume MSAA is the proven cause.

Keep the game-build guard enabled. No game executable is replaced. Launching
normally omits the mod. Offline use only: no anti-cheat approval is claimed.
See README.md for implementation, limitations and build instructions.
Original code: PolyForm Noncommercial 1.0.0 (LICENSE.md).
Dependencies retain their own licenses (licenses/ and THIRD_PARTY_NOTICES.md).
