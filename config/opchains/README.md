# Reusable OpChains

These dependency chains combine the published models for camera contact, gaze
estimation, and object re-identification. YOLO26n-320 is the default detector
because the replaced single-YOLO flow used a 320x320 input.

MobileGaze and NITEC provide ONNX and ExecuTorch/XNNPACK variants. Both
variants use UltraFace ONNX for face detection before running the selected
face-level backend.
