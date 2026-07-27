# ArcFace MobileFaceNet Hailo 10

Hailo 10-compiled variant of the ArcFace MobileFaceNet face embedding model.

- Backend: HailoRT
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Input: NHWC face crop, `[1, 112, 112, 3]`, `Uint8`
- Output: dynamic embedding tensor, typically `[1, 512]`
- Post processor: `ObjectEmbeddingParser`
- Supported Perception result: `Perception::ObjectEmbedding` in an `objectEmbedding` layer
- Typical use: run on detected `humanFace` crops after a face detector such as UltraFace

## Verified HEF contract

From `hailortcli parse-hef arcface_mobilefacenet.hef`:

- Input `arcface_mobilefacenet/input_layer1`: `UINT8`, `NHWC(112x112x3)`
- Output `arcface_mobilefacenet/fc1`: `UINT8`, `NC(512)`

The checked-in `model.json` and `opchain.json` match that contract:

- preprocessing targets `ImageRgbHwc` with `Uint8` values
- the opchain consumes `humanFace` crops
- embedding output is parsed with `ObjectEmbeddingParser`
