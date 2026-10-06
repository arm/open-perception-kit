---
sidebar_position: 3
sidebar_label: opkperformance
---
<!--
SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
SPDX-License-Identifier: Apache-2.0

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    https://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->


# C++ Performance Metrics

OPK uses `PerformanceMetrics` as its single C++ timing recorder. Ops record
hierarchical scopes once; runtime APIs, CSV export, and `opkperformance` read
the same process-wide data without resetting it.

## Data Flow

```text
preprocess / inference / postprocess / OpChain
                     |
               OPK_PERF_SCOPE
                     |
                     v
             PerformanceMetrics
             (process-wide recorder)
              /         \
             /           \
 aggregate snapshots   span history
          |              + CSV
          |
 runtime::PerformanceMetrics
 + pipeline-exec
          |
 calculateScopeIntervalMetrics
          |
   opkperformance
          |
  PerformanceOverlayT
          |
    opkosd / WebUI
```

The previous duplicate recorder and its paired instrumentation have been
removed. `opkperformance` remains a packaged GStreamer element; only its
internal timing source changed.

## Components And Capabilities

| Component | Capabilities | Consumers |
| --- | --- | --- |
| [`PerformanceMetrics`](../../../development/common/perf/PerformanceMetrics.h) | Hierarchical RAII scopes; bounded per-thread cumulative aggregates; concurrent snapshots; optional completed-span history; CSV export; overflow, drop, and name-truncation diagnostics. | The public [`runtime::PerformanceMetrics`](../../../development/runtime/PerformanceMetrics.h) facade, `pipeline-exec`, and `opkperformance`. |
| `calculateScopeIntervalMetrics(intervalStartSnapshot, intervalEndSnapshot)` | Matches metrics by their complete root-to-scope name hierarchy and subtracts cumulative count and total duration. Returns only scopes completed between chronological snapshots, including completed-scope count, total duration, and average duration. | `opkperformance`. |
| [`opkperformance`](../../../development/elements/opkperformance/opkperformance.cpp) | Calculates FPS, formats interval averages, and appends `PerformanceOverlayT` to `FrameResults`. | `opkosd` and the WebUI render the payload. |

The hierarchy is the dynamic nesting of named `OPK_PERF_SCOPE` instances. It
distinguishes the same scope name beneath different parents; it is not a
filesystem path or a reconstructed static call graph.

Aggregate snapshots contain summaries rather than individual timing events and
do not copy span history. For example, if one scope advances from 100 calls and
500 ms to 120 calls and 620 ms, its interval is 20 calls and 120 ms, averaging
6 ms. Comparing snapshots does not reset or otherwise mutate the global
recorder, so independent snapshot and CSV clients can run at the same time.

Historical spans are captured only when a client explicitly enables history.
They support CSV export but are not enabled by `opkperformance`, keeping
continuous reporting bounded by the aggregate recorder capacities.

## `opkperformance` Behavior

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
- The interval baseline is element-local. Enabling or disabling `opkperformance` does
  not enable, disable, or reset process-wide recording.

The interval is process-wide, matching the recorder's ownership model.

## Controls

- `enabled`: enable or disable publishing.
- `update-interval`: refresh cadence in frames.
- `show-all-metrics`: publish all completed interval scopes instead of the
  compact stage view.
- The upstream `opkperformance` event can toggle `enabled` at runtime.
- The element appends `PerformanceOverlayT`; `opkosd` or the WebUI renders it.
