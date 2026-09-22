# Tracking OpChains

Detector + ReID micropipeline for `opktracker`.

- `opchain-onnx.json`: YOLOv11 ONNX + OSNet ONNX
- Input: one supported raw video frame
- Output: `genericObject` rectangles + `objectEmbedding`
- Typical use: feed `opktracker` element for stable multi-object tracking
