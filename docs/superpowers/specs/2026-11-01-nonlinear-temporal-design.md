# Gaze-change stability measurement

Continue the authorized standalone research inline. Add a measurement to the
sampling viewer that changes simulated gaze over a deterministic closed path,
first with a stationary camera and then with camera movement. Compare ordinary,
nonlinear and matched-budget quad outputs with a four-sample ordinary reference
at each position. Keep source budgets and material settings fixed during a run.

For each path, define error `e_t = output_t - reference_t` in linear RGB. Report
spatial error RMS and frame-to-frame error-change RMS `e_t - e_(t-1)`. Accumulate
a per-pixel error-change heatmap with the same amplification for every path.
This subtracts the reference's image changes when the camera moves; it does not
make the finite reference ground truth, register surfaces between frames, or
model human retinal motion. No temporal accumulation/history filter is added.

Validate the statistic with synthetic CPU arrays: static bias has zero temporal
change; equal output/reference motion cancels; known alternating residuals have
known RMS; alpha is ignored. GPU gates: stationary ordinary output is unchanged
under gaze movement, zero-warp/full-size nonlinear output matches ordinary
temporal behavior, trajectory return restores the same bytes, and actual warped
gaze motion changes source sampling. Capture and inspect the heatmap. Document
scope and results, then commit/push on the research branch only.
