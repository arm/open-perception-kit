# MODNet

Full-frame matting / segmentation model.

- Backend: ONNX
- Input: RGB image, `[1, 3, 128, 128]`
- Output:  a single-channel forergound mask image `[1, 1, H, W]`
- Post processor: `ModNetSegmentationParser`
- Supported Perception result: `Perception::SegmentationMap` in a `segmentation` layer
- Typical use: foreground/background segmentation/masking
