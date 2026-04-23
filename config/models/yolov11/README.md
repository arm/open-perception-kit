# YOLOv11 ONNX

Full-frame object detector.

- Backend: ONNX
- Input: NCHW image, `[1, 3, 320, 320]`
- Output: dynamic detection tensor containing bounding boxes and labels
- Post processor: `YoloParser`
- Supported Perception result: `Perception::Rect` in a `genericObject` layer
- Typical use: first stage for object detection and tracking input
