const MODEL_LABELS = new Map([
  ["arcfacemobilefacenet hailo", label("ArcFace MobileFaceNet", "Face embedding", "Hailo 10")],
  ["cameracontact", label("Nitec RS18", "Camera contact", "ONNX")],
  ["gazedetection", label("L2CS MobileGaze", "Gaze estimation", "ONNX")],
  ["imagenet", label("MobileNetV2", "Image classification", "ONNX")],
  ["imagenet hailo", label("MobileNetV2", "Image classification", "Hailo")],
  ["modnet", label("MODNet", "Foreground segmentation", "ONNX")],
  ["osnetx025reid", label("OSNet x0.25", "Object re-identification", "ONNX")],
  ["osnetx025reid hailo", label("OSNet x0.25", "Object re-identification", "Hailo")],
  ["padleocr", label("PaddleOCR", "Text detection", "ONNX")],
  ["paddleocr", label("PaddleOCR", "Text detection", "ONNX")],
  ["personclassification", label("MobileNetV1", "Person classification", "ONNX")],
  [
    "repvgga0personreid512 hailo10",
    label("RepVGG A0 512", "Person re-identification", "Hailo 10"),
  ],
  ["mobilenetv3rvm", label("RVM MobileNetV3", "Video matting", "ONNX")],
  ["scrfd 2.5g hailo", label("SCRFD 2.5G", "Face detection", "Hailo 10")],
  ["ultraface", label("UltraFace", "Face detection", "ONNX")],
  ["yolov26", label("YOLO26n", "Object detection", "ONNX")],
  ["yolov11", label("YOLOv11n", "Object detection", "ONNX")],
  ["yolov11 hailo", label("YOLOv11n", "Object detection", "Hailo")],
  ["yolov11n hailo10", label("YOLOv11n", "Object detection", "Hailo 10")],
  ["yolox", label("YOLOX Nano", "Object detection", "ExecuTorch")],
  [
    "cameracontactwithultraface",
    label("UltraFace + Nitec RS18", "Camera contact", "ONNX"),
  ],
  [
    "faceembeddingwithscrfdhailo10",
    label("SCRFD 2.5G + ArcFace MobileFaceNet", "Face embedding", "Hailo 10"),
  ],
  [
    "gazedetectionwithultraface",
    label("UltraFace + L2CS MobileGaze", "Gaze estimation", "ONNX"),
  ],
  [
    "fulltrackingexecutorch",
    label("YOLOX Nano + OSNet x0.25", "Object tracking", "ExecuTorch + ONNX"),
  ],
  [
    "fulltrackinghailo10",
    label("YOLOv11n + RepVGG A0 512", "Object tracking", "Hailo 10"),
  ],
  [
    "fulltrackinghailo8",
    label("YOLOv11n + OSNet x0.25", "Object tracking", "Hailo 8"),
  ],
  [
    "fulltrackingonnx",
    label("YOLOv11n + OSNet x0.25", "Object tracking", "ONNX"),
  ],
]);

function label(modelName, task, runtime) {
  return Object.freeze({modelName, task, runtime});
}

function normalizeModelName(rawName) {
  return String(rawName ?? "").trim().toLowerCase();
}

export function resolveModelLabel(rawName) {
  const rawLabel = String(rawName ?? "").trim();
  const configured = MODEL_LABELS.get(normalizeModelName(rawName));
  const modelName = configured?.modelName || rawLabel || "Unknown model";
  const task = configured?.task || "";
  const runtime = configured?.runtime || "";
  const primaryLabel = task ? `${modelName} - ${task}` : modelName;

  return {
    modelName,
    task,
    runtime,
    primaryLabel,
    fullLabel: runtime ? `${primaryLabel} (${runtime})` : primaryLabel,
  };
}
