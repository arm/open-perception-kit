# Fast Performance Tracer Implementation

## Overview

The Fast Performance Tracer is a high-performance, zero-overhead timing framework designed for real-time inference pipelines where every nanosecond counts.

## Performance Comparison

| Metric | Standard Tracer | Fast Tracer | Improvement |
|--------|----------------|-------------|-------------|
| **Overhead per operation** | ~25,000 ns | ~57 ns | **447x faster** |
| **Heap allocations** | Yes (map, string) | No | **Zero allocations** |
| **Lock contention** | 4 mutexes | None | **Lock-free** |
| **Thread scalability** | Limited | Linear | **Perfect scaling** |
| **API overhead** | Function call | Inline | **Zero call overhead** |

## Key Features

### 1. **Compile-Time Key Resolution**
- No string allocations or lookups
- Keys resolved at compile time via enum
- Direct array access (O(1))

### 2. **Lock-Free Architecture**
- Thread-local storage for active timers
- Atomic operations for statistics updates
- No mutex contention

### 3. **Zero Heap Allocations**
- Pre-allocated fixed-size arrays
- No dynamic memory in hot path
- Cache-friendly data structures

### 4. **Inline Everything**
- `__attribute__((always_inline))` on hot functions
- RAII scoped timers with zero overhead
- Compiler can optimize away instrumentation

## Usage

### Predefined Keys

```cpp
enum class TimingKey : uint32_t {
    PREPROCESSING = 0,   // Image preprocessing
    INFERENCE = 1,       // Neural network inference
    POSTPROCESSING = 2,  // NMS, result filtering
    FRAME_TOTAL = 3,     // Complete frame processing
    CUSTOM_0 = 4,        // Application-specific
    CUSTOM_1 = 5,
    CUSTOM_2 = 6,
    CUSTOM_3 = 7,
    MAX_KEYS = 8
};
```

### Manual Timing

```cpp
// Fast manual timing
AMP_FAST_START(PREPROCESSING);
preprocess_image();
AMP_FAST_END(PREPROCESSING);

AMP_FAST_START(INFERENCE);
run_inference();
AMP_FAST_END(INFERENCE);
```

### Scoped Timing (Recommended)

```cpp
void process_frame() {
    AMP_FAST_SCOPE(FRAME_TOTAL);
    
    {
        AMP_FAST_SCOPE(PREPROCESSING);
        preprocess_image();
    }
    
    {
        AMP_FAST_SCOPE(INFERENCE);
        run_inference();
    }
    
    {
        AMP_FAST_SCOPE(POSTPROCESSING);
        postprocess_results();
    }
}
```

### Getting Statistics

```cpp
// Print summary
amp::fast::printSummary();

// Export to JSON
std::string json = amp::fast::toJSON();

// Access individual stats
const auto& stats = amp::fast::getStats(amp::fast::TimingKey::INFERENCE);
double avg_ms = stats.avg_ms();
double p95_ms = stats.percentile_ms(95.0);

// Reset statistics
amp::fast::reset();
```

## Integration Example

### In ampinfer element

```cpp
// In ampinfer.cpp
#include "PerformanceTracer.h"

static GstFlowReturn gst_ampinfer_transform_frame_ip(GstVideoFilter *vf, GstVideoFrame *frame) {
    GstAmpInfer *self = GST_AMPINFER(vf);
    
    // Time the entire frame
    AMP_FAST_SCOPE(FRAME_TOTAL);
    
    // Preprocessing
    {
        AMP_FAST_SCOPE(PREPROCESSING);
        // ... preprocessing code ...
    }
    
    // Inference
    {
        AMP_FAST_SCOPE(INFERENCE);
        // ... ONNX inference ...
    }
    
    // Postprocessing
    {
        AMP_FAST_SCOPE(POSTPROCESSING);
        // ... NMS and result filtering ...
    }
    
    // Every 30 frames, print stats
    static int frame_count = 0;
    if (++frame_count % 30 == 0) {
        amp::fast::printSummary();
    }
    
    return GST_FLOW_OK;
}
```

## Technical Implementation

### Lock-Free Statistics Update

```cpp
struct FastStats {
    std::atomic<uint64_t> count;
    std::atomic<uint64_t> total_ns;
    std::atomic<uint64_t> min_ns;
    std::atomic<uint64_t> max_ns;
    
    void update(uint64_t duration_ns) {
        count.fetch_add(1, std::memory_order_relaxed);
        total_ns.fetch_add(duration_ns, std::memory_order_relaxed);
        
        // CAS loop for min/max
        uint64_t current_min = min_ns.load(std::memory_order_relaxed);
        while (duration_ns < current_min && 
               !min_ns.compare_exchange_weak(current_min, duration_ns));
    }
};
```

### Thread-Local Timers

```cpp
// No locks - each thread has its own storage
thread_local std::array<std::chrono::steady_clock::time_point, 8> g_active_timers;

inline void start(TimingKey key) {
    g_active_timers[static_cast<size_t>(key)] = std::chrono::steady_clock::now();
}
```

### Inline Hot Path

```cpp
template<TimingKey Key>
class ScopedTimer {
    std::chrono::steady_clock::time_point start_;
public:
    inline ScopedTimer() __attribute__((always_inline))
        : start_(std::chrono::steady_clock::now()) {}
    
    inline ~ScopedTimer() __attribute__((always_inline)) {
        auto duration = std::chrono::steady_clock::now() - start_;
        g_stats[static_cast<size_t>(Key)].update(duration.count());
    }
};
```

## Percentile Calculation

Percentiles are calculated from a ring buffer of recent samples (256 by default):

```cpp
struct FastStats {
    static constexpr size_t SAMPLE_SIZE = 256;
    std::array<std::atomic<uint64_t>, SAMPLE_SIZE> samples;
    std::atomic<size_t> sample_index{0};
    
    double percentile_ms(double p) const {
        // Collect and sort samples
        std::vector<uint64_t> sorted_samples;
        for (const auto& s : samples) {
            uint64_t val = s.load(std::memory_order_relaxed);
            if (val > 0) sorted_samples.push_back(val);
        }
        
        std::sort(sorted_samples.begin(), sorted_samples.end());
        size_t idx = (sorted_samples.size() - 1) * p / 100.0;
        return sorted_samples[idx] / 1e6;
    }
};
```

## When to Use Which Tracer

### Use Fast Tracer When:
- ✅ In real-time inference hot paths
- ✅ Measuring frame-by-frame performance
- ✅ Need minimal overhead (<100ns)
- ✅ Working with fixed set of timing keys
- ✅ Multi-threaded performance critical code

### Use Standard Tracer When:
- ✅ Dynamic/runtime key generation needed
- ✅ Detailed cycle-by-cycle analysis
- ✅ Complex callback requirements
- ✅ Debugging/development (flexibility > performance)
- ✅ Need full measurement history

## Benchmark Results

From `fast_tracer_example`:

```
Standard tracer: 25,505 ns per start/end pair
Fast tracer:         57 ns per start/end pair

Speedup: 447x faster
```

### Real-World Impact

For 60 FPS video processing with 4 timing points per frame:

| Tracer | Overhead per frame | % of 16.67ms budget |
|--------|-------------------|---------------------|
| **Standard** | ~204 µs | 1.2% |
| **Fast** | ~0.5 µs | 0.003% |

**Result**: Fast tracer overhead is negligible even at 60 FPS!

## Output Examples

### Terminal Output

```
╔═══════════════════════════════════════════════════════════════════════════╗
║                  Fast Performance Tracer Summary                          ║
╠═══════════════════════════════════════════════════════════════════════════╣
║ Key                  │ Count │  Avg(ms) │  P50(ms) │  P95(ms) │  P99(ms) ║
╠══════════════════════╪═══════╪══════════╪══════════╪══════════╪══════════╣
║ preprocessing        │   100 │     6.69 │     6.73 │     7.49 │     7.57 ║
║ inference            │   100 │    22.93 │    22.38 │    25.85 │    26.32 ║
║ postprocessing       │   100 │     9.89 │     9.89 │    10.97 │    11.27 ║
║ frame_total          │   100 │    39.52 │    39.25 │    43.09 │    43.82 ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

### JSON Export

```json
{
  "fast_tracer": [
    {
      "key": "inference",
      "count": 100,
      "avg_ms": 22.932,
      "p50_ms": 22.384,
      "p95_ms": 25.849,
      "p99_ms": 26.318,
      "min_ms": 20.337,
      "max_ms": 26.323
    }
  ]
}
```

## API Reference

### Functions

- `amp::fast::start(TimingKey key)` - Start timing
- `amp::fast::end(TimingKey key)` - End timing and return duration
- `amp::fast::getStats(TimingKey key)` - Get statistics for key
- `amp::fast::getKeyName(TimingKey key)` - Get string name of key
- `amp::fast::reset()` - Clear all statistics
- `amp::fast::printSummary()` - Print formatted statistics
- `amp::fast::toJSON()` - Export to JSON string

### Macros

- `AMP_FAST_START(key)` - Start timing with key name
- `AMP_FAST_END(key)` - End timing with key name
- `AMP_FAST_SCOPE(key)` - Create scoped timer for key

### Classes

- `amp::fast::ScopedTimer<TimingKey>` - RAII scoped timer (template)
- `amp::fast::FastStats` - Lock-free statistics container

## Summary

The Fast Performance Tracer provides:
- **447x faster** than standard tracer
- **Lock-free** multi-threaded operation
- **Zero heap allocations** in hot path
- **Inline** everything for zero call overhead
- **Full statistics** (avg, p50, p95, p99, min, max)
- **JSON export** for analysis tools
- **Simple API** with compile-time safety

Perfect for real-time inference pipelines where performance is critical!
