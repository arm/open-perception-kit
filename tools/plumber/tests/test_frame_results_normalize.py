# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import annotations

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "plumber"))

from plumber.frame_results_normalize import PayloadKey, normalize_frame_results  # noqa: E402
from plumber.frame_results_sdk import FrameResults  # noqa: E402

from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Classification import ClassificationT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ClassificationCandidate import (  # noqa: E402
    ClassificationCandidateT,
)
from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbedding import ObjectEmbeddingT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbeddings import ObjectEmbeddingsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Point2f import Point2fT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PoseEstimation import PoseEstimationT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PoseEstimations import PoseEstimationsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ProducerInfo import ProducerInfoT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.TrackTrace import TrackTraceT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.VideoFrameContext import VideoFrameContextT  # noqa: E402


def layer(content_type: str, infer_element_id: str = "infer0") -> LayerInfoT:
    return LayerInfoT(
        model="model-a",
        inferElementId=infer_element_id,
        contentType=content_type,
        engine="test",
        producer=ProducerInfoT(
            instanceId=f"{infer_element_id}/fixture",
            component="test/Producer",
            implementation="FixtureProducer",
        ),
    )


class FrameResultsNormalizeTests(unittest.TestCase):
    def test_turn_assessments_roundtrip_and_compare(self):
        from open_perception_kit.fb.open_perception_kit.metadata.TurnDetection import TurnDetectionT
        from open_perception_kit.fb.open_perception_kit.metadata.TurnDetections import TurnDetectionsT
        from plumber.frame_results_compare import DISTANCE_SPECS

        results = FrameResults()
        results.add(TurnDetectionsT(layer=layer("turnDetection"), detections=[
            TurnDetectionT(object=ObjectMetaT(id=201), turnProbability=0.8,
                           turnComplete=True, windowStartSample=0, windowEndSample=16000,
                           windowStartTsNs=0, windowEndTsNs=1_000_000_000, discontinuity=True),
            TurnDetectionT(object=ObjectMetaT(id=202), turnProbability=0.2,
                           windowStartSample=512, windowEndSample=128512),
        ]))
        snapshot = normalize_frame_results(FrameResults(results.serialize()))
        key = PayloadKey("open_perception_kit.metadata.TurnDetections", "infer0", "turnDetection", "model-a")
        items = snapshot.payloads[key].items
        self.assertEqual(len(items), 2)
        self.assertEqual(items[0]["item_type"], "TurnDetection")
        data = items[0]["data"]
        self.assertAlmostEqual(data["turnProbability"], 0.8)
        self.assertTrue(data["turnComplete"])
        self.assertEqual(data["windowStartSample"], 0)
        self.assertEqual(data["windowEndSample"], 16000)
        self.assertEqual(data["windowStartTsNs"], 0)
        self.assertEqual(data["windowEndTsNs"], 1_000_000_000)
        self.assertTrue(data["discontinuity"])
        self.assertFalse(items[1]["data"]["turnComplete"])
        self.assertIsNone(items[1]["data"]["windowStartTsNs"])
        self.assertIsNone(items[1]["data"]["windowEndTsNs"])
        spec = DISTANCE_SPECS["TurnDetection"]
        self.assertEqual(spec.func(items[0], items[0], {}, {}), 0)
        for field, changed in (("turnComplete", False), ("windowStartSample", 1),
                               ("windowEndSample", 16001), ("windowStartTsNs", None),
                               ("windowEndTsNs", None), ("discontinuity", False)):
            with self.subTest(field=field):
                altered = {"data": {**data, field: changed}}
                self.assertEqual(spec.func(items[0], altered, {}, {}), 1)
        altered = {"data": {**data, "turnProbability": data["turnProbability"] + 0.005}}
        self.assertLess(spec.func(items[0], altered, {}, {}), spec.threshold)

    def test_voice_activity_roundtrip_preserves_unknown_and_zero_timestamps(self):
        from open_perception_kit.fb.open_perception_kit.metadata.VoiceActivityDetection import VoiceActivityDetectionT
        from open_perception_kit.fb.open_perception_kit.metadata.VoiceActivityDetections import VoiceActivityDetectionsT
        results = FrameResults()
        results.add(VoiceActivityDetectionsT(layer=layer("voiceActivity"), detections=[
            VoiceActivityDetectionT(object=ObjectMetaT(id=101), speechProbability=0.8,
                                    speechActive=True, windowStartSample=0, windowEndSample=320,
                                    windowStartTsNs=0, windowEndTsNs=20_000_000, discontinuity=True),
            VoiceActivityDetectionT(object=ObjectMetaT(id=102), speechProbability=0.2,
                                    windowStartSample=320, windowEndSample=640),
        ]))
        snapshot = normalize_frame_results(FrameResults(results.serialize()))
        key = PayloadKey("open_perception_kit.metadata.VoiceActivityDetections", "infer0", "voiceActivity", "model-a")
        items = snapshot.payloads[key].items
        self.assertEqual(len(items), 2)
        self.assertEqual(items[0]["data"]["windowStartTsNs"], 0)
        self.assertEqual(items[0]["data"]["windowEndTsNs"], 20_000_000)
        self.assertTrue(items[0]["data"]["speechActive"])
        self.assertIsNone(items[1]["data"]["windowStartTsNs"])
        from plumber.distance_classification import distance_voice_activity
        self.assertEqual(distance_voice_activity(items[0], items[0], {}, {}), 0)
        self.assertEqual(distance_voice_activity(items[0], items[1], {}, {}), 1)

    def test_normalizes_frame_and_box_detection_payloads(self) -> None:
        frame_results = FrameResults()
        frame_results.add(
            FrameContextT(
                layer=layer("frameContext", "camera"),
                video=VideoFrameContextT(
                    object=ObjectMetaT(id=1),
                    originalWidth=640,
                    originalHeight=480,
                ),
            )
        )
        frame_results.add(
            BoxDetectionsT(
                layer=layer("humanFace"),
                detections=[
                    BoxDetectionT(
                        object=ObjectMetaT(id=2, parentId=1),
                        box=BoundingBoxT(x=10.0, y=20.0, width=30.0, height=40.0),
                        confidence=0.9,
                        classId=3,
                        text="face",
                    )
                ],
            )
        )

        snapshot = normalize_frame_results(frame_results, frame_counter=42)

        frame_key = PayloadKey(
            "open_perception_kit.metadata.FrameContext",
            infer_element_id="camera",
            content_type="frameContext",
            model="model-a",
        )
        box_key = PayloadKey(
            "open_perception_kit.metadata.BoxDetections",
            infer_element_id="infer0",
            content_type="humanFace",
            model="model-a",
        )

        self.assertEqual(snapshot.frame_counter, 42)
        self.assertIn(frame_key, snapshot.payloads)
        self.assertIn(box_key, snapshot.payloads)
        self.assertIn(1, snapshot.object_index)
        self.assertIn(2, snapshot.object_index)

        frame_item = snapshot.payloads[frame_key].items[0]
        box_item = snapshot.payloads[box_key].items[0]
        self.assertEqual(frame_item["item_type"], "VideoFrame")
        self.assertEqual(frame_item["data"]["originalWidth"], 640)
        self.assertEqual(box_item["item_type"], "BoxDetection")
        self.assertEqual(box_item["data"]["parentId"], 1)
        self.assertEqual(box_item["data"]["text"], "face")
        self.assertEqual(
            snapshot.payloads[box_key].layer_info["producer"],
            {
                "instanceId": "infer0/fixture",
                "component": "test/Producer",
                "implementation": "FixtureProducer",
            },
        )

    def test_normalizes_classification_pose_embedding_and_trace_payloads(self) -> None:
        frame_results = FrameResults()
        frame_results.add(
            ClassificationsT(
                layer=layer("classification"),
                classifications=[
                    ClassificationT(
                        object=ObjectMetaT(id=3, parentId=1),
                        candidates=[
                            ClassificationCandidateT(
                                confidence=0.8,
                                classId=9,
                                text="label",
                            )
                        ],
                    )
                ],
            )
        )
        frame_results.add(
            PoseEstimationsT(
                layer=layer("eyeYawPitch"),
                poses=[
                    PoseEstimationT(
                        object=ObjectMetaT(id=4, parentId=2),
                        confidence=0.7,
                        yaw=11.0,
                        pitch=-4.0,
                    )
                ],
            )
        )
        frame_results.add(
            ObjectEmbeddingsT(
                layer=layer("objectEmbedding"),
                embeddings=[
                    ObjectEmbeddingT(
                        object=ObjectMetaT(id=5, parentId=2),
                        values=[0.1, 0.2, 0.3],
                    )
                ],
            )
        )
        frame_results.add(
            TrackTracesT(
                layer=layer("trackTrace"),
                traces=[
                    TrackTraceT(
                        object=ObjectMetaT(id=6),
                        trackId=77,
                        points=[Point2fT(x=1.0, y=2.0), Point2fT(x=3.0, y=4.0)],
                    )
                ],
            )
        )

        snapshot = normalize_frame_results(frame_results)
        item_types = {
            item["item_type"]
            for payload in snapshot.payloads.values()
            for item in payload.items
        }

        self.assertEqual(
            item_types,
            {"Classification", "PoseEstimation", "ObjectEmbedding", "TrackTrace"},
        )
        self.assertEqual(len(snapshot.object_index), 4)


if __name__ == "__main__":
    unittest.main()
