# YOLOv11n Hailo 10

Official Hailo 10H-compiled YOLOv11n object detection model from Hailo Model Zoo.

- Backend: HailoRT
- Source: Hailo Model Zoo HAILO10H public object detection models
- Source file: `HAILO10H_object_detection.rst`
- HEF download: `https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v5.3.0/hailo10h/yolov11n.hef`
- Model: `yolov11n`
- Input: NHWC image, `[1, 640, 640, 3]`, `Uint8`
- Float mAP: 39.0
- Hardware mAP: 37.9
- Batch-size-1 FPS: 303
- Model size: 344 MB
- Output: packed Hailo NMS detections
- Post processor: `YoloParser`
- Supported FrameResults payload: `BoxDetectionsT` with `content_type` set to `genericObject`
- Typical pairing: `config/pipelines/tracker-rpi-hailo10.json` and `config/opchains/tracking/opchain-hailo-v10.json`

This model was selected from the official HAILO10H object detection table as the fastest YOLOv11 detector available there for Hailo 10H.
