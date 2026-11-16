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
from open_perception_kit.fb.open_perception_kit.metadata.ProducerInfo import ProducerInfoT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.VideoFrameContext import VideoFrameContextT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements import PerformanceMeasurementsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.FrameRate import FrameRateT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ImageSpace import ImageSpaceT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.CoordinateUnits import CoordinateUnits  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.TrackTrace import TrackTraceT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Point2f import Point2fT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.Classification import ClassificationT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ClassificationCandidate import ClassificationCandidateT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ClassificationKind import ClassificationKind  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.CameraContacts import CameraContactsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.CameraContact import CameraContactT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.CameraContactResult import CameraContactResultT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PersonPresences import PersonPresencesT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PersonPresence import PersonPresenceT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.PersonPresenceResult import PersonPresenceResultT  # noqa: E402


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

    def test_empty_candidates_respect_subject_kind_and_label_set(self):
        def make(kind=ClassificationKind.Categorical, label_set="test:labels", subject=1, candidates=()):
            result = frame_results_with_boxes()
            result.add(ClassificationsT(layer=LayerInfoT(model="empty-classifier"), classifications=[
                ClassificationT(subjectId=subject, kind=kind, labelSet=label_set, candidates=list(candidates))]))
            return frame_results_frame(FrameResults(result.serialize()))

        categorical = make()
        text = make(kind=ClassificationKind.RecognizedText, label_set="")
        for value in (categorical, text):
            self.assertTrue(compare_frame_results_frame(self.args, value, value)[0])
        for other in (make(label_set="other"), text,
                      make(candidates=[ClassificationCandidateT(classId=0, confidence=.9)])):
            self.assertFalse(compare_frame_results_frame(self.args, categorical, other)[0])
            self.assertFalse(compare_frame_results_frame(self.args, other, categorical)[0])
        for invalid in (make(label_set=""), make(kind=99), make(subject=999),
                        make(kind=ClassificationKind.RecognizedText)):
            self.assertFalse(compare_frame_results_frame(self.args, invalid, invalid)[0])

    def test_classification_uses_label_set_and_class_id(self):
        def make(label_set, class_id, text, kind=ClassificationKind.Categorical, confidence=.9):
            result = frame_results_with_boxes()
            result.add(ClassificationsT(layer=layer("classification", "infer0"), classifications=[
                ClassificationT(object=ObjectMetaT(id=10, parentId=999), subjectId=1, labelSet=label_set, kind=kind,
                                candidates=[ClassificationCandidateT(classId=class_id, text=text, confidence=confidence)])]))
            return frame_results_frame(FrameResults(result.serialize()))

        expected = make("set-a", 7, "label")
        for actual, matches in [(make("set-a", 8, "label"), False),
                                (make("set-b", 7, "label"), False),
                                (make("set-a", 7, "renamed"), True),
                                (make("", 7, "label"), False)]:
            ok, message = compare_frame_results_frame(self.args, expected, actual)
            self.assertEqual(ok, matches, message)
        for text, matches in [("Hello\n世界", True), ("different", False)]:
            ok, message = compare_frame_results_frame(self.args, make("", None, "Hello\n世界", ClassificationKind.RecognizedText),
                                                      make("", None, text, ClassificationKind.RecognizedText))
            self.assertEqual(ok, matches, message)
        for actual, matches in [(make("set-a", 0, "renamed"), True),
                                (make("set-a", None, "zero"), False),
                                (make("", None, "zero"), False)]:
            ok, message = compare_frame_results_frame(self.args, make("set-a", 0, "zero"), actual)
            self.assertEqual(ok, matches, message)
        ok, message = compare_frame_results_frame(self.args, make("", 7, "label"), make("", 7, "label"))
        self.assertFalse(ok, message)
        for kind in (ClassificationKind.Categorical, ClassificationKind.Unspecified):
            unknown = make("", None, "same text", kind)
            self.assertFalse(compare_frame_results_frame(self.args, unknown, unknown)[0])
        bad = make("set-a", 7, "label", confidence=float("nan"))
        self.assertFalse(compare_frame_results_frame(self.args, expected, bad)[0])

    def test_contact_outcomes_do_not_depend_on_diagnostic_model_indices(self):
        def make(outcome, index=None, selected=True, confidence=.5):
            result = frame_results_with_boxes((2, 10, 10, 20, 20))
            result.add(CameraContactsT(layer=layer("cameraContact", "infer0"), contacts=[
                CameraContactT(object=ObjectMetaT(id=10), subjectId=2, result=CameraContactResultT(
                    inContact=outcome, modelClassIndex=index, confidence=confidence) if selected else None)]))
            return frame_results_frame(FrameResults(result.serialize()))

        for actual, matches in [(make(True, 0), True), (make(True, 99), True),
                                (make(False, 1), False), (make(True, selected=False), False),
                                (make(True, confidence=float("nan")), False)]:
            ok, message = compare_frame_results_frame(self.args, make(True, 1), actual)
            self.assertEqual(ok, matches, message)

    def test_presence_compares_decisions_without_guessing_probability_units(self):
        def make(outcome, yes=None, no=None):
            result = frame_results_with_boxes()
            result.add(PersonPresencesT(layer=layer("personClassification", "infer0"), presences=[
                PersonPresenceT(object=ObjectMetaT(id=10), subjectId=1,
                                result=PersonPresenceResultT(personPresent=outcome, yesScore=yes, noScore=no))]))
            return frame_results_frame(FrameResults(result.serialize()))

        for actual, matches in [(make(True, 2, -1), True), (make(False, .2, .8), False),
                                (make(True), True), (make(True, .8, float("nan")), False),
                                (make(True, .8, None), False)]:
            ok, message = compare_frame_results_frame(self.args, make(True, .8, .2), actual)
            self.assertEqual(ok, matches, message)

    def test_domain_results_cannot_swap_subject_associations(self):
        def make(kind, assignments, boxes=((2, 10, 10, 20, 20), (3, 60, 10, 20, 20))):
            result = frame_results_with_boxes(*boxes)
            objects = [ObjectMetaT(id=100+i, parentId=2) for i in range(len(assignments))]
            if kind == "classification":
                result.add(ClassificationsT(layer=layer("custom", "secondary"), classifications=[
                    ClassificationT(object=obj, subjectId=subject, kind=ClassificationKind.Categorical, labelSet="test:labels",
                                    candidates=[ClassificationCandidateT(classId=value)])
                    for obj, (subject, value) in zip(objects, assignments)]))
            elif kind == "contact":
                result.add(CameraContactsT(layer=layer("custom", "secondary"), contacts=[
                    CameraContactT(object=obj, subjectId=subject, result=CameraContactResultT(inContact=bool(value)))
                    for obj, (subject, value) in zip(objects, assignments)]))
            else:
                result.add(PersonPresencesT(layer=layer("custom", "secondary"), presences=[
                    PersonPresenceT(object=obj, subjectId=subject,
                                    result=PersonPresenceResultT(personPresent=bool(value)))
                    for obj, (subject, value) in zip(objects, assignments)]))
            return frame_results_frame(FrameResults(result.serialize()))
        for kind in ("classification", "contact", "presence"):
            expected = make(kind, [(2, 1), (3, 0)])
            for actual, matches in [(make(kind, [(3, 0), (2, 1)]), True),
                                    (make(kind, [(2, 0), (3, 1)]), False),
                                    (make(kind, [(None, 1), (3, 0)]), False),
                                    (make(kind, [(20, 1), (30, 0)], ((20, 10, 10, 20, 20), (30, 60, 10, 20, 20))), True)]:
                ok, message = compare_frame_results_frame(self.args, expected, actual)
                self.assertEqual(ok, matches, f"{kind}: {message}")
            duplicate = make(kind, [(2, 1)], ((2, 10, 10, 20, 20), (2, 60, 10, 20, 20)))
            ok, message = compare_frame_results_frame(self.args, duplicate, duplicate)
            self.assertFalse(ok)
            self.assertIn("ambiguous", message)

    def test_matches_equivalent_frame_results_frames(self) -> None:
        gt_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))
        out_frame = frame_results_frame(frame_results_with_boxes((2, 10.0, 10.0, 20.0, 20.0)))

        ok, msg = compare_frame_results_frame(self.args, gt_frame, out_frame)

        self.assertTrue(ok, msg)

    def test_classifications_cannot_swap_producers(self):
        def make(swapped=False):
            result = frame_results_with_boxes()
            for index, instance in enumerate(("a", "b")):
                source = layer("classification", "infer0")
                source.producer = ProducerInfoT(component="classifier", instanceId=instance)
                result.add(ClassificationsT(layer=source, classifications=[ClassificationT(
                    object=ObjectMetaT(id=10 + index), subjectId=1,
                    kind=ClassificationKind.Categorical, labelSet="test:labels",
                    candidates=[ClassificationCandidateT(classId=1-index if swapped else index)])]))
            return frame_results_frame(FrameResults(result.serialize()))
        self.assertTrue(compare_frame_results_frame(self.args, make(), make())[0])
        self.assertFalse(compare_frame_results_frame(self.args, make(), make(True))[0])

    def test_frame_subjects_require_unambiguous_source_correspondence(self):
        def make(distinct_sources, swapped=False, offset=0):
            result = FrameResults()
            for index in range(2):
                frame = frame_payload()
                frame.video.object.id = 1 + index + offset
                frame.video.originalWidth = 100 * (index + 1)
                if distinct_sources:
                    frame.layer.producer = ProducerInfoT(component="camera", instanceId=str(index))
                result.add(frame)
            result.add(ClassificationsT(layer=layer("classification", "infer0"), classifications=[
                ClassificationT(object=ObjectMetaT(id=100 + index), subjectId=1 + index + offset,
                                kind=ClassificationKind.Categorical, labelSet="test:labels",
                                candidates=[ClassificationCandidateT(classId=1-index if swapped else index)])
                for index in range(2)]))
            return frame_results_frame(FrameResults(result.serialize()))
        for distinct_sources in (False, True):
            for swapped in (False, True):
                ok, message = compare_frame_results_frame(self.args, make(distinct_sources),
                                                          make(distinct_sources, swapped=swapped, offset=10))
                self.assertEqual(ok, distinct_sources and not swapped, message)

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
