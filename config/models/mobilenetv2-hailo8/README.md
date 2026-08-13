# MobileNetV2 ImageNet Hailo 8

Hailo 8-compiled variant of the MobileNetV2 ImageNet classifier.

- Backend: HailoRT
- Input: NCHW image, `[1, 3, 224, 224]`
- Output: ImageNet classification handled like the ONNX variant
- Post processor: `ImageNetClassificationParser`
- Supported FrameResults payload: `ClassificationsT` with `content_type` set to `classification`
- Note: this `.hef` is the compiled Hailo version of the original ONNX model
- Typical pairing: `config/pipelines/02-full-onnx-hailo8.json`

Example export commands:

```bash
hailo parser onnx mobilenet_v2_1.4_224.onnx --tensor-shapes [1,3,224,224]
hailo optimize mobilenet_v2_1_4_224.har --use-random-calib-set
hailo compiler mobilenet_v2_1_4_224_optimized.har
```
