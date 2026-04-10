---
slug: /
sidebar_position: 1
sidebar_label: Overview
---

# AMP Elements Documentation

Welcome to the AMP (Accelerated Media Processing) elements documentation. This suite provides high-performance GStreamer elements for real-time inference and performance monitoring.

## 📚 Documentation Index

### Core Components

- **[Performance Tracer](PERFORMANCE_TRACER.md)** - Timing framework for measuring pipeline performance
  - API reference and usage examples
  - Thread-safe timing with statistical analysis
  - Integration guide for GStreamer elements

### GStreamer Elements

- **[AMP Performance Overlay](AMPPERFORMANCE_ELEMENT.md)** - Visual overlay element
  - Displays real-time performance metrics on video streams
  - Configurable appearance and positioning
  - Cairo-based rendering

### Coming Soon

- AMP Inference Element Guide
- Pipeline Integration Examples
- Performance Optimization Guide

## Quick Start

### Performance Monitoring in Your Element

```cpp
#include "PerformanceTracer.h"

static GstFlowReturn transform_frame(GstVideoFilter *filter, GstVideoFrame *frame) {
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
    amp::PerformanceTracer::ScopedTimer timer(tracer, "frame_total");

    // Your processing code here

    tracer->endCycle();
    return GST_FLOW_OK;
}
```

### Visual Performance Overlay

```bash
gst-launch-1.0 \
  videotestsrc ! video/x-raw,format=RGB ! \
  ampinfer model-path=model.onnx ! \
  videoconvert ! video/x-raw,format=RGBA ! \
  ampperformance x-offset=20 y-offset=20 font-size=14 ! \
  videoconvert ! autovideosink
```

## Architecture Overview

```
┌──────────────────────────────────────────────────┐
│          Performance Tracer (Core)               │
│  - Thread-safe timing framework                  │
│  - Statistical analysis (min/max/avg/percentiles)│
│  - Cycle-based measurement grouping              │
└──────────────┬───────────────────────────────────┘
               │
               ├─────────────────┬─────────────────┐
               │                 │                 │
        ┌──────▼──────┐   ┌─────▼─────┐   ┌──────▼──────┐
        │  ampinfer   │   │ ampperform│   │ Your Custom │
        │  (timing)   │   │  (overlay) │   │   Element   │
        └─────────────┘   └────────────┘   └─────────────┘
```

## Key Features

### Performance Tracer

- ✅ Simple string-based API
- ✅ RAII scoped timers
- ✅ Thread-safe concurrent access
- ✅ Comprehensive statistics (P50, P95, P99)
- ✅ JSON export capability
- ✅ Minimal overhead (~50-100ns per operation)

### AMP Performance Element

- ✅ Real-time video overlay
- ✅ Customizable appearance
- ✅ Low rendering overhead
- ✅ Configurable update intervals
- ✅ Cairo-based high-quality rendering

## Performance Characteristics

| Component | Overhead | Best For |
|-----------|----------|----------|
| Performance Tracer | ~50-100ns per operation | Operations > 0.1ms |
| AMP Performance Overlay | ~0.1ms per frame (at 5 frame interval) | Real-time monitoring |

## Common Use Cases

### 1. Development & Debugging
Monitor performance metrics during development to identify bottlenecks.

### 2. Production Monitoring
Track inference pipeline performance in deployed systems.

### 3. Performance Regression Testing
Automated testing with JSON export for CI/CD pipelines.

### 4. Real-time Visualization
Live performance overlay for demos and presentations.

## Building the Elements

```bash
cd /work/development
meson setup builddir --prefix=/usr
meson compile -C builddir
sudo meson install -C builddir
```

## Contributing

When adding new elements or modifying existing ones:

1. Use Performance Tracer for timing measurements
2. Follow the scoped timer pattern (RAII)
3. Call `endCycle()` at appropriate boundaries
4. Update documentation with usage examples

## Support

- **Issues**: Report bugs or request features via GitHub issues
- **Documentation**: All docs are in Markdown format in `/work/docs/`
- **Examples**: See individual element documentation for pipeline examples

## License

LGPL - Same as GStreamer

---

**Last Updated**: December 2025
