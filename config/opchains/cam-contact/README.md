# Camera Contact OpChain

Two-stage camera contact micropipeline:

1. detect `humanFace` rectangles with UltraFace
2. classify each face crop with the camera-contact model. The model determines fi a person looks at the camera or not.

- Input: one BGRA video frame
- Output: `humanFace` + `cameraContact`
- Typical use: per-face camera-contact visualization
