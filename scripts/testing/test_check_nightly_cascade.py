# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0

import base64
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location(
    "check_nightly_cascade", Path(__file__).with_name("check-nightly-cascade.py")
)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class CascadeResultTest(unittest.TestCase):
    def test_requires_nonempty_face_and_contact_payloads(self):
        face = SimpleNamespace(layer=SimpleNamespace(contentType=b"humanFace"), detections=[1])
        contact = SimpleNamespace(
            layer=SimpleNamespace(contentType=b"cameraContact"), classifications=[1]
        )
        box_type, classification_type = object(), object()
        payloads = {box_type: [face], classification_type: [contact]}
        envelope = SimpleNamespace(for_each=lambda kind: payloads[kind])
        self.assertTrue(checker.has_cascade_result(envelope, box_type, classification_type))
        contact.classifications = []
        self.assertFalse(checker.has_cascade_result(envelope, box_type, classification_type))
        contact.classifications = [1]
        face.layer.contentType = b"genericObject"
        self.assertFalse(checker.has_cascade_result(envelope, box_type, classification_type))

    def test_reads_serialized_packet(self):
        try:
            from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT
            from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
            from open_perception_kit.fb.open_perception_kit.metadata.Classification import ClassificationT
            from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT
            from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT
            from open_perception_kit.sdk import Envelope
        except ModuleNotFoundError as error:
            self.skipTest(str(error))

        envelope = Envelope()
        envelope.add(
            BoxDetectionsT(layer=LayerInfoT(contentType="humanFace"), detections=[BoxDetectionT()])
        )
        envelope.add(
            ClassificationsT(
                layer=LayerInfoT(contentType="cameraContact"),
                classifications=[ClassificationT()],
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.ndjson"
            path.write_text(
                json.dumps({"frame_results_packet_b64": base64.b64encode(envelope.serialize()).decode()}),
                encoding="utf-8",
            )
            checker.main(path)


if __name__ == "__main__":
    unittest.main()
