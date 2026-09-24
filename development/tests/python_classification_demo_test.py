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
    / "config/models/mobilenetv2/scripts/python_classification.py"
)
LABEL_PATH = SCRIPT_PATH.with_name("imagenet_labels.txt")
CPP_LABEL_PATH = REPOSITORY_ROOT / "development/common/opk/Labels.cpp"

sys.path.insert(
    0, str(REPOSITORY_ROOT / "generated/perception/python/src")
)

ProducerInfoT = importlib.import_module(
    "perception.fb.perception.metadata.ProducerInfo"
).ProducerInfoT


def python_script(callback):
    return callback


tensor_module = types.ModuleType("opk_python_ops")
tensor_module.Context = object
tensor_module.Tensor = object
tensor_module.python_script = python_script
sys.modules["opk_python_ops"] = tensor_module

guest_module = types.ModuleType("open_perception_kit.guest")
guest_module.Envelope = object
sys.modules["open_perception_kit.guest"] = guest_module

spec = importlib.util.spec_from_file_location("python_classification", SCRIPT_PATH)
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


class FakeContext:
    def __init__(self, producer_info):
        self.producer_info = producer_info


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
        self.context = FakeContext(
            ProducerInfoT(
                instanceId="opkinfer0/python-classifier",
                component="opk-python-ops/PythonScript",
                implementation=SCRIPT_PATH.name,
            )
        )

    def test_bundled_labels_match_cpp_table(self):
        self.assertEqual(tuple(LABEL_PATH.read_text(encoding="utf-8").splitlines()),
                         cpp_imagenet_labels())

    def test_top_classes_returns_only_sorted_top_k(self):
        logits = numpy.zeros((1, 1001), dtype=numpy.float32)
        expected_class_ids = [21, 34, 55, 89, 144]
        logits[0, expected_class_ids] = [9.0, 7.0, 5.0, 3.0, 1.0]

        top_classes = demo._top_classes(FakeTensor(logits))

        self.assertEqual([class_id for class_id, _ in top_classes], expected_class_ids)
        self.assertEqual(len(top_classes), demo.TOP_K)
        self.assertTrue(
            all(
                top_classes[index][1] > top_classes[index + 1][1]
                for index in range(len(top_classes) - 1)
            )
        )

    def test_process_emits_stateful_bottom_right_classification(self):
        logits = numpy.zeros((1, 1001), dtype=numpy.float32)
        logits[0, 2] = 5.0
        envelope = FakeEnvelope()
        tensor = FakeTensor(logits)

        demo.process(envelope, (tensor,), self.context)
        demo.process(envelope, (tensor,), self.context)

        payload = envelope.payloads[-1]
        candidates = payload.classifications[0].candidates
        self.assertEqual(payload.layer.contentType, "classification")
        self.assertEqual(payload.layer.compositingMode, "bottomRight")
        self.assertEqual(payload.layer.tags, "statefulTensorDemo;stableFrames=2")
        self.assertEqual(payload.layer.producer.instanceId, "opkinfer0/python-classifier")
        self.assertEqual(payload.layer.producer.component, "opk-python-ops/PythonScript")
        self.assertEqual(payload.layer.producer.implementation, SCRIPT_PATH.name)
        self.assertEqual(len(candidates), 5)
        self.assertEqual(candidates[0].classId, 2)
        self.assertEqual(candidates[0].text, "goldfish")

        logits[0, 2] = 0.0
        logits[0, 3] = 5.0
        demo.process(envelope, (tensor,), self.context)
        candidates = envelope.payloads[-1].classifications[0].candidates
        self.assertEqual(candidates[0].classId, 3)
        self.assertEqual(candidates[0].text, "great white shark")
        self.assertEqual(
            envelope.payloads[-1].layer.tags,
            "statefulTensorDemo;stableFrames=1",
        )


if __name__ == "__main__":
    unittest.main()
