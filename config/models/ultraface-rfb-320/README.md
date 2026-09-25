# UltraFace RFB-320 INT8

Full-frame face detector optimized for ONNX Runtime.

- Source: [Arm/ultraface-rfb-320-onnx-raspberry](https://huggingface.co/Arm/ultraface-rfb-320-onnx-raspberry)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW image, `[1, 3, 240, 320]`
- Preprocessing: mean `127/255`, standard deviation `128/255`
- Output: dynamically discovered score and box tensors
- Postprocessor: `UltrafaceParser`, confidence threshold `0.1`, IoU threshold `0.3`
- FrameResults payload: `BoxDetectionsT` with `contentType` set to `humanFace`
- Typical use: first stage for NITEC and MobileGaze face-crop inference

The model binary is downloaded from the pinned `hfDownload` entry in
`model.json`; it is not stored in the repository.

Run the focused preset inside the OPK container:

```bash
./tools/opk-menu ultraface-rfb-320
```
