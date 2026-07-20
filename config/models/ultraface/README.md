# UltraFace

Full-frame face detector.

- Backend: ONNX
- Artifact: downloaded during container initialization from the descriptor's `modelFile` locator
- Input: NCHW image, `[1, 3, 240, 320]`
- Output: score and box tensors
- Post processor: `UltrafaceParser`
- Supported Perception result: `Perception::Rect` in a `humanFace` layer
- Typical use: first stage for face-based pipelines and face detection
