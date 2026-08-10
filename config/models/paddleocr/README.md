# PaddleOCR

Text-region detection integration.

- Backend: ONNX
- Input: NCHW image, `[1, 3, 640, 640]`
- Output: detection mask of the text region
- Post processor: `PaddleOcrDetectionParser`
- Supported Perception result: `Perception::SegmentationMap` in a `segmentation` layer
- Note: only the detection stage is wired; recognition is present in the folder but not used by current `opchain.json`
- Recognition descriptor input: `[1, 3, 48, 640]`; the ONNX input fixes height at 48 and
  accepts a dynamic width, while the descriptor retains 640 as its concrete preprocessing width
