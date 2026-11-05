# CPU and NVIDIA stereo research, 5 November 2026

This branch is an unsigned, offline-test candidate. It has not been installed
over the active game, signed or published. The native SPS/MVR scheduler is not
connected to the game renderer.

## NVIDIA experiments

The isolated backend and DXBC adapter cover MVR2/MVR4 shader interfaces and
mocked NVAPI submission. WARP checks cover generated shader creation, per-view
positions, generic attributes, constant banks, masks and native instance IDs.
The portable stereo converter regression tests also pass.

Two physical NVIDIA experiments returned `DXGI_ERROR_DEVICE_HUNG` during
readback, although the preceding NVAPI shader/mode/reset calls returned success.
LMU was running during these attempts, so these were not isolated GPU runs.
Concurrent game activity is a possible confounder, not an established cause.
Driver success codes and WARP correctness do not prove hardware correctness.

Do not enable this backend in LMU or run another hardware experiment until the
other task has released the GPU and an isolated test is arranged. The explicit
`--hardware` test remains opt-in. There is no demonstrated in-game SPS/MVR CPU
improvement. Classic SPS and arbitrary four-position MVR are distinct paths.

## Conservative native validation

Retained COM interfaces in the outer bank are AddRef-owned and already reuse
cached validation. Renderer objects, wrapper slots and native vectors are
borrowed. Their checks remain until renderer destruction/resize generations
and lifetime ownership are proven. A matching pointer alone is not that proof.

## CPU changes

Fresh launcher settings disable diagnostics, overlays, optional render
experiments and startup update checks. Explicit user opt-ins are preserved.
Diagnostic output uses a bounded worker queue and asynchronous flush requests.
The active-mode readers use atomic lifecycle snapshots. Repeated periodic
messages are sampled at ten-second intervals after initial diagnostics.

The Begin regression reproduces the Pimax late application Begin sequence,
checks the state mutex from another thread during runtime Begin, preserves a
pipelined Wait and rejects teardown while the runtime call is in flight.
Normalized Begin runs at the valid Locate boundary; D3D diagnostic setup stays
on engine access. Abandoned acquired images prevent early normalization.

Outer ownership tests check unlocked acquisition, blocking/nonblocking waits,
release and destruction, prefetched ownership, timeout retry and rejection of
borrowed render targets during an in-flight runtime operation.

These are correctness and regression checks. A new matched cockpit capture is
still needed before making a CPU-frametime improvement claim.
