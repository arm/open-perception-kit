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
from open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements import PerformanceMeasurementsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.FrameRate import FrameRateT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ImageSpace import ImageSpaceT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.CoordinateUnits import CoordinateUnits  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.TrackTrace import TrackTraceT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Point2f import Point2fT  # noqa: E402


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
                box=BoundingBoxT(x=x, y=y, width=w, height=h,
                                 space=ImageSpaceT(frameId=1, width=100, height=100, extentWidth=100, extentHeight=100)),
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

    def test_normalized_crop_matches_full_frame_without_derivation_parent(self):
        expected = frame_results_with_boxes((2, 30.0, 30.0, 20.0, 10.0))
        actual = FrameResults()
        actual.add(frame_payload())
        payload = box_payload((7, 0.25, 0.5, 0.5, 0.5))
        box = payload.detections[0].box
        box.space = ImageSpaceT(frameId=99, width=100, height=100, originX=20, originY=20,
                                extentWidth=40, extentHeight=20)
        box.units = CoordinateUnits.Normalized
        payload.detections[0].object.parentId = 999  # Derivation does not determine geometry.
        actual.add(payload)
        ok, message = compare_frame_results_frame(self.args, frame_results_frame(expected), frame_results_frame(actual))
        self.assertTrue(ok, message)

    def test_missing_spatial_reference_is_an_error_not_a_parent_fallback(self):
        expected = frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0))
        actual = FrameResults()
        actual.add(frame_payload())
        payload = box_payload((2, 10.0, 10.0, 20.0, 20.0))
        payload.detections[0].box.space = None
        actual.add(payload)
        ok, message = compare_frame_results_frame(self.args, frame_results_frame(expected), frame_results_frame(actual))
        self.assertFalse(ok)
        self.assertIn("spatial reference", message)

    def test_resized_trace_uses_each_sources_dimensions(self):
        frames = []
        for scale in (1, 2):
            frame = FrameResults()
            frame.add(TrackTracesT(layer=layer("trackTrace", "tracker"), traces=[TrackTraceT(
                object=ObjectMetaT(id=7, parentId=999), trackId=1,
                points=[Point2fT(x=10 * scale, y=20 * scale), Point2fT(x=20 * scale, y=30 * scale)],
                space=ImageSpaceT(frameId=scale, width=100 * scale, height=100 * scale,
                                  extentWidth=100 * scale, extentHeight=100 * scale))]))
            frames.append(frame_results_frame(frame))
        ok, message = compare_frame_results_frame(self.args, *frames)
        self.assertTrue(ok, message)

    def test_performance_measurements_remain_outside_comparison(self) -> None:
        expected = frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0))
        actual = frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0))
        measurements = PerformanceMeasurementsT()
        measurements.frameRate = FrameRateT()
        measurements.frameRate.fps = 23.5
        actual.add(measurements)
        ok, message = compare_frame_results_frame(self.args, frame_results_frame(expected), frame_results_frame(actual))
        self.assertTrue(ok, message)

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
