# Tracking OpChains

Detector + ReID micro-pipeline for `amptracker`.

- `opchain-onnx.json`: YOLOv11 ONNX + OSNet ONNX
- `opchain-hailo.json`: YOLOv11 Hailo + OSNet Hailo
- Input: one BGRA video frame
- Output: `genericObject` rectangles + `objectEmbedding`
- Typical use: feed `amptracker` element for stable multi-object tracking
