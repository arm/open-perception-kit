# YOLO26 ONNX

Full-frame object detector.

- Backend: ONNX Runtime on CPU
- Model file: `yolo26n.onnx`
- Input: NCHW image, `[1, 3, 320, 320]`
- Output: same tensor contract as the checked-in YOLOv11 ONNX model
- Post processor: `YoloParser`
- Labels: COCO class order
- Supported FrameResults payload: `BoxDetectionsT` with `content_type` set to `genericObject`
- Typical use: active object detector in the `yolo26-onnx` viewer pipeline
