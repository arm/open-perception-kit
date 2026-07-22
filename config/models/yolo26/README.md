# YOLO26 ONNX

Full-frame object detector.

- Backend: ONNX Runtime on CPU
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Input: NCHW image, `[1, 3, 320, 320]`
- Output: same tensor contract as the checked-in YOLOv11 ONNX model
- Post processor: `YoloParser`
- Labels: COCO class order
- Supported Perception result: `Perception::Rect` in a `genericObject` layer
- Typical use: active object detector in the `yolo26-onnx` viewer pipeline
