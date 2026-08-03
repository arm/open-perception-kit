# RVM

Recurrent video matting model.

- Backend: ONNX
- Main input: NCHW image, `[1, 3, 256, 256]`
- Extra inputs: four recurrent state tensors are reused from the previous inference step through `tensorFeedbacks`, so the model can keep temporal context between frames
- Post processor: `RvmParser`
- Supported FrameResults payload: `SegmentationMasksT` with `content_type` set to `segmentation`
- Typical use: foreground/background segmentation/masking of a person
