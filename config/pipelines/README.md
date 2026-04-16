# Pipelines

Top-level pipeline presets used by `amp-menu`.

- `01-full-onnx` — all listed ONNX model pipelines on a still image
- `02-full-onnx-hailo` — all listed ONNX + Hailo pipelines on camera + audio input
- `cam-connect` — camera-contact demo
- `gaze-detection` — gaze-estimation demo
- `tracker-pc` — ONNX tracking demo
- `tracker-rpi` — Hailo tracking demo

`.last_selected_pipeline_id` stores the last chosen top-level pipeline id. `amp-menu -l` uses it to rerun the previous selection.
