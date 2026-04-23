---
sidebar_position: 3
sidebar_label: ampperformance
---

# Performance Tracing and Overlay
## PerformanceTracer, PerformanceMonitor, and `ampperformance`

This module provides a thread-safe timing infrastructure (`PerformanceTracer`)
and a GStreamer element (`ampperformance`) that publishes aggregated metrics
into `Perception::perfdata` for downstream visualization (e.g., via `amposd`).

The design separates:

- measurement collection (library / runtime code)
- statistics aggregation (percentiles and summaries)
- presentation (overlay generation via pipeline elements)

---

# PerformanceTracer
## Thread-Safe Keyed Timing Store

`amp::PerformanceTracer` is a keyed timer registry that records duration samples
as `TimingMeasurement` entries.

### Measurement API

- `start(key)` stores the start timestamp for `key`.
- `end(key)` ends the timer for `key`, records a `TimingMeasurement`, and returns the duration.
- `ScopedTimer` provides RAII timing via construction/destruction.
- Convenience macros provide a global tracer API:

`AMP_TRACE_START(key)`  
`AMP_TRACE_END(key)`  
`AMP_TRACE_SCOPE(key)`  
`AMP_TRACE_END_CYCLE()`

### Concurrency Model

The tracer is designed for multi-threaded pipelines.

It uses separate mutex-protected stores for:

- active timers (start called, end not called)
- current-cycle measurements
- measurement history (per key)
- cached statistics (per key)
- end-cycle callbacks

Timers are keyed by string; multiple overlapping timers for the same key are not supported
(the latest `start(key)` overwrites the active entry).

### History Retention

Measurements are retained per key up to `max_measurements_per_key_`
(default 1000). Older samples are dropped when the limit is exceeded.

### Cycle Semantics

`endCycle()` marks the end of a measurement cycle.

- increments the cycle counter
- snapshots and clears current-cycle measurements
- optionally recalculates statistics
- triggers registered cycle-end callbacks
- performs cleanup (currently managed by history retention limits)

Cycle boundaries are defined by the caller.
In media pipelines, a cycle typically corresponds to a frame.

---

# Statistics Model

`TimingStats` aggregates samples per key across retained history.

Computed fields:

- count
- total, min, max, avg
- percentiles: p50, p95, p99

Percentiles are computed by sorting durations and selecting indices derived from size.
Statistics are stored in `stats_cache_` and returned via `getStats(key)` and `getAllStats()`.

`toJSON()` exports aggregated stats for external tooling.

`printSummary()` prints a formatted summary table to stdout.

---

# PerformanceMonitor
## Console-Oriented Reporting

`amp::PerformanceMonitor` is a helper around `PerformanceTracer` for printing
or continuously refreshing statistics to stdout.

Modes:

- COMPACT: one-line per key summaries
- DETAILED: full table view with percentiles
- LIVE_UPDATE: refresh loop (blocking) using ANSI clear-screen sequences

This is intended for interactive debugging outside GStreamer overlays.

---

# Global Tracer Instance

`getGlobalTracer()` provides a lazy-initialized singleton.

This is used as a convenience bridge between pipeline elements and non-GStreamer code
so all components report into the same timing store.

---

# `ampperformance` Element
## Publishing Metrics Into Perception

`ampperformance` is a `GstVideoFilter` element that injects aggregated performance
metrics into `Perception::perfdata`.

It does not draw overlays itself.
Instead, it populates the Perception payload, enabling visualization via `amposd`
or consumption by other downstream components.

### Execution Path

In `transform_frame_ip`:

1. Update FPS estimation based on inter-frame timing.
2. Refresh cached metric lines every `update-interval` frames or when marked dirty.
3. Try to mutate `PerceptionMeta` on the current buffer.
4. If `PerceptionMeta` is present, write the cached formatted lines into `perception.perfdata`.

`get_performance_data()` internally calls `getGlobalTracer()->endCycle()`.
This establishes the cycle boundary from within the element.

### Metric Presentation Modes

- `show-all-metrics=false` shows a fixed set of keys (preprocess/inference/postprocess).
- `show-all-metrics=true` exports all available tracer keys.

When showing all metrics, keys are grouped by a model prefix
(extracted as the substring before the first underscore) and ordered by suffix:
`_preprocess`, `_inference`, `_postprocess`.

The element also computes:

- Pipeline FPS (EMA-smoothed)
- AI utilization estimate based on summed p50 timings versus frame time

### Properties

- `enabled` (bool): enable/disable publishing.
- `x-offset`, `y-offset`, `font-size`, `bg-color`, `text-color`, `alpha`:
  present for overlay formatting; actual rendering is performed by `amposd`.
- `update-interval` (uint): refresh cadence in frames.
- `show-all-metrics` (bool): export all tracer keys vs predefined set.

### Event Control

The element supports control via custom upstream events:

- Event name: `"ampperformance"`
- Fields: `"enabled"` boolean

This allows runtime toggling without property reconfiguration.

---

# Integration Notes

- `ampperformance` writes metrics when `PerceptionMeta` is present on the buffer.
- If no `PerceptionMeta` is attached, the element returns successfully without writing anything.
- Downstream elements (e.g., `amposd`) can render `Perception::perfdata` as text overlay.
- For correctness, the cycle boundary should match the intended unit of work.
  When `ampperformance` drives `endCycle()`, it effectively defines the cycle as “per frame”.
