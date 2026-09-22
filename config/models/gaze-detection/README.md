# Gaze Detection

Face-level gaze estimator for yaw and pitch.

- Backend: ONNX
- Input: NCHW crop, `[1, 3, 448, 448]`, ImageNet mean/std normalization
- Output: tensor with two dimensions, expected as yaw and pitch logits `[1, 90]`
- Post processor: `GazeDetectionParser` with `angleBinWidthDeg: 4`, converting each
  softmax expectation to `expectedBin * 4 - 180` degrees
- Supported FrameResults payload: `PoseEstimationsT` with `content_type` set to `eyeYawPitch`
- Typical use: run on detected face crops and render gaze direction per face
