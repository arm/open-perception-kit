---
slug: /performance-tracer
sidebar_position: 2
sidebar_label: Performance Tracer
---

# Performance Tracer

## Overview

The Performance Tracer is a simple and reliable timing framework for measuring performance in AMP inference pipelines. It provides accurate timing statistics with minimal overhead.

## Features

- **String-based Keys**: Flexible key naming for any metric
- **Thread-safe**: Mutex-protected for multi-threaded environments
- **RAII Scoped Timers**: Automatic timing with zero-overhead pattern
- **Statistical Analysis**: Calculates min, max, average, and percentiles (P50, P95, P99)
- **Cycle Management**: Groups measurements into cycles for periodic analysis
- **JSON Export**: Export statistics for external analysis
- **Monitor Class**: Real-time display utilities

## Performance Characteristics

- **Overhead per operation**: ~50-100 ns (negligible for ms-scale operations)
- **Memory**: Dynamic allocation using standard containers
- **Thread-safety**: Mutex-protected, safe for concurrent access
- **Best for**: Operations > 0.1ms (100 microseconds)

## Usage

### Basic Manual Timing

```cpp
#include "PerformanceTracer.h"

amp::PerformanceTracer tracer;

// Manual timing
tracer.start("preprocessing");
preprocess_image();
auto duration = tracer.end("preprocessing");
```

### Scoped Timing (Recommended)

```cpp
void process_frame() {
    amp::PerformanceTracer tracer;
    
    // Frame total timing
    amp::PerformanceTracer::ScopedTimer frame_timer(&tracer, "frame_total");
    
    {
        amp::PerformanceTracer::ScopedTimer prep_timer(&tracer, "preprocessing");
        preprocess_image();
    }
    
    {
        amp::PerformanceTracer::ScopedTimer infer_timer(&tracer, "inference");
        run_inference();
    }
    
    {
        amp::PerformanceTracer::ScopedTimer post_timer(&tracer, "postprocessing");
        postprocess_results();
    }
}
```

### Using Global Instance with Macros

```cpp
// Most convenient for application-wide tracing
void process_frame() {
    AMP_TRACE_SCOPE("frame_total");
    
    {
        AMP_TRACE_SCOPE("preprocessing");
        preprocess_image();
    }
    
    {
        AMP_TRACE_SCOPE("inference");
        run_inference();
    }
    
    {
        AMP_TRACE_SCOPE("postprocessing");
        postprocess_results();
    }
    
    // End cycle to calculate statistics
    AMP_TRACE_END_CYCLE();
}
```

### Getting Statistics

```cpp
// Get global tracer
amp::PerformanceTracer* tracer = amp::getGlobalTracer();

// Get stats for specific key
auto stats = tracer->getStats("inference");
std::cout << "Inference avg: " << stats.avg_ms() << "ms\n";
std::cout << "Inference P95: " << stats.p95_ms() << "ms\n";

// Get all stats
auto all_stats = tracer->getAllStats();
for (const auto& [key, stats] : all_stats) {
    if (stats.count > 0) {
        std::cout << key << ": " 
                  << stats.avg_ms() << "ms (P95: " 
                  << stats.p95_ms() << "ms)\n";
    }
}

// Print formatted summary
tracer->printSummary();

// Export to JSON
std::string json = tracer->toJSON();
```

## API Reference

### Core Methods

```cpp
class PerformanceTracer {
public:
    // Start timing operation
    void start(const std::string& key);
    
    // End timing and return duration
    std::chrono::nanoseconds end(const std::string& key);
    
    // End current cycle and calculate statistics
    void endCycle();
    
    // Get statistics for specific key
    TimingStats getStats(const std::string& key) const;
    
    // Get all statistics
    std::map<std::string, TimingStats> getAllStats() const;
    
    // Reset all data
    void reset();
    
    // Print summary table
    void printSummary() const;
    
    // Export to JSON
    std::string toJSON() const;
};
```

### RAII Scoped Timer

```cpp
class ScopedTimer {
public:
    ScopedTimer(PerformanceTracer* tracer, const std::string& key);
    ~ScopedTimer();  // Automatically calls end()
};
```

### Global Instance

```cpp
// Get global tracer instance (thread-safe singleton)
PerformanceTracer* getGlobalTracer();
```

### Convenience Macros

```cpp
// Start timing
AMP_TRACE_START(key)

// End timing
AMP_TRACE_END(key)

// Scoped timing (recommended)
AMP_TRACE_SCOPE(key)

// End cycle
AMP_TRACE_END_CYCLE()
```

## Statistics Structure

```cpp
struct TimingStats {
    std::string key;              // Metric name
    size_t count;                 // Number of measurements
    std::chrono::nanoseconds total;   // Sum of all durations
    std::chrono::nanoseconds min;     // Minimum duration
    std::chrono::nanoseconds max;     // Maximum duration
    std::chrono::nanoseconds avg;     // Average duration
    std::chrono::nanoseconds p50;     // 50th percentile (median)
    std::chrono::nanoseconds p95;     // 95th percentile
    std::chrono::nanoseconds p99;     // 99th percentile
    
    // Convenience methods
    double avg_ms() const;
    double p50_ms() const;
    double p95_ms() const;
    double p99_ms() const;
};
```

## Integration Example

### In GStreamer Element

```cpp
#include "PerformanceTracer.h"

static GstFlowReturn gst_ampinfer_transform_frame_ip(
    GstVideoFilter *vf, GstVideoFrame *frame) {
    
    static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
    amp::PerformanceTracer::ScopedTimer frame_timer(tracer, "frame_total");
    
    auto *self = GST_AMPINFER(vf);
    
    // Preprocessing
    {
        amp::PerformanceTracer::ScopedTimer prep_timer(tracer, "preprocessing");
        input = resize_normalize_rgb(frame, self->imgsz);
    }
    
    // Inference
    {
        amp::PerformanceTracer::ScopedTimer infer_timer(tracer, "inference");
        output = self->session->Run(input);
    }
    
    // Postprocessing
    {
        amp::PerformanceTracer::ScopedTimer post_timer(tracer, "postprocessing");
        results = parse_detections(output);
    }
    
    // End cycle every frame
    tracer->endCycle();
    
    // Print stats every 30 frames
    static int frame_count = 0;
    if (++frame_count % 30 == 0) {
        tracer->printSummary();
    }
    
    return GST_FLOW_OK;
}
```

## Output Format

### Summary Table

```
╔═══════════════════════════════════════════════════════════════════════════╗
║                  Performance Tracer Summary                               ║
╠═══════════════════════════════════════════════════════════════════════════╣
║ Key                  │ Count │  Avg(ms) │  P50(ms) │  P95(ms) │  P99(ms) ║
╠══════════════════════╪═══════╪══════════╪══════════╪══════════╪══════════╣
║ frame_total          │   300 │     7.83 │     7.81 │     8.09 │     8.92 ║
║ inference            │   300 │     7.41 │     7.40 │     7.64 │     8.46 ║
║ postprocessing       │   300 │     0.08 │     0.08 │     0.11 │     0.18 ║
║ preprocessing        │   300 │     0.26 │     0.26 │     0.32 │     0.41 ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

### JSON Export

```json
{
  "performance_tracer": [
    {
      "key": "frame_total",
      "count": 300,
      "avg_ms": 7.830,
      "p50_ms": 7.810,
      "p95_ms": 8.090,
      "p99_ms": 8.920,
      "min_ms": 7.450,
      "max_ms": 10.120
    },
    {
      "key": "inference",
      "count": 300,
      "avg_ms": 7.410,
      "p50_ms": 7.400,
      "p95_ms": 7.640,
      "p99_ms": 8.460,
      "min_ms": 7.230,
      "max_ms": 9.410
    }
  ]
}
```

## Performance Considerations

### When to Use

- ✅ Operations taking > 0.1ms (100 microseconds)
- ✅ Frame-level timing (preprocessing, inference, postprocessing)
- ✅ Pipeline performance monitoring
- ✅ Development and debugging
- ✅ Production monitoring with acceptable overhead

### When Not to Use

- ❌ Tight loops (< 10 microseconds per iteration)
- ❌ Low-level memory operations
- ❌ Interrupt handlers or real-time critical code
- ❌ Operations where string allocation matters

### Optimization Tips

1. **Use Scoped Timers**: RAII pattern prevents forgetting to call `end()`
2. **Reuse Keys**: Same string keys benefit from map caching
3. **Batch Cycles**: Call `endCycle()` periodically, not every operation
4. **Limit Key Count**: Each key adds to map overhead
5. **Use Global Instance**: Avoid creating multiple tracer instances

## Best Practices

### Naming Convention

```cpp
// Good: Clear, descriptive names
AMP_TRACE_SCOPE("model_inference");
AMP_TRACE_SCOPE("image_preprocessing");
AMP_TRACE_SCOPE("nms_postprocessing");

// Avoid: Generic names
AMP_TRACE_SCOPE("step1");
AMP_TRACE_SCOPE("process");
```

### Scope Management

```cpp
// Good: Clear scopes
void process_frame() {
    AMP_TRACE_SCOPE("frame_total");
    
    { // Explicit scope
        AMP_TRACE_SCOPE("preprocessing");
        preprocess();
    }
    
    { // Explicit scope
        AMP_TRACE_SCOPE("inference");
        inference();
    }
}

// Avoid: Overlapping scopes
void bad_example() {
    AMP_TRACE_START("operation1");
    AMP_TRACE_START("operation2");  // operation1 still active!
    AMP_TRACE_END("operation2");
    AMP_TRACE_END("operation1");
}
```

### Cycle Management

```cpp
// Good: End cycle at frame boundary
void process_frames() {
    for (auto& frame : frames) {
        process_frame(frame);
        amp::getGlobalTracer()->endCycle();  // Once per frame
    }
}

// Avoid: Too frequent cycles
void bad_example() {
    amp::getGlobalTracer()->endCycle();  // Don't call in tight loop
    for (int i = 0; i < 1000; i++) {
        process();
        amp::getGlobalTracer()->endCycle();  // Too much overhead!
    }
}
```

## Thread Safety

The Performance Tracer is thread-safe with the following guarantees:

- ✅ Multiple threads can call `start()` and `end()` concurrently
- ✅ Statistics calculation is atomic
- ✅ Read operations are safe during writes
- ⚠️ Same key from multiple threads will aggregate results

### Multi-threaded Example

```cpp
// Safe: Each thread traces independently
void worker_thread() {
    amp::PerformanceTracer* tracer = amp::getGlobalTracer();
    
    while (running) {
        AMP_TRACE_SCOPE("worker_iteration");
        process_task();
    }
}

int main() {
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; i++) {
        workers.emplace_back(worker_thread);
    }
    
    // All threads contribute to same "worker_iteration" statistics
    for (auto& t : workers) t.join();
    
    amp::getGlobalTracer()->printSummary();
}
```

## Related Documentation

- [AMP Performance Overlay Element](AMPPERFORMANCE_ELEMENT.md) - Visual overlay of tracer metrics
- [AMP Inference Element Source](https://github.com/Arm-Debug/amp-dev-forge/tree/main/development/elements/ampinfer) - Example integration

## License

Part of the AMP elements suite - LGPL (same as GStreamer)
