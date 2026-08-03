import assert from "node:assert/strict";
import test from "node:test";

import {resolveModelLabel} from "./model-labels.js";

const CASES = [
  ["YoloV11", "YOLOv11n", "Object detection", "ONNX"],
  ["YoloV11 HAILO", "YOLOv11n", "Object detection", "Hailo"],
  ["YoloV11n HAILO10", "YOLOv11n", "Object detection", "Hailo 10"],
  ["YoloV26", "YOLO26n", "Object detection", "ONNX"],
  ["yolox", "YOLOX Nano", "Object detection", "ExecuTorch"],
  ["Ultraface", "UltraFace", "Face detection", "ONNX"],
  ["SCRFD 2.5G HAILO", "SCRFD 2.5G", "Face detection", "Hailo 10"],
  ["CameraContact", "Nitec RS18", "Camera contact", "ONNX"],
  ["GazeDetection", "L2CS MobileGaze", "Gaze estimation", "ONNX"],
  ["ImageNet", "MobileNetV2", "Image classification", "ONNX"],
  ["ImageNet Hailo", "MobileNetV2", "Image classification", "Hailo"],
  ["PersonClassification", "MobileNetV1", "Person classification", "ONNX"],
  ["OsnetX025Reid", "OSNet x0.25", "Object re-identification", "ONNX"],
  ["OsnetX025Reid HAILO", "OSNet x0.25", "Object re-identification", "Hailo"],
  ["RepVGGA0PersonReID512 HAILO10", "RepVGG A0 512", "Person re-identification", "Hailo 10"],
  ["ArcFaceMobileFaceNet HAILO", "ArcFace MobileFaceNet", "Face embedding", "Hailo 10"],
  ["PadleOCR", "PaddleOCR", "Text detection", "ONNX"],
  ["Modnet", "MODNet", "Foreground segmentation", "ONNX"],
  ["MobileNetV3RVM", "RVM MobileNetV3", "Video matting", "ONNX"],
  ["CameraContactWithUltraface", "UltraFace + Nitec RS18", "Camera contact", "ONNX"],
  ["GazeDetectionWithUltraface", "UltraFace + L2CS MobileGaze", "Gaze estimation", "ONNX"],
  [
    "FaceEmbeddingWithScrfdHailo10",
    "SCRFD 2.5G + ArcFace MobileFaceNet",
    "Face embedding",
    "Hailo 10",
  ],
  ["FullTrackingOnnx", "YOLOv11n + OSNet x0.25", "Object tracking", "ONNX"],
  ["FullTrackingHailo8", "YOLOv11n + OSNet x0.25", "Object tracking", "Hailo 8"],
  ["FullTrackingHailo10", "YOLOv11n + RepVGG A0 512", "Object tracking", "Hailo 10"],
  [
    "FullTrackingExecutorch",
    "YOLOX Nano + OSNet x0.25",
    "Object tracking",
    "ExecuTorch + ONNX",
  ],
];

test("known OpChain names resolve to model-first UI labels", () => {
  for (const [rawName, modelName, task, runtime] of CASES) {
    const primaryLabel = `${modelName} - ${task}`;

    assert.deepEqual(resolveModelLabel(rawName), {
      modelName,
      task,
      runtime,
      primaryLabel,
      fullLabel: `${primaryLabel} (${runtime})`,
    });
  }
});

test("lookup is case-insensitive and ignores surrounding whitespace", () => {
  assert.deepEqual(resolveModelLabel("  yOlOv11  "), {
    modelName: "YOLOv11n",
    task: "Object detection",
    runtime: "ONNX",
    primaryLabel: "YOLOv11n - Object detection",
    fullLabel: "YOLOv11n - Object detection (ONNX)",
  });
});

test("correct PaddleOCR spelling is also accepted", () => {
  assert.equal(resolveModelLabel("PaddleOCR").modelName, "PaddleOCR");
});

test("custom OpChain names remain visible without a guessed task or runtime", () => {
  assert.deepEqual(resolveModelLabel("  My Custom Chain  "), {
    modelName: "My Custom Chain",
    task: "",
    runtime: "",
    primaryLabel: "My Custom Chain",
    fullLabel: "My Custom Chain",
  });
});

test("missing names use an explicit fallback", () => {
  assert.deepEqual(resolveModelLabel(null), {
    modelName: "Unknown model",
    task: "",
    runtime: "",
    primaryLabel: "Unknown model",
    fullLabel: "Unknown model",
  });
});
