# Person Classification

Binary person / non-person classifier.

- Backend: ONNX
- Artifact: materialized on demand from the descriptor's `modelFile` locator
- Input: NHWC image, `[1, 96, 96, 3]`
- Output: logits `[1, 2]` (first is the person prob)
- Post processor: `PersonClassificationParser`
- Supported Perception result: PersonClassification
- Current status: `PersonClassificationParser` validates the tensor and emits a Perception result
