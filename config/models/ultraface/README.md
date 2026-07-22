# UltraFace

Full-frame face detector.

- Backend: ONNX
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Input: NCHW image, `[1, 3, 240, 320]`
- Output: score and box tensors
- Post processor: `UltrafaceParser`
- Supported Perception result: `Perception::Rect` in a `humanFace` layer
- Typical use: first stage for face-based pipelines and face detection
