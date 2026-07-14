# MODNet

Full-frame matting / segmentation model.

- Backend: ONNX
- Artifact: materialized on demand from the descriptor's `modelFile` locator
- Input: NCHW image, `[1, 3, 128, 128]`
- Output:  a single-channel foreground mask image `[1, 1, H, W]`
- Post processor: `ModNetSegmentationParser`
- Supported Perception result: `Perception::SegmentationMap` in a `segmentation` layer
- Typical use: foreground/background segmentation/masking
