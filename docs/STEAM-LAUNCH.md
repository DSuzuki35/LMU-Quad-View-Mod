# Steam startup

0.3.0 adds `LMU-QuadView-Launcher.exe --steam %command%` as a Steam launch
wrapper. Setup copies the complete quoted path for the current package.
Save settings first, paste into LMU's Steam Launch Options, then use Play.
Additional LMU arguments belong after `%command%`. Steam's forwarded executable
must match the saved game directory; the existing hash guard still validates it.

The wrapper starts the game directly with the normal launch preparation:
provider settings, OpenXR layer order, quad-only OpenGridXR isolation, diagnostics
and benchmark setup. It forwards arguments without using a command shell and
adds +XR if absent. The launcher minimizes after startup and closes after the
game exits. You can restore it from the taskbar for benchmark controls.

Missing or invalid settings and an unexpected executable show an error before
the game starts. Configure the correct game folder/provider in the launcher
first. A game update still requires validation against the supported build.

The package must stay at its configured location. If moved or upgraded to a new
folder, copy the new option. Removing the wrapper from Steam restores ordinary
startup; retain any previous game arguments you had. No Steam configuration is
silently edited by the launcher, and no game binaries or global layers change.

Sources: [Steam's Launch Options UI](https://help.steampowered.com/en/faqs/view/7D01-D2DD-D75E-2955).
Windows argument fidelity is covered with real child-process round-trip tests;
automated tests do not substitute for an actual Steam/headset session.
