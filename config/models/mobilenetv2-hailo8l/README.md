# MobileNetV2 ImageNet Hailo 8L

Hailo 8-compiled variant of the MobileNetV2 ImageNet classifier.

- Backend: HailoRT
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Input: NCHW image, `[1, 3, 224, 224]`
- Output: ImageNet classification handled like the ONNX variant
- Post processor: `ImageNetClassificationParser`
- Supported Perception result: `Perception::Classification` in a `classification` layer
- Note: this `.hef` is the compiled Hailo version of the original ONNX model
- Typical pairing: `config/pipelines/03-full-onnx-hailo8l.json`
