# Gaze Detection

Face-level gaze estimator for yaw and pitch.

- Backend: ONNX
- Input: NCHW crop, `[1, 3, 448, 448]`, ImageNet mean/std normalization
- Output: tensor with two dimensions, expected as yaw and pitch logits `[1, 90]`
- Post processor: `GazeDetectionParser`
- Supported Perception result: `Perception::YawPitch` in an `eyeYawPitch` layer
- Typical use: run on detected face crops and render gaze direction per face
