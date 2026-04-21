# Tracking OpChains

Detector + ReID micropipeline for `amptracker`.

- `opchain-onnx.json`: YOLOv11 ONNX + OSNet ONNX
- `opchain-hailo.json`: YOLOv11 Hailo 8 + OSNet Hailo 8
- Input: one BGRA video frame
- Output: `genericObject` rectangles + `objectEmbedding`
- Typical use: feed `amptracker` element for stable multi-object tracking
