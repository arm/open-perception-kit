# RVM

Recurrent video matting model.

- Backend: ONNX
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Main input: NCHW image, `[1, 3, 256, 256]`
- Extra inputs: four recurrent state tensors are reused from the previous inference step through `tensorFeedbacks`, so the model can keep temporal context between frames
- Post processor: `RvmParser`
- Supported Perception result: `Perception::SegmentationMap` in a `segmentation` layer
- Typical use: foreground/background segmentation/masking of a person
