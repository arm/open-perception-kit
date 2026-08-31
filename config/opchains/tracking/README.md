# Tracking OpChains

Detector + ReID micropipeline for `pektracker`.

- `opchain-onnx.json`: YOLOv11 ONNX + OSNet ONNX
- Input: one BGRA video frame
- Output: `genericObject` rectangles + `objectEmbedding`
- Typical use: feed `pektracker` element for stable multi-object tracking
