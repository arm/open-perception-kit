# MobileNetV2 ImageNet

Whole-frame ImageNet classifier.

- Backend: ONNX
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Input: NCHW image, `[1, 3, 224, 224]`
- Output: ImageNet class scores
- Post processor: `ImageNetClassificationParser`
- Supported Perception result: `Perception::Classification` in a `classification` layer
- Typical use: scene/image classification without detection boxes
