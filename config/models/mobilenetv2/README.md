# MobileNetV2 ImageNet

Whole-frame ImageNet classifier.

- Backend: ONNX
- Input: RGB image, `[1, 3, 224, 224]`
- Output: ImageNet class scores
- Post processor: `ImageNetClassificationParser`
- Supported Perception result: `Perception::Classification` in a `classification` layer
- Typical use: scene/image classification without detection boxes
