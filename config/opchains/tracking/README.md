# Tracking OpChains

Detector + ReID micropipeline for `pektracker`.

- `opchain-onnx.json`: YOLOv11 ONNX + OSNet ONNX
- `opchain-hailo-v8.json`: YOLOv11 Hailo 8 + OSNet Hailo 8
- `opchain-hailo-v10.json`: YOLOv11n Hailo 10 + RepVGG A0 person ReID Hailo 10
- Input: one BGRA video frame
- Output: `genericObject` rectangles + `objectEmbedding`
- Typical use: feed `pektracker` element for stable multi-object tracking
