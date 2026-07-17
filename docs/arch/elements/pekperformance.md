---
sidebar_position: 3
sidebar_label: pekperformance
---

# Performance Tracing And Overlay

OPK has a shared timing infrastructure (`PerformanceTracer`) and a GStreamer
element (`pekperformance`) that publishes aggregated metrics into
`Perception::perfdata` for downstream display or inspection.

## PerformanceTracer

`pek::PerformanceTracer` records duration samples keyed by string. It supports
manual `start()`/`end()` calls, scoped timing, and convenience tracing macros.
Measurements are retained per key and can be summarized as count, min, max,
average, and percentiles.

The tracer uses mutex-protected stores for active timers, current-cycle samples,
history, cached statistics, and cycle-end callbacks. Multiple overlapping timers
for the same key are not supported; the latest `start(key)` owns that key.

`endCycle()` defines a measurement cycle. In media pipelines, that usually maps
to one frame.

## PerformanceMonitor

`PerformanceMonitor` is a console-oriented helper around `PerformanceTracer`. It
can print compact summaries, detailed tables, or a live-refreshing terminal view.
It is intended for debugging outside overlay rendering.

## pekperformance Element

`pekperformance` is a `GstVideoFilter` that writes formatted performance lines
into `Perception::perfdata`. It does not draw overlays; `pekosd` renders the text
if present.

During `transform_frame_ip`, the element updates FPS estimation, refreshes cached
metric lines on the configured interval, mutates `PerceptionMeta` when present,
and writes the cached lines into `perception.perfdata`.

## Properties And Control

- `enabled`: enable or disable publishing.
- `update-interval`: refresh cadence in frames.
- `show-all-metrics`: export all tracer keys instead of the predefined set.
- Overlay formatting properties are preserved for downstream rendering.

The element also accepts custom upstream `pekperformance` events with an
`enabled` boolean for runtime toggling.

## Notes

When `pekperformance` calls `endCycle()`, it effectively defines the performance
cycle as per-frame. Pipelines that need a different unit of work should make that
boundary explicit.
