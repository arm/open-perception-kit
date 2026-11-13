# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0

import base64
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

EXPECTED_FACE_COUNT = 7
EXPECTED_CONTACT_COUNT = 7

spec = importlib.util.spec_from_file_location(
    "check_nightly_cascade", Path(__file__).with_name("check-nightly-cascade.py")
)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class CascadeResultTest(unittest.TestCase):
    def test_counts_only_face_and_contact_payloads(self):
        face = SimpleNamespace(layer=SimpleNamespace(contentType=b"humanFace"), detections=[1])
        contact = SimpleNamespace(
            layer=SimpleNamespace(contentType=b"cameraContact"), classifications=[1]
        )
        box_type, classification_type = object(), object()
        payloads = {box_type: [face], classification_type: [contact]}
        envelope = SimpleNamespace(for_each=lambda kind: payloads[kind])
        self.assertEqual(checker.cascade_counts(envelope, box_type, classification_type), (1, 1))
        contact.classifications = []
        self.assertEqual(checker.cascade_counts(envelope, box_type, classification_type), (1, 0))
        contact.classifications = [1]
        face.layer.contentType = b"genericObject"
        self.assertEqual(checker.cascade_counts(envelope, box_type, classification_type), (0, 1))

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
            BoxDetectionsT(
                layer=LayerInfoT(contentType="humanFace"),
                detections=[BoxDetectionT() for _ in range(EXPECTED_FACE_COUNT)],
            )
        )
        envelope.add(
            ClassificationsT(
                layer=LayerInfoT(contentType="cameraContact"),
                classifications=[ClassificationT() for _ in range(EXPECTED_CONTACT_COUNT)],
            )
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "results.ndjson"
            path.write_text(
                json.dumps({"frame_results_packet_b64": base64.b64encode(envelope.serialize()).decode()}),
                encoding="utf-8",
            )
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                checker.main(path)
            self.assertIn(
                f"{EXPECTED_FACE_COUNT} face detections, "
                f"{EXPECTED_CONTACT_COUNT} camera-contact classifications",
                output.getvalue(),
            )

            for faces, contacts in (
                (EXPECTED_FACE_COUNT - 1, EXPECTED_CONTACT_COUNT),
                (EXPECTED_FACE_COUNT, EXPECTED_CONTACT_COUNT - 1),
                (EXPECTED_FACE_COUNT + 1, EXPECTED_CONTACT_COUNT),
                (EXPECTED_FACE_COUNT, EXPECTED_CONTACT_COUNT + 1),
            ):
                with self.subTest(faces=faces, contacts=contacts):
                    envelope = Envelope()
                    envelope.add(
                        BoxDetectionsT(
                            layer=LayerInfoT(contentType="humanFace"),
                            detections=[BoxDetectionT() for _ in range(faces)],
                        )
                    )
                    envelope.add(
                        ClassificationsT(
                            layer=LayerInfoT(contentType="cameraContact"),
                            classifications=[ClassificationT() for _ in range(contacts)],
                        )
                    )
                    path.write_text(
                        json.dumps({"frame_results_packet_b64": base64.b64encode(envelope.serialize()).decode()}),
                        encoding="utf-8",
                    )
                    with contextlib.redirect_stdout(io.StringIO()):
                        with self.assertRaisesRegex(SystemExit, f"expected {EXPECTED_FACE_COUNT} face detections"):
                            checker.main(path)

            path.write_text("", encoding="utf-8")
            with self.assertRaisesRegex(SystemExit, f"expected {EXPECTED_FACE_COUNT} face detections"):
                checker.main(path)


if __name__ == "__main__":
    unittest.main()
