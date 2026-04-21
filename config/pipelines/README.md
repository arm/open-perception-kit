# Pipelines

Top-level pipeline presets used by `amp-menu`.

The main demo presets typically register their `ampinfer` elements with `active=false`.
This is intentional: open the AMP web UI and enable the models you want from the **AI Models** panel.

- `01-full-onnx` — all listed ONNX model pipelines on a still image
- `02-full-onnx-hailo8` — all listed ONNX + Hailo 8 model pipelines on a still image with ampsink video and optional audio sink
- `03-full-onnx-hailo10` — all listed ONNX + Hailo 10 model pipelines on a still image with ampsink video and optional audio sink
- `cam-connect` — camera-contact demo
- `gaze-detection` — gaze-estimation demo
- `tracker-pc` — ONNX tracking demo
- `tracker-rpi` — Hailo 8 tracking demo

`.last_selected_pipeline_id` stores the last chosen top-level pipeline id. `amp-menu -l` uses it to rerun the previous selection.
