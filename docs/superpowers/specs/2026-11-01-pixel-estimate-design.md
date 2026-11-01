# Pixel estimate and comparison

The user chose a live estimate with a **Use as comparison** button to understand
resolution changes such as 300% to 200%. Continue autonomously under the existing
instruction; no new release or replacement of the published 0.1.1 assets.

## Approach

A launcher-only calculator is preferable to a GPU counter: it updates before a
game restart and adds no game overhead. A count without comparison would not
directly answer the user's savings question. A full performance profiler measures
different work and already exists separately.

Show estimated outer, focus and total view pixels for both eyes. With a matching
latest run's actual swapchain sizes and launch metadata, use megapixels. Without
that measurement, show a normalized percentage of full-view 100% stereo pixels
and still allow comparisons. The baseline initially uses the loaded settings and
can be replaced with the button. Slider changes update immediately.

QVFR scales each dimension linearly: relative outer area is outerScale squared;
focus area is innerScale squared times provider width and height fractions. Use
the existing equal-axis conversion before calculation. Actual measured sizes
anchor estimates, including the common focus buffer used for both eyes. Predict
changes with these area ratios; runtime limits/rounding can differ. Offsets do not
affect counts. With low-resolution outer rendering disabled, both outer views
render at focus-buffer size; account for that instead of claiming smaller outer
rendering. These are steady-state view-area estimates, excluding warmup/fallback,
MSAA, overdraw, shadows, post-processing and compositor work, not FPS predictions.

Only QVFR settings are editable here. Native/VectorXR show a matching last-run
view-area count, clearly historical; no estimate from inactive QVFR sliders and no
comparison button. Read only the latest log plus its paired metadata, with bounded
reads and validation. Never substitute an older matching headset/provider run.
Show the measurement time and instruct refresh after headset/runtime changes.
No per-slider filesystem reads, renderer changes, saved-setting schema changes,
provider writes, new dependencies, or automatic game launches.

## Validation

Independent arithmetic cases: 300->200 focus area 4/9, total includes outer;
measured asymmetric outer eyes/common focus, changed box size, equal-axis mapping,
native-outer disabled, invalid and missing/latest mismatched run data, large
dimensions without integer overflow, and locale-invariant metadata parsing.
GUI checks cover live updates, pinned baseline, inactive providers, offsets, and
existing default/minimum simulated DPI layouts. Rebuild launcher; native code is
unchanged. Real headset and physical monitor transitions are outside this pass.
