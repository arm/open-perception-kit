---
sidebar_position: 3
sidebar_label: pekperformance
---

# C++ Performance Metrics

OPK uses `PerformanceMetrics` as its single C++ timing recorder. Ops record
hierarchical scopes once; runtime APIs, CSV export, `pekperformance`, and the
YOLO benchmark read the same process-wide data without resetting it.

## Data Flow

```text
preprocess / inference / postprocess / OpChain
                     |
               PEK_PERF_SCOPE
                     |
                     v
             PerformanceMetrics
             (process-wide recorder)
              /         |          \
             /          |           \
 aggregate snapshots  span history  aggregate snapshots
          |            + CSV              |
          |                    calculateScopeIntervalMetrics
 runtime::PerformanceMetrics               /          \
 + pipeline-exec                          /            \
                                  pekperformance    yolo-benchmark
                                        |           per-image stages
                              PerformanceOverlayT
                                        |
                                  pekosd / WebUI
```

The previous duplicate recorder and its paired instrumentation have been
removed. `pekperformance` remains a packaged GStreamer element; only its
internal timing source changed.

## Components And Capabilities

| Component | Capabilities | Consumers |
| --- | --- | --- |
| [`PerformanceMetrics`](../../../development/common/perf/PerformanceMetrics.h) | Hierarchical RAII scopes; bounded per-thread cumulative aggregates; concurrent snapshots; optional completed-span history; CSV export; overflow, drop, and name-truncation diagnostics. | The public [`runtime::PerformanceMetrics`](../../../development/runtime/PerformanceMetrics.h) facade, `pipeline-exec`, `pekperformance`, and the YOLO benchmark. |
| `calculateScopeIntervalMetrics(intervalStartSnapshot, intervalEndSnapshot)` | Matches metrics by their complete root-to-scope name hierarchy and subtracts cumulative count and total duration. Returns only scopes completed between chronological snapshots, including completed-scope count, total duration, and average duration. | `pekperformance` and the YOLO benchmark. |
| [`pekperformance`](../../../development/elements/pekperformance/pekperformance.cpp) | Calculates FPS, formats interval averages, and appends `PerformanceOverlayT` to `FrameResults`. | `pekosd` and the WebUI render the payload. |

The hierarchy is the dynamic nesting of named `PEK_PERF_SCOPE` instances. It
distinguishes the same scope name beneath different parents; it is not a
filesystem path or a reconstructed static call graph.

Aggregate snapshots contain summaries rather than individual timing events and
do not copy span history. For example, if one scope advances from 100 calls and
500 ms to 120 calls and 620 ms, its interval is 20 calls and 120 ms, averaging
6 ms. Comparing snapshots does not reset or otherwise mutate the global
recorder, so independent snapshot and CSV clients can run at the same time.

Historical spans are captured only when a client explicitly enables history.
They support CSV export but are not enabled by `pekperformance` or the benchmark,
keeping continuous reporting bounded by the aggregate recorder capacities.

## `pekperformance` Behavior

On start and after a disabled-to-enabled transition, the element records a new
local interval baseline snapshot. At each configured refresh it compares the
current snapshot with that baseline and then advances the baseline. Inactive
scopes therefore disappear instead of leaving stale rows.

- The default compact view combines interval count and total duration into
  `PreProc`, `Inference`, and `PostProc` averages.
- `show-all-metrics=true` displays each completed interval scope separately.
- Percentiles are not shown because cumulative aggregates cannot reconstruct an
  interval distribution without retaining every span.
- If no scope completes during an interval, the overlay still contains its
  heading and FPS line.
- The interval baseline is element-local. Enabling or disabling `pekperformance` does
  not enable, disable, or reset process-wide recording.

The interval is process-wide, matching the recorder's ownership model. The
standalone YOLO benchmark measures one synchronous OpChain and derives
preprocess, inference, and postprocess totals from snapshots around each image;
unrelated concurrent OpChain execution in that process is unsupported.

## Controls

- `enabled`: enable or disable publishing.
- `update-interval`: refresh cadence in frames.
- `show-all-metrics`: publish all completed interval scopes instead of the
  compact stage view.
- The upstream `pekperformance` event can toggle `enabled` at runtime.
- The element appends `PerformanceOverlayT`; `pekosd` or the WebUI renders it.
