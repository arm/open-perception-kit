# Face Embedding Hailo 10

Two-stage face embedding micropipeline for Hailo 10:

1. detect `humanFace` rectangles with SCRFD 2.5G
2. run ArcFace MobileFaceNet on each detected face crop

- Detector stage: `config/models/scrfd_2.5g/`
- Embedding stage: `config/models/arcface_mobilefacenet_hailo10/`
- Output: `humanFace` rectangles and `objectEmbedding`
- Typical use: face embedding extraction for downstream matching logic
