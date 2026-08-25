# YOLOv11 ONNX

Full-frame object detector.

- Backend: ONNX
- Input: NCHW image, `[1, 3, 320, 320]`
- Output: dynamic detection tensor containing bounding boxes and labels
- Post processor: `YoloParser`
- Supported FrameResults payload: `BoxDetectionsT` with `content_type` set to `genericObject`
- Typical use: first stage for object detection and tracking input
