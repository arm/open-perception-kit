<!-- SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates -->

# Pipelines

Top-level pipeline presets used by `opk-menu`.

`opk-menu` reads this directory from
`${OPK_PROJECT_ROOT}/config/pipelines`. When `OPK_PROJECT_ROOT` is unset or
empty, it defaults to `/work`. Pipeline strings use the equivalent
`${OPK_PROJECT_ROOT:-/work}` expansion for repository-relative model, media,
and output paths.

Each preset contains a string `description` for launcher UIs and a `pipeline`
that is either one GStreamer launch-syntax string or an array of string
fragments joined with spaces. The optional string `sourceInfo` is shown in
`opk-menu` as a short source requirement. The optional boolean `loop` defaults
to `false`. The shared pipeline-preset loader used by both `opk-menu` and the
C++ Runtime validates this format and expands `${VAR}`, `${VAR:-default}`, and
`${VAR?error message}` placeholders. Quote properties containing paths or other
values that may contain spaces, for example `location="${MEDIA_FILE}"`.

## Run a preset

Start the interactive menu, run a preset by ID, print its expanded pipeline
without running it, or repeat the last selection:

```bash
./tools/opk-menu
./tools/opk-menu yolo26n-320
./tools/opk-menu -p full-onnx
./tools/opk-menu -l
```

`opk-menu` runs the selected pipeline in-process through the C++ Runtime. It
still prints an equivalent `gst-launch-1.0` command as a copyable diagnostic;
that text is not a child-process invocation.

When `loop` is `true`, the Runtime uses GStreamer time-segment seeks to return
seekable, finite media to its beginning without reconstructing the pipeline. A
looped preset needs at least one finite, seekable terminal media branch. Purely
live or otherwise non-seekable pipelines must leave `loop` disabled. Element
and Op state therefore remain alive across media iterations and are released
when the pipeline is stopped.

## Logging

Runtime logging defaults to the `error` level and the `stderr` target. Use
`--log-level 0..5` (or `off`, `error`, `warn`, `notice`, `info`, or `debug`) and
`--log-targets stdout,stderr,file` to change the settings for the selected
pipeline. The exact target value `none` disables all asynchronous log output.
`OPK_LOG_FILE` selects the file path when the `file` target is enabled.

## Model activation

Focused presets enable their model or complete detector cascade. The four
`full-onnx` presets register all ten models in dependency-safe order with
YOLO26n-320 active by default. The remaining models can be enabled from the
Open Perception Kit web UI's **Model Selector** panel.

Setting `active=false` skips per-frame inference only. Each `opkinfer` still
loads its OpChain and model during startup, so every artifact referenced by the
selected pipeline must be present.

## Available presets

Full catalog presets:

- `full-onnx` — all ten ONNX models on bundled video.
- `full-onnx-raspicam` — all ten ONNX models on a Raspberry Pi camera.
- `full-onnx-usb-cam` — all ten ONNX models on a USB camera at `/dev/video0`.
- `full-onnx-yuv` — all ten ONNX models on bundled video while preserving the
  decoded pixel format, with an OPKOSD overlay.

Focused detector and cascade presets:

- `ultraface-rfb-320` — UltraFace face detection on bundled video.
- `nitec-resnet-18` — UltraFace followed by NITEC camera-contact inference on
  detected face crops.
- `nitec-resnet-18-executorch` — UltraFace ONNX followed by NITEC
  ExecuTorch/XNNPACK inference.
- `mobilegaze-mobilenet-v2` — UltraFace followed by MobileGaze inference on
  detected face crops.
- `mobilegaze-mobilenet-v2-executorch` — UltraFace ONNX followed by MobileGaze
  ExecuTorch/XNNPACK inference.
- `osnet-x0-25` — YOLO26n-320 followed by OSNet embeddings and object tracking.
- `yolo26n-320`, `yolo26n-480`, and `yolo26n-640` — YOLO26n at the selected
  input resolution on bundled video.
- `yolo26s-320`, `yolo26s-480`, and `yolo26s-640` — YOLO26s at the selected
  input resolution on bundled video.

Use `full-onnx-raspicam` or `full-onnx-usb-cam` when you want a live camera
source enabled by default. Every preset includes `alternative-source-*` and
`alternative-sink-*` examples for adapting image, video, camera, recording,
and audio paths.

## Interactive selection

`.last_selected_pipeline_id` stores the last chosen top-level pipeline ID.
`opk-menu -l` uses it to rerun the previous selection.

When stdin and stdout are attached to a compatible terminal, the interactive
menu supports the Up/Down arrow keys or `j`/`k`, Enter to run the highlighted
pipeline, and `q` or Escape to quit. Typing a pipeline number and pressing
Enter remains supported, and `0` selects the last used pipeline. Set
`NO_COLOR=1` to disable menu colors. When terminal interaction is unavailable,
`opk-menu` automatically uses its line-oriented numeric menu.
