# Pipelines

Top-level pipeline presets used by `pek-menu`.

`pek-menu` reads this directory from
`${PEK_PROJECT_ROOT}/config/pipelines`. When `PEK_PROJECT_ROOT` is unset or
empty, it defaults to `/work`. Pipeline strings use the equivalent
`${PEK_PROJECT_ROOT:-/work}` expansion for repository-relative model, media,
and output paths.

The main demo presets typically register their `pekinfer` elements with `active=false`.
This is intentional: open the Perception Experience Kit web UI and enable the models you want from the **AI Models** panel.
`active=false` skips per-frame inference only. Each `pekinfer` still loads its
OpChain and model during startup, so all artifacts referenced by the selected
pipeline must be present.

- `01-full-onnx` — all listed ONNX model pipelines on a video source
- `05-full-onnx-raspicam` — all listed ONNX model pipelines on the Raspberry Pi camera source
- `06-full-onnx-usb-cam` — all listed ONNX model pipelines on the USB camera source at `/dev/video0`
- `cam-connect` — camera-contact demo
- `gaze-detection` — gaze-estimation demo
- `tracker-pc` — ONNX tracking demo
- `yolov11-onnx` — bundled YOLOv11 ONNX object detection viewer demo
- `yolo26-onnx` — YOLO26 ONNX object detection viewer demo

Use `05-full-onnx-raspicam` or `06-full-onnx-usb-cam` when you want a live camera source enabled by default without copying an alternative source into another preset.

`.last_selected_pipeline_id` stores the last chosen top-level pipeline id. `pek-menu -l` uses it to rerun the previous selection.
