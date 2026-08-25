# MobileNetV2 ImageNet

Whole-frame ImageNet classifier.

- Backend: ONNX
- Input: NCHW image, `[1, 3, 224, 224]`
- Output: ImageNet class scores
- Post processor: `ImageNetClassificationParser`
- Supported FrameResults payload: `ClassificationsT` with `content_type` set to `classification`
- Typical use: scene/image classification without detection boxes
