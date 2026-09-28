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
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools" / "plumber"))

from plumber.frame_results_compare import compare_frame_results_frame  # noqa: E402
from plumber.frame_results_decode import FrameResultsFrame  # noqa: E402
from plumber.frame_results_sdk import FrameResults  # noqa: E402

from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.VideoFrameContext import VideoFrameContextT  # noqa: E402


def layer(content_type: str, infer_element_id: str) -> LayerInfoT:
    return LayerInfoT(
        model="model-a",
        inferElementId=infer_element_id,
        contentType=content_type,
        engine="test",
    )


def frame_payload() -> FrameContextT:
    return FrameContextT(
        layer=layer("frameContext", "camera"),
        video=VideoFrameContextT(
            object=ObjectMetaT(id=1),
            originalWidth=100,
            originalHeight=100,
        ),
    )


def box_payload(*boxes: tuple[int, float, float, float, float]) -> BoxDetectionsT:
    return BoxDetectionsT(
        layer=layer("genericObject", "infer0"),
        detections=[
            BoxDetectionT(
                object=ObjectMetaT(id=object_id, parentId=1),
                box=BoundingBoxT(x=x, y=y, width=w, height=h),
                confidence=0.9,
                classId=1,
                text="object",
            )
            for object_id, x, y, w, h in boxes
        ],
    )


def frame_results_with_boxes(*boxes: tuple[int, float, float, float, float]) -> FrameResults:
    frame_results = FrameResults()
    frame_results.add(frame_payload())
    if boxes:
        frame_results.add(box_payload(*boxes))
    return frame_results


def frame_results_frame(frame_results: FrameResults) -> FrameResultsFrame:
    return FrameResultsFrame(
        frame_counter=0,
        ndjson_record={},
        frame_results=frame_results,
    )


class FrameResultsCompareTests(unittest.TestCase):
    def setUp(self) -> None:
        self.args = SimpleNamespace(verbose=False)

    def test_matches_equivalent_frame_results_frames(self) -> None:
        gt_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))
        out_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))

        ok, msg = compare_frame_results_frame(self.args, gt_frame, out_frame)

        self.assertTrue(ok, msg)

    def test_reports_missing_payload(self) -> None:
        gt_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))
        out_frame = frame_results_frame(frame_results_with_boxes())

        ok, msg = compare_frame_results_frame(self.args, gt_frame, out_frame)

        self.assertFalse(ok)
        self.assertIn("payload mismatch", msg)

    def test_reports_item_count_mismatch(self) -> None:
        gt_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))
        out_frame = frame_results_frame(
            frame_results_with_boxes(
                (2, 10.0, 10.0, 20.0, 20.0),
                (3, 40.0, 40.0, 20.0, 20.0),
            )
        )

        ok, msg = compare_frame_results_frame(self.args, gt_frame, out_frame)

        self.assertFalse(ok)
        self.assertIn("count mismatch", msg)

    def test_reports_distance_failure(self) -> None:
        gt_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))
        out_frame = frame_results_frame(frame_results_with_boxes((2, 70.0, 70.0, 20.0, 20.0)))

        ok, msg = compare_frame_results_frame(self.args, gt_frame, out_frame)

        self.assertFalse(ok)
        self.assertIn("distance too large", msg)


if __name__ == "__main__":
    unittest.main()
