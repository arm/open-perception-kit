# AMP Performance Overlay Element

## Overview

The `ampperformance` GStreamer element overlays real-time performance metrics from the Performance Tracer directly onto video streams. It provides visual feedback of inference pipeline performance including preprocessing, inference, postprocessing, and overall frame timing.

## Features

- **Real-time Overlay**: Draws performance statistics directly on video frames
- **Cairo Graphics**: Uses Cairo for high-quality rendering
- **Customizable Appearance**: Configurable position, colors, font size, and transparency
- **Low Overhead**: Updates at configurable intervals to minimize performance impact
- **Performance Tracer Integration**: Reads statistics from global amp::PerformanceTracer instance

## Installation

The element is built as part of the AMP elements suite:

```bash
cd /work/scripts
bash build-elements.sh release
```

## Pipeline Integration

### Basic Usage

```bash
export GST_PLUGIN_PATH=/work/development/build/elements/ampinfer:/work/development/build/elements/ampperformance

gst-launch-1.0 \
  videotestsrc ! \
  video/x-raw,format=RGB,width=1280,height=720 ! \
  ampinfer model-path=/path/to/model.onnx imgsz=160 ! \
  videoconvert ! video/x-raw,format=BGRA ! \
  ampperformance ! \
  videoconvert ! autovideosink
```

### With File Output

```bash
gst-launch-1.0 \
  filesrc location=input.mp4 ! qtdemux ! h264parse ! avdec_h264 ! \
  videoconvert ! video/x-raw,format=RGB ! \
  ampinfer model-path=model.onnx imgsz=320 ! \
  videoconvert ! video/x-raw,format=BGRA ! \
  ampperformance x-offset=20 y-offset=20 font-size=16 ! \
  videoconvert ! x264enc ! mp4mux ! \
  filesink location=output.mp4
```

## Properties

| Property | Type | Range | Default | Description |
|----------|------|-------|---------|-------------|
| `x-offset` | int | 0-∞ | 10 | Horizontal position in pixels from left edge |
| `y-offset` | int | 0-∞ | 10 | Vertical position in pixels from top edge |
| `font-size` | double | 6.0-72.0 | 12.0 | Font size in points |
| `bg-color` | string | hex | #000000 | Background color in hex format |
| `text-color` | string | hex | #00FF00 | Text color in hex format |
| `alpha` | double | 0.0-1.0 | 0.85 | Background transparency (0=transparent, 1=opaque) |
| `update-interval` | uint | 1-120 | 5 | Update overlay every N frames |

### Property Examples

```bash
# Large green text on black background
ampperformance x-offset=30 y-offset=30 font-size=20 text-color="#00FF00" bg-color="#000000" alpha=0.9

# Small white text on dark gray background  
ampperformance x-offset=10 y-offset=10 font-size=12 text-color="#FFFFFF" bg-color="#333333" alpha=0.7

# Bottom-right corner placement
ampperformance x-offset=1100 y-offset=600 font-size=14

# Update every frame for maximum responsiveness
ampperformance update-interval=1

# Update every 10 frames for minimal overhead
ampperformance update-interval=10
```

## Display Format

The overlay shows the following metrics:

```
═══ Performance Metrics ═══
PreProc: 0.25ms (p95:0.31ms)
Inference: 7.44ms (p95:7.91ms)
PostProc: 0.07ms (p95:0.09ms)
Frame: 7.78ms (p95:8.30ms)
FPS: 128.5
═══════════════════════════
```

### Metrics Explained

- **PreProc**: Image preprocessing time (resize, normalize, format conversion)
- **Inference**: Runtime model execution time
- **PostProc**: Output processing time (detection parsing, NMS, drawing)
- **Frame**: Total frame processing time (sum of all stages)
- **FPS**: Calculated frames per second based on average frame time
- **p95**: 95th percentile - shows worst-case performance

## Supported Video Formats

Input formats (requires videoconvert before ampperformance):
- BGRA (preferred)
- RGBA
- RGB
- BGR

## Performance Considerations

### Update Interval

The `update-interval` property controls how often the overlay is redrawn:

- **1**: Maximum responsiveness, updates every frame (~0.1ms overhead per frame with Cairo)
- **5**: Good balance (default)
- **10-30**: Minimal overhead for production use

### Performance Tracer Integration

The element reads directly from the global Performance Tracer statistics:
- Thread-safe mutex-protected reads
- ~50-100ns overhead per timing operation
- Access via `amp::getGlobalTracer()` singleton

## Code Integration

To enable performance tracking in your element, use the Performance Tracer macros:

```cpp
#include "PerformanceTracer.h"

static GstFlowReturn my_transform_frame_ip(GstVideoFilter *filter, GstVideoFrame *frame) {
  static amp::PerformanceTracer *tracer = amp::getGlobalTracer();
  amp::PerformanceTracer::ScopedTimer frame_timer(tracer, "frame_total");
  
  // Preprocessing
  {
    amp::PerformanceTracer::ScopedTimer prep_timer(tracer, "preprocessing");
    // ... preprocessing code ...
  }
  
  // Inference
  {
    amp::PerformanceTracer::ScopedTimer infer_timer(tracer, "inference");
    // ... inference code ...
  }
  
  // Postprocessing
  {
    amp::PerformanceTracer::ScopedTimer post_timer(tracer, "postprocessing");
    // ... postprocessing code ...
  }
  
  // End cycle to calculate statistics
  tracer->endCycle();
  
  return GST_FLOW_OK;
}
```

## Example Pipelines

### 1. Real-time Monitoring

```bash
gst-launch-1.0 \
  v4l2src device=/dev/video0 ! \
  videoconvert ! video/x-raw,format=RGB ! \
  ampinfer model-path=yolo.onnx imgsz=320 ! \
  videoconvert ! video/x-raw,format=BGRA ! \
  ampperformance x-offset=20 y-offset=20 font-size=18 alpha=0.9 ! \
  videoconvert ! autovideosink
```

### 2. Batch Processing with Overlay

```bash
for video in *.mp4; do
  gst-launch-1.0 \
    filesrc location="$video" ! qtdemux ! h264parse ! avdec_h264 ! \
    videoconvert ! video/x-raw,format=RGB ! \
    ampinfer model-path=model.onnx imgsz=160 ! \
    videoconvert ! video/x-raw,format=BGRA ! \
    ampperformance font-size=16 ! \
    videoconvert ! x264enc ! mp4mux ! \
    filesink location="processed_$video"
done
```

### 3. Multiple Streams with Performance

```bash
gst-launch-1.0 \
  compositor name=comp \
    sink_0::xpos=0 sink_0::ypos=0 sink_0::width=640 sink_0::height=480 \
    sink_1::xpos=640 sink_1::ypos=0 sink_1::width=640 sink_1::height=480 ! \
  videoconvert ! autovideosink \
  \
  videotestsrc pattern=0 ! video/x-raw,width=640,height=480,format=RGB ! \
  ampinfer model-path=model1.onnx ! videoconvert ! video/x-raw,format=BGRA ! \
  ampperformance x-offset=10 y-offset=10 ! comp.sink_0 \
  \
  videotestsrc pattern=1 ! video/x-raw,width=640,height=480,format=RGB ! \
  ampinfer model-path=model2.onnx ! videoconvert ! video/x-raw,format=BGRA ! \
  ampperformance x-offset=10 y-offset=10 text-color="#FF0000" ! comp.sink_1
```

## Testing

Run the test script:

```bash
cd /work/scripts
bash test-performance-overlay.sh
```

This generates test videos with performance overlays that can be inspected.

## Troubleshooting

### Issue: No overlay visible

**Solution**: Ensure videoconvert is used before ampperformance and format is BGRA:
```bash
! videoconvert ! video/x-raw,format=BGRA ! ampperformance !
```

### Issue: Overlay position is cut off

**Solution**: Adjust x-offset and y-offset to account for video dimensions and overlay size.

### Issue: Performance metrics show zero

**Solution**: Ensure ampinfer is using Performance Tracer (ScopedTimer) in the code and calling `endCycle()`.

### Issue: Overlay updates too slowly

**Solution**: Reduce update-interval property:
```bash
ampperformance update-interval=1
```

## Architecture

```
┌─────────────────┐
│  Video Stream   │
└────────┬────────┘
         │
         v
┌─────────────────┐
│   ampinfer      │ ◄── Performance Tracer (ScopedTimer)
│  (with timing)  │     Records: preprocessing, inference, postprocessing
└────────┬────────┘
         │
         v
┌─────────────────┐
│  videoconvert   │
│  (to BGRA/RGBA) │
└────────┬────────┘
         │
         v
┌─────────────────┐
│ ampperformance  │ ◄── Reads amp::getGlobalTracer() (thread-safe)
│  (draws overlay)│     Renders with Cairo
└────────┬────────┘
         │
         v
┌─────────────────┐
│   Output        │
└─────────────────┘
```

## Future Enhancements

- [ ] Performance graph visualization
- [ ] Histogram display of timing distribution
- [ ] Custom metric selection (choose which metrics to display)
- [ ] Multiple display layouts (compact, detailed, minimal)
- [ ] OpenGL-based rendering for zero-copy operation
- [ ] WebSocket interface for remote monitoring
- [ ] CSV/JSON export of metrics alongside video

## Related Documentation

- [Performance Tracer Guide](PERFORMANCE_TRACER.md) - Complete API documentation
- [Performance Tracer API](../development/common/PerformanceTracer.h) - Header file reference
- [AMP Inference Element](../development/elements/ampinfer/) - Example integration

## License

LGPL - Same as GStreamer
