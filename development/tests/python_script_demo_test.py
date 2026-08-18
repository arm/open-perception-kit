################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import importlib.util
import json
from pathlib import Path
import re
import sys
import types
import unittest

import numpy


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_PATH = (
    REPOSITORY_ROOT
    / "config/models/mobilenetv2/scripts/tensor_metrics_overlay.py"
)
LABEL_PATH = SCRIPT_PATH.with_name("imagenet_labels.txt")
CPP_LABEL_PATH = REPOSITORY_ROOT / "development/common/pek/Labels.cpp"

sys.path.insert(
    0, str(REPOSITORY_ROOT / "generated/perception/python/src")
)

tensor_module = types.ModuleType("pek_python_ops")
tensor_module.Tensor = object
sys.modules["pek_python_ops"] = tensor_module

guest_module = types.ModuleType("perception.guest")
guest_module.Envelope = object
sys.modules["perception.guest"] = guest_module

spec = importlib.util.spec_from_file_location("tensor_metrics_overlay", SCRIPT_PATH)
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


class FakeTensor:
    def __init__(self, logits):
        self.array = logits
        self.index = 0
        self.name = "logits"
        self.quantized = False
        self.scale = 1.0
        self.zero_point = 0.0


class FakeEnvelope:
    def __init__(self):
        self.payloads = []

    def add(self, payload):
        self.payloads.append(payload)


def cpp_imagenet_labels():
    source = CPP_LABEL_PATH.read_text(encoding="utf-8")
    start = source.index("imageNetLabels = {")
    end = source.index("};", start)
    entries = re.findall(r'"((?:\\.|[^"\\])*)"', source[start:end])
    return tuple(json.loads(f'"{entry}"') for entry in entries)


class PythonScriptDemoTest(unittest.TestCase):
    def setUp(self):
        demo.previous_class_id = None
        demo.stable_frame_count = 0

    def test_bundled_labels_match_cpp_table(self):
        self.assertEqual(tuple(LABEL_PATH.read_text(encoding="utf-8").splitlines()),
                         cpp_imagenet_labels())

    def test_process_emits_stateful_bottom_right_classification(self):
        logits = numpy.zeros((1, 1001), dtype=numpy.float32)
        logits[0, 2] = 5.0
        envelope = FakeEnvelope()
        tensor = FakeTensor(logits)

        demo.process(envelope, (tensor,))
        demo.process(envelope, (tensor,))

        payload = envelope.payloads[-1]
        candidates = payload.classifications[0].candidates
        self.assertEqual(payload.layer.contentType, "classification")
        self.assertEqual(payload.layer.compositingMode, "bottomRight")
        self.assertEqual(payload.layer.tags, "statefulTensorDemo;stableFrames=2")
        self.assertEqual(len(candidates), 5)
        self.assertEqual(candidates[0].classId, 2)
        self.assertEqual(candidates[0].text, "goldfish")

        logits[0, 2] = 0.0
        logits[0, 3] = 5.0
        demo.process(envelope, (tensor,))
        candidates = envelope.payloads[-1].classifications[0].candidates
        self.assertEqual(candidates[0].classId, 3)
        self.assertEqual(candidates[0].text, "great white shark")
        self.assertEqual(
            envelope.payloads[-1].layer.tags,
            "statefulTensorDemo;stableFrames=1",
        )


if __name__ == "__main__":
    unittest.main()
