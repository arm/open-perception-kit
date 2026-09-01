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
  pipeline JSON through `pek::runtime::Pipeline` and receiving serialized Perception packet callbacks.
- `yolo-benchmark`: YOLO benchmark comparing bare Ultralytics with PEK using
  either a preloaded COCO image list or a pinned video. Each mode emits matching
  Bare/PEK `benchmark_summary.json` artifacts.
- `smart-doorway`: Python application sketch that launches an OPK pipeline,
  watches metadata, gates face and gaze stages from a person detector, and shows
  the pipeline shape in a small browser UI.
