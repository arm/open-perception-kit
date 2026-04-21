# Camera Contact OpChain

Two-stage camera-contact micro-pipeline:

1. Detect `humanFace` rectangles with UltraFace.
2. Classify each detected face crop with the camera-contact model.

- Input: one BGRA video frame
- Output: `humanFace` and `cameraContact`
- Typical use: per-face camera-contact visualization
