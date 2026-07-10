# Examples

This directory contains proof-of-concept applications that use the public PEK runtime API.

Examples here are intentionally not treated as supported installed tools yet.
They are small integration sketches for validating runtime API shape and use cases.
If an example becomes stable and generally useful, it can be promoted later into `tools/`.

## Available Examples

- `opchain-exec`: command-line image input proof of concept that loads an image
  through `pek::runtime::Tools`, wraps it as a `pek::runtime::VideoFrame`, runs an
  OpChain through `pek::runtime::OpChain`, and prints the serialized result.
- `pipeline-exec`: C++ application facade proof of concept for loading a PEK
  pipeline JSON through `pek::runtime::Pipeline` and receiving serialized perception JSON callbacks.
- `yolo-benchmark`: manual YOLO benchmark example that compares the bare
  Ultralytics predict loop and PEK OpChain runner using the same preloaded image
  list and common `benchmark_summary.json` artifact.
