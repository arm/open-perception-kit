# YOLO26n ONNX

Full-frame object detector.

- Backend: ONNX Runtime on CPU
- Model file: `yolo26n.onnx`
- Input: NCHW image, `[1, 3, 320, 320]`
- Output: same tensor contract as the checked-in YOLOv11 ONNX model
- Post processor: `YoloParser`
- Labels: COCO class order
- Supported Perception result: `Perception::Rect` in a `genericObject` layer
- Typical use: active object detector in the `yolo26n-onnx` viewer pipeline
