# Development Examples

This directory contains proof-of-concept applications that are tightly coupled
to the checked-in Open Perception Kit runtime, configuration, pipeline, and SDK
surfaces.

The C++ runtime API examples are part of the main Meson build. Running
`./scripts/build.sh` builds them with the repository's normal dependencies and
stages their binaries into `tools/` beside `opk-menu`.

## Available Examples

- `opchain-exec`: command-line image input proof of concept that loads an image
  through `opk::runtime::Tools`, wraps it as a `opk::runtime::VideoFrame`, runs
  an OpChain through `opk::runtime::OpChain`, and prints the serialized result.
- `pipeline-exec`: C++ application facade proof of concept for loading an OPK
  pipeline JSON through `opk::runtime::Pipeline` and receiving serialized
  FrameResults packet callbacks.
- `byom-blazeface`: Python/config application sketch that brings a BlazeFace
  model through OPK model, OpChain, pipeline, Python postprocessing, and
  FrameResults packet consumption surfaces.
