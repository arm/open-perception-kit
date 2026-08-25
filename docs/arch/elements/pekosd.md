---
sidebar_position: 2
sidebar_label: pekosd
---

# pekosd

`pekosd` is a `GstVideoFilter` that renders `FrameResults` payloads onto BGRA video
frames. It is a presentation/debugging stage and does not modify `FrameResults`
itself.

## Element Contract

- Base class: `GstVideoFilter`
- Processing mode: in-place `transform_frame_ip`
- Pad caps: `video/x-raw, format=BGRA`
- Metadata dependency: `FrameResultsMeta`
- Main property: `enabled`

If no `FrameResultsMeta` is attached, the frame passes through unchanged.

## Execution Model

For each frame, `pekosd` reads the immutable `FrameResults` envelope, creates one or
more Cairo-backed overlay layers, composites those layers onto the input frame,
and returns the modified frame downstream.

The current implementation uses CPU-based Cairo rendering over linear BGRA memory.
Future DMABUF/Vulkan-style rendering is a planned direction, not the current path.

## Rendering Model

Rendering is driven by generated payload types and `LayerInfoT::contentType`.
Supported paths include:

- `genericObject`: bounding boxes and labels
- `humanFace`: face-centered circular overlays
- `classification`: top-k classification text
- `eyeYawPitch`: gaze direction vectors anchored to parent faces
- `cameraContact`: face-centered contact status dot
- `segmentation`: alpha-blended segmentation maps
- `trackTrace`: tracker history lines
- `perfdata`: performance text written by `pekperformance`

UUID parent relationships are used when a rendered result depends on another
object, such as gaze vectors anchored to face rectangles.

## Memory And Performance

- Frames are modified in place.
- Overlay layers are temporary Cairo ARGB32 surfaces.
- No full-frame copy is required for the final canvas.
- Rendering is CPU-bound and assumes linear image memory.

Treat `pekosd` as a debugging overlay rather than the long-term application UI
contract.
