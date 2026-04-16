# Person Classification

Binary person / non-person classifier.

- Backend: ONNX
- Input: RGB image, `[1, 3, 224, 224]`
- Output: logits `[1, 2]`
- Post processor: `PersonClassificationParser`
- Supported Perception result: none yet; the current parser validates the tensor but does not write a Perception object
- Current status: `PersonClassificationParser` validates the tensor but does not emit a Perception result yet
