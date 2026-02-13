# AMP Development Forge – Copilot Instructions

## Big picture (architecture + data flow)
- GStreamer plugins live in development/elements/ and are built by Meson (development/meson.build).
- ampinfer loads an OpChain from JSON (etc/models/*/opchain.json) and executes ops from ops-onnx/, ops-std/, ops-hailort/, and optionally ops-executorch/ (see Plugin.cpp factories).
- ampinfer writes PerceptionContextMeta on buffers; amposd and ampperformance read Perception/metrics to render overlays.
- ampsink is a GstBin with WebRTC + HTTP/WS control/status; embedded server is enabled only if libsoup + json-glib are found.
- Shared types/utilities: Perception in development/common/amp/Perception.h, Op/OpChain in development/common/op/, PerformanceTracer in development/common/PerformanceTracer.*.

## Critical workflow (DevContainer only)
- Always build and test inside the DevContainer; host lacks GStreamer/ONNX/Meson.
- Build (scripts/build-elements.sh):
   - debug [tests?]: ./scripts/build-elements.sh debug [true|false]
   - release [tests?]: ./scripts/build-elements.sh release [true|false]
   - ExecuTorch: ./scripts/build-elements.sh debug_with_executorch
- Build artifacts land in development/build/meson-out; test scripts set GST_PLUGIN_PATH to that path.
- Tests are gst-launch pipelines in scripts/test-elements.sh (onnx, video, audio, single_cam, etc.) using assets in etc/.

## Project-specific conventions
- Elements generally assume BGRA video (see ampinfer/amposd/ampperformance caps). Preserve caps expectations.
- Web mode is always compiled (AMP_WEB_MODE=1); web server is optional (AMP_WEB_SERVER=0/1) based on libsoup/json-glib.
- Op plugins expose amp_create_op_instance/amp_delete_op_instance in ops-*/Plugin.cpp; new ops must register here.
- Performance metrics use PerformanceTracer; overlay data is surfaced via Perception.perfdata.

## Tooling notes
- clang-format is enforced in CI (see .clang-format and .github/workflows/clang-format-check.yml).
- For clangd, symlink compile_commands.json from development/build to repo root after first build.
