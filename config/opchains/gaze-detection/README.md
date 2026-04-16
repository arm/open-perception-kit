# Gaze Detection OpChain

Two-stage gaze detection micropipeline:

1. detect `humanFace` rectangles with UltraFace
2. estimate gaze for each face crop

- Input: one BGRA video frame
- Output: `humanFace` + `eyeYawPitch`
- Typical use: per-face gaze visualization or downstream attention logic
