# MobileNetV2 ImageNet

Whole-frame ImageNet classifier.

- Backend: ONNX
- Artifact: materialized on demand from the descriptor's `modelFile` locator
- Input: NCHW image, `[1, 3, 224, 224]`
- Output: ImageNet class scores
- Post processor: `ImageNetClassificationParser`
- Supported Perception result: `Perception::Classification` in a `classification` layer
- Typical use: scene/image classification without detection boxes
