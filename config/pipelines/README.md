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

`opk-menu` runs the selected description in-process through the C++ Runtime. It
still prints an equivalent `gst-launch-1.0` command as a copyable diagnostic;
that text is not a child process invocation. When `loop` is `true`, the Runtime
uses GStreamer time-segment seeks to return seekable, finite media to its
beginning without reconstructing the pipeline. A looped preset needs at least
one finite, seekable terminal media branch; purely live or otherwise
non-seekable pipelines must leave `loop` disabled. Element and Op state
therefore remain alive across media iterations and are released when the
pipeline is stopped.

Runtime logging defaults to the `error` level and the `stderr` target. Use
`--log-level 0..5` (or `off`, `error`, `warn`, `notice`, `info`, or `debug`) and
`--log-targets stdout,stderr,file` to change the settings for the selected
pipeline. The exact target value `none` disables all asynchronous log output.
`OPK_LOG_FILE` selects the file path when the `file` target is enabled.

The main demo presets typically register their `opkinfer` elements with `active=false`.
This is intentional: open the Open Perception Kit web UI and enable the models you want from the **Model Selector** panel.
`active=false` skips per-frame inference only. Each `opkinfer` still loads its
OpChain and model during startup, so all artifacts referenced by the selected
pipeline must be present.

- `cam-connect` — camera-contact demo on bundled video
- `full-onnx` — ONNX models on bundled video
- `full-onnx-raspicam` — ONNX models on the Raspberry Pi camera source
- `full-onnx-usb-cam` — ONNX models on the USB camera source at `/dev/video0`
- `full-onnx-yuv` — ONNX models on bundled video with original pixel format and OPKOSD overlay
- `gaze-detection` — gaze-estimation demo on bundled video
- `mobilenet-python-classification` — MobileNetV2 classification on bundled image with C++ and Python results
- `rvm` — RVM segmentation on bundled video with OPKOSD overlay
- `rvm-raspicam` — RVM segmentation on the Raspberry Pi camera source with OPKOSD overlay
- `rvm-usb-cam` — RVM segmentation on the USB camera source at `/dev/video0` with OPKOSD overlay
- `tracker-executorch` — ExecuTorch tracking demo on bundled video
- `tracker-pc` — ONNX tracking demo on bundled video
- `yolo26-onnx` — YOLO26 ONNX object detection viewer demo
- `yolov11-onnx` — bundled YOLOv11 ONNX object detection viewer demo

Use `full-onnx-raspicam` or `full-onnx-usb-cam` when you want a live camera source enabled by default without copying an alternative source into another preset.

`.last_selected_pipeline_id` stores the last chosen top-level pipeline id. `opk-menu -l` uses it to rerun the previous selection.

When stdin and stdout are attached to a compatible terminal, the interactive
menu supports the Up/Down arrow keys or `j`/`k`, Enter to run the highlighted
pipeline, and `q` or Escape to quit. Typing a pipeline number and pressing
Enter remains supported, and `0` selects the last used pipeline. Set
`NO_COLOR=1` to disable menu colors. When terminal interaction is unavailable,
`opk-menu` automatically uses its line-oriented numeric menu.
