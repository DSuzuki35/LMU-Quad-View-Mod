# Live SPS CPU cost capture, 4 November 2026

The `af50e53` diagnostic completed its 120-second capture on the pinned game
executable with VectorXR fixed foveation, runtime-controlled targets and native
outer reduction. Layer SHA256:
`2CFB7D3FA3F01B0897D31C18E9E5F38BAB4AC61310AA55E65D103A6656513476`.
The local log is `quad-20261104-211450-8e0b8e.log`; private logs are not committed.
Runtime targets were 770x617 outer and 1589x1751 focus for each eye. Upload
experiments, VRS, SSR reduction, heavy audit and other profilers were disabled.
Engine startup and quad rendering succeeded; session teardown completed.

## Coverage passes; detailed timing needs follow-up

The capture observed 10,132 scene-frame boundaries and sampled 634 frames. Every
eye supplied 317 coarse and 317 detailed group samples: 2,536 groups total.
There were no dropped/invalid ranges or thread-cycle query failures. All nested
exclusive-category accounting checks pass within output rounding.

| Opaque group | Coarse mean ms/group | Detailed mean ms/group | Detailed/coarse difference |
| --- | ---: | ---: | ---: |
| Outer left | 0.578772 | 1.041648 | +79.98% |
| Outer right | 0.570393 | 1.074277 | +88.34% |
| Focus left | 0.424316 | 0.785400 | +85.10% |
| Focus right | 0.416565 | 0.780540 | +87.38% |

These are per-group measurements, not whole-frame or full-focus-pass CPU times.
Both phases retain the same hooks; coarse omits detailed nested timer/accounting
work. Phases sample different frames in a changing workload. The large difference
is evidence that detailed attribution needs lower-overhead follow-up. It is not
an exact independently measured profiler overhead to subtract from each category.
Thread-cycle counts rise similarly, consistent with additional CPU execution;
cycles are not converted to milliseconds or used to separate spin waits.

## Directional findings

Detailed focus groups averaged 317.94/318.39 Draw calls per left/right invocation,
with about 7.21 Map and Unmap calls per draw. Those are observed call counts,
not counts of common stereo objects, redundant uploads or calls removable by SPS.

| Detailed focus category | Left ms/group | Right ms/group |
| --- | ---: | ---: |
| Native preparation, inclusive of helper | 0.165625 | 0.165180 |
| Ordinary transform helper (nested in preparation) | 0.086390 | 0.086334 |
| Native upload wrapper, inclusive of its nested APIs | 0.242413 | 0.241053 |
| Draw API | 0.013590 | 0.013498 |

These perturbed values cannot support a net-saving forecast. Transform and
preparation must not be added; similarly, Map/Unmap overlap the upload wrapper.
Some observed Map/Unmap calls are outside that wrapper. The residual includes
unhooked sorting, state-setting and bridge/profiler work; it is not all shareable.

The small Draw API component gives little support for a large benefit from Draw
call reduction alone. Continue investigating preparation, constant uploads and
state changes together with paired submissions. View-dependent transforms and
per-eye membership/order still require separate handling. The ordinary transform
mode label here does not apply the numeric audit's full eligibility checks.

## Consequence for the next stage

The measurement coverage gate passes. Reliable detailed cost attribution and the
net-performance gate remain open. Do not use the old broad focus-render timing,
the 0.84 ms sum of coarse focus group means, or these category timings as promised
SPS savings. The 0.3 ms net target remains an acceptance target.

Use substantially fewer timed boundaries or an external CPU sampling trace with
fine hooks disabled to locate remaining preparation/submission/driver costs.
[Microsoft's CPU analysis guide](https://learn.microsoft.com/en-us/windows-hardware/test/wpt/cpu-analysis)
describes sampled execution and precise scheduling/wait analysis. WPR and its CPU
profile are available locally; WPA/xperf were not found on PATH. No ETW trace was
started or installed by this review.

The intended shared-preparation plus portable-instancing route remains a research
direction. Early right-focus inputs, source ownership, complete native side effects
and target/consumer compatibility remain unimplemented. No native work was removed
and no CPU/GPU frametime gain was established by this capture.

The analyzer now flags coarse/detailed differences of at least 10% separately
from coverage warnings. This is a diagnostic follow-up policy, not a confidence
threshold or a proof that smaller differences are free of instrumentation bias.
