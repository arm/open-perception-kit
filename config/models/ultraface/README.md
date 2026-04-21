# UltraFace

Full-frame face detector.

- Backend: ONNX
- Input: NCHW image, `[1, 3, 240, 320]`
- Output: score and box tensors
- Post processor: `UltrafaceParser`
- Supported Perception result: `Perception::Rect` in a `humanFace` layer
- Typical use: first stage for face-based pipelines and face detection
