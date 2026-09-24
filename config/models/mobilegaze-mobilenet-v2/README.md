# MobileGaze MobileNetV2 INT8

Face-level gaze estimator for yaw and pitch.

- Source: [Arm/mobilegaze-mobilenet-v2-int8-onnx](https://huggingface.co/Arm/mobilegaze-mobilenet-v2-int8-onnx)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW face crop, `[1, 3, 448, 448]`, `Float32`
- Preprocessing: ImageNet mean and standard-deviation normalization
- Output: two `[1, 90]` angle-logit tensors
- Postprocessor: `GazeDetectionParser` with `angleBinWidthDeg` set to `4`
- FrameResults payload: `PoseEstimationsT` with `contentType` set to `eyeYawPitch`
- Dependency: `humanFace` detections from UltraFace

The model-local OpChain processes existing face detections. The focused
pipeline uses `config/opchains/mobilegaze-mobilenet-v2/opchain.json` to run
UltraFace first. The model binary is downloaded from the pinned `hfDownload`
entry in `model.json`; it is not stored in the repository.

Run the complete focused preset inside the OPK container:

```bash
./tools/opk-menu mobilegaze-mobilenet-v2
```
