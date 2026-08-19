# MobileNetV2 ImageNet

Whole-frame ImageNet classifier.

- Backend: ONNX
- Input: NCHW image, `[1, 3, 224, 224]`
- Output: ImageNet class scores
- Post processor: `ImageNetClassificationParser`
- Supported FrameResults payload: `ClassificationsT` with `content_type` set to `classification`
- Typical use: scene/image classification without detection boxes

The optional `opchain-python-overlay.json` variant runs
`scripts/tensor_metrics_overlay.py` between inference and the standard
postprocessor. The script independently calculates the top five ImageNet
classes from the raw output tensor and tracks how many consecutive frames keep
the same top class. The `mobilenet-python-op` pipeline continuously classifies a
bundled real sample image and uses the WebUI as the single overlay renderer. The
Python list appears in the lower-right corner and the standard postprocessor
list appears in the lower-left. Both sides use the same five-row rank, label,
and confidence format; the stable-frame count remains available in the Python
layer metadata.

The Python demo uses the model-local `scripts/imagenet_labels.txt` table so it
can produce a human-readable label before the C++ postprocessor executes. Keep
that table aligned with the built-in ImageNet labels when they change.
