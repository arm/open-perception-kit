---
sidebar_position: 2
sidebar_label: opkosd
---

# opkosd

`opkosd` is a `GstVideoFilter` that renders `FrameResults` payloads onto OPK video
frames. It is a presentation/debugging stage and does not modify `FrameResults`
itself.

## Element Contract

- Base class: `GstVideoFilter`
- Processing mode: in-place `transform_frame_ip`
- Pad caps: `video/x-raw, format={BGRA,RGB,I420,NV12,YUY2}`
- Metadata dependency: `FrameResultsMeta`
- Properties:
  - `enabled`: enable or disable drawing
  - `performance-overlay-enabled`: enable or disable `perfdata` drawing
  - `bg-image`: optional replacement background image for segmentation masks

If no `FrameResultsMeta` is attached, the frame passes through unchanged.

## Execution Model

For each frame, `opkosd` reads the immutable `FrameResults` envelope, draws supported
overlays directly onto the negotiated input frame, and returns the modified frame
downstream. Future DMABUF/Vulkan-style rendering is a planned direction, not the
current path.

## Rendering Model

Rendering is driven by generated payload types and `LayerInfoT::contentType`.
Supported paths include:

- `genericObject`: bounding boxes and labels
- `humanFace`: face-centered circular overlays
- `classification`: top-k classification text
- `personClassification`: centered person / non-person status text
- `eyeYawPitch`: gaze direction vectors anchored to parent faces
- `cameraContact`: face-centered contact status dot
- `segmentation`: alpha-blended segmentation maps, or background replacement
  when `LayerInfoT::compositingMode` requests it and `bg-image` is configured
- `trackTrace`: tracker history lines
- `perfdata`: performance text written by `opkperformance`

UUID parent relationships are used when a rendered result depends on another
object, such as gaze vectors anchored to face rectangles.

## Memory And Performance

- Frames are modified in place.
- No full-frame overlay canvas is allocated.
- Rendering is CPU-bound and assumes linear image memory.

Treat `opkosd` as a debugging overlay rather than the long-term application UI
contract.
