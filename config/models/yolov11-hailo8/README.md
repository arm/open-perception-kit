# YOLOv11 Hailo 8

Hailo 8-compiled variant of the YOLOv11 detector.

- Backend: HailoRT
- Input: NHWC image, `[1, 320, 320, 3]`, `Uint8`
- Output: packed Hailo NMS detections
- Post processor: `YoloParser`
- Supported Perception result: `Perception::Rect` in a `genericObject` layer
- Note: this `.hef` is the compiled Hailo version of the original ONNX model
- Typical pairing: `config/pipelines/02-full-onnx-hailo8.json` and `config/opchains/tracking/opchain-hailo-v8.json`

Example export commands:

```bash
hailo parser onnx yolo11n-fp32-320.onnx --tensor-shapes [1,3,320,320]
hailo optimize yolo11n-fp32-320.har --use-random-calib-set
hailo compiler yolo11n-fp32-320_optimized.har
```
