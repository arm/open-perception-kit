---
sidebar_position: 19
sidebar_label: amposd
---

# amposd
## On-Screen Display Element for Perception Visualization

`amposd` is a GStreamer `GstVideoFilter` element that renders structured
`Perception` metadata onto ARGB/BGRA video frames.

This component currently supports 32bit ARGB drawing only, what determines the required video frame format.
If there is no decoration over the video it is likely because of the mismached format.

It uses the received `Perception` instace attached to buffers, generates
intermediate Cairo drawing layers, and composites them over the input frame
in-place.

In a later stage of development a Vulkan or OpenGL based rendering path also will be 
implemented to make DMA-BUF/zero copy pipelines possible.

The element operates purely as a visualization stage and does not modify Perception itself.

---

# Integration

- Element type: `GstVideoFilter`
- Pad caps: `video/x-raw, format=BGRA`
- Processing mode: in-place (`transform_frame_ip`)
- Metadata dependency: `PerceptionContextMeta`

`amposd` can be inserted anywhere downstream of `ampinfer`
or other elements that attach `Perception` metadata.

---

# Execution Model

For each incoming frame:

1. Retrieve `PerceptionContextMeta` from the buffer.
2. Access the immutable `Perception` payload.
3. Generate one or more Cairo-backed overlay layers.
4. Composite the layers over the input frame using `CAIRO_OPERATOR_OVER`.
5. Return the modified frame downstream.

All drawing occurs in-memory using Cairo image surfaces.

---

# Drawing Architecture

## Layer Abstraction

`Osd::Layer` wraps a Cairo ARGB32 surface and its drawing context.
Each logical overlay (detections, segmentation, performance data)
is rendered into its own layer.

Layers are later composited onto the final canvas.

This layered approach provides:

- Separation of overlay concerns.
- Independent blending behavior.
- Ordered composition.

---

## Canvas

`Osd::Canvas` wraps the actual video frame memory using
`cairo_image_surface_create_for_data`.

The final paint operation blends all prepared layers into the frame.

No additional frame copies are created.

---

# Supported Perception Content Types

Rendering is driven by `Perception::Layer::contentType`.

Currently supported:

## genericObject

- Renders bounding rectangles.
- Displays object label text.
- Draws rectangle outlines with configurable thickness.

## humanFace

- Renders circular overlays centered on detected faces.
- Circle radius derived from bounding box width.

## classification

- Renders top-k classification candidates.
- Draws label list in lower-left corner.
- Displays rank and confidence percentage.

## eyeYawPitch

- Renders gaze direction vectors.
- Computes endpoint from yaw/pitch angles.
- Draws arrow from face center to projected gaze endpoint.

## cameraContact

- Renders a face-centered status dot.
- Draws a green dot when the subject is looking at the camera.
- Draws a red dot when the subject is not looking at the camera.

## ocrDetectionSegmentation

- Renders segmentation map as alpha-blended overlay.
- Performs min-max normalization of map values.
- Supports automatic scaling when segmentation resolution differs from frame size.

---

# Gaze Vector Rendering

Yaw/pitch values are converted to screen-space vectors:

- Degrees converted to radians.
- Tangent-based directional projection.
- Normalized direction vector.
- Arrow drawn with adjustable head geometry.

Vectors are anchored to the parent face rectangle center,
resolved via UUID-based parent relationships.

---

# Performance Overlay

`Perception::perfdata` lines are rendered in the upper-left corner.

- Monospace font.
- Semi-transparent background.
- One line per performance entry.

This allows lightweight runtime profiling visualization.

---

# Properties

`enabled` (boolean)

- Enables or disables OSD rendering.
- When disabled, frames pass through unmodified.

---

# Memory and Performance Characteristics

- In-place modification of BGRA frames.
- No intermediate frame duplication.
- Cairo ARGB32 overlays composited over original buffer.
- CPU-based rendering.
- Current implementation requires linear image memory.

Future extension plans include:

- DMABUF-backed rendering.
- Vulkan-based zero-copy overlay.
- Reduced CPU overhead for high-resolution streams.

---

# Design Characteristics

- Metadata-driven rendering.
- Fully decoupled from inference logic.
- Supports multiple detection types in a single frame.
- Layered composition model.
- Deterministic rendering order.

`amposd` acts strictly as a presentation stage,
bridging structured Perception metadata to human-readable visualization.
