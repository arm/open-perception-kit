# SCRFD 2.5G Hailo 10

Hailo 10-compiled SCRFD 2.5G face detector.

- Backend: HailoRT
- Input: NHWC full-frame image, `[1, 640, 640, 3]`, `Uint8`
- Output: 9 dynamic tensors across strides 8, 16, and 32 for face score, box regression, and landmark regression
- Post processor: `ScrfdParser`
- Supported Perception result: `Perception::Rect` in a `humanFace` layer
- Typical use: first stage for face-crop pipelines such as ArcFace embedding extraction

## Verified HEF contract

From `hailortcli parse-hef ./scrfd_2.5g.hef`:

- Input `scrfd_2_5g/input_layer1`: `UINT8`, `NHWC(640x640x3)`
- Outputs:
  - `conv43`: `NHWC(80x80x8)`
  - `conv42`: `NHWC(80x80x2)`
  - `conv44`: `NHWC(80x80x20)`
  - `conv50`: `NHWC(40x40x8)`
  - `conv49`: `NHWC(40x40x2)`
  - `conv51`: `NHWC(40x40x20)`
  - `conv56`: `NHWC(20x20x8)`
  - `conv55`: `NHWC(20x20x2)`
  - `conv57`: `NHWC(20x20x20)`

The checked-in parser currently uses the score and box tensors to emit `humanFace` rectangles.
Landmark tensors are validated and reserved for future alignment work, but they are not yet emitted
as structured runtime output.
