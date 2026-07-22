# OSNet x0.25 ONNX

Re-identification model for object embeddings.

- Backend: ONNX
- Artifact: downloaded on demand from the descriptor's pinned `modelFile` locator when first activated
- Input: NCHW crop, `[1, 3, 256, 128]`, normalized with ImageNet mean/std
- Output: dynamic embedding tensor, typically `[1, 512]`
- Post processor: `ObjectEmbeddingParser`
- Supported Perception result: `Perception::ObjectEmbedding` in an `objectEmbedding` layer
- Typical use: detector and tracker association
