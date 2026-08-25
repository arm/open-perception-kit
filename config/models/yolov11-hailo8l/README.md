# YOLOv11 Hailo 8L

Hailo 8-compiled variant of the YOLOv11 detector.

- Backend: HailoRT
- Input: NHWC image, `[1, 320, 320, 3]`, `Uint8`
- Output: packed Hailo NMS detections
- Post processor: `YoloParser`
- Supported FrameResults payload: `BoxDetectionsT` with `content_type` set to `genericObject`
- Note: this `.hef` is the compiled Hailo version of the original ONNX model
- Typical pairing: `config/pipelines/03-full-onnx-hailo8l.json`

Example export commands:

```bash
hailo parser onnx yolo11n-fp32-320.onnx --tensor-shapes [1,3,320,320] --hw-arch hailo8l
hailo optimize yolo11n-fp32-320.har --use-random-calib-set --hw-arch hailo8l
hailo compiler yolo11n-fp32-320_optimized.har --hw-arch hailo8l
```
