# RepVGG A0 Person ReID 512 Hailo 10

Official Hailo 10H-compiled RepVGG A0 person ReID model from Hailo Model Zoo.

- Backend: HailoRT
- Source: Hailo Model Zoo HAILO10H public person ReID models
- Source file: `HAILO10H_person_re_id.rst`
- HEF download: `https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v5.3.0/hailo10h/repvgg_a0_person_reid_512.hef`
- Model: `repvgg_a0_person_reid_512`
- Dataset: Market1501
- Input: NHWC image, `[1, 256, 128, 3]`, `Uint8`
- Float rank1: 89.9
- Hardware rank1: 89.7
- Batch-size-1 FPS: 3538
- Batch-size-8 FPS: 3551
- Params: 7.68M
- OPS: 1.78G
- Output size: 512
- Post processor: `ObjectEmbeddingParser`
- Supported Perception result: `Perception::ObjectEmbedding` in an `objectEmbedding` layer
- Typical pairing: `config/pipelines/tracker-rpi-hailo10.json` and `config/opchains/tracking/opchain-hailo-v10.json`

This model was selected from the official HAILO10H person ReID table for high-throughput embedding generation.
