# YOLO26s INT8 (640x640)

Small full-frame object detector optimized for ONNX Runtime on Raspberry Pi 5.

- Source: [Arm/yolo26s-640-int8-onnx-raspberrypi5](https://huggingface.co/Arm/yolo26s-640-int8-onnx-raspberrypi5)
- Backend: ONNX Runtime on CPU
- Input: RGB NCHW image, `[1, 3, 640, 640]`, with aspect-ratio-preserving letterboxing
- Output: decoded corner coordinates, score, and COCO class ID
- Postprocessor: `YoloParser` using `cornerScoreClass`; model output already includes NMS
- FrameResults payload: `BoxDetectionsT` with `contentType` set to `genericObject`

The model binary is downloaded from the pinned `hfDownload` entry in
`model.json`; it is not stored in the repository.

```bash
./tools/opk-menu yolo26s-640
```
