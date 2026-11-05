# UltraFace through LOOM

This example loads the sibling Optimized-Runtimes UltraFace library and its
one-thread reference model package. Build the artifacts with `make runtime`
and `make model` in `0/ultraface-raspberry-pi-5/` first.

Run with OPK's `tools/opchain-exec development/examples/loom-ultraface/opchain.json image.jpg`.
The descriptor uses RGB NCHW Float32 `[1,3,240,320]`, the existing UltraFace
normalization, and fixed outputs `[1,4420,2]` and `[1,4420,4]`.

OpChain version `1.5.0` and `scoresAreLogits: true` request stable two-class softmax before the existing
SSD decode and NMS (confidence 0.1, IoU 0.3). Existing ONNX examples keep their
probability semantics because this option defaults to false. Rebuild
`opk-std-ops.so` before using this example; an older parser ignores this option
and produces incorrect confidences. Change `modelFile` to `model-2t.ufw` or
`model-4t.ufw` to select the other worker policies.

See the workspace [tutorial](../../../../TUTORIAL.md) for exact setup and the
current-source ARM64 VM verification results.
