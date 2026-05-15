# Pipelines

Top-level pipeline presets used by `pek-menu`.

The main demo presets typically register their `pekinfer` elements with `active=false`.
This is intentional: open the Perception Experience Kit web UI and enable the models you want from the **AI Models** panel.

- `01-full-onnx` — all listed ONNX model pipelines on a video source
- `02-full-onnx-hailo8` — all listed ONNX + Hailo 8 model pipelines on a video source with peksink video and optional audio sink
- `03-full-onnx-hailo8l` — all listed ONNX + Hailo 8L model pipelines on a video source with peksink video and optional audio sink
- `04-full-onnx-hailo10` — all listed ONNX + Hailo 10 model pipelines on a video source with peksink video and optional audio sink
- `05-full-onnx-raspicam` — all listed ONNX model pipelines on the Raspberry Pi camera source
- `06-full-onnx-usb-cam` — all listed ONNX model pipelines on the USB camera source at `/dev/video0`
- `cam-connect` — camera-contact demo
- `gaze-detection` — gaze-estimation demo
- `tracker-pc` — ONNX tracking demo
- `tracker-rpi-hailo8` — Hailo 8 tracking demo

Use `05-full-onnx-raspicam` or `06-full-onnx-usb-cam` when you want a live camera source enabled by default without copying an alternative source into another preset.

`.last_selected_pipeline_id` stores the last chosen top-level pipeline id. `pek-menu -l` uses it to rerun the previous selection.
