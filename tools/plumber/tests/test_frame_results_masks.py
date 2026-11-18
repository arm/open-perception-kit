# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
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

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks import InstanceSegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMask import InstanceSegmentationMaskT
from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks import SemanticSegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMask import SemanticSegmentationMaskT
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import SegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMask import SegmentationMaskT
from open_perception_kit.fb.open_perception_kit.metadata.OpacityMeaning import OpacityMeaning
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT
from open_perception_kit.fb.open_perception_kit.metadata.ImageSpace import ImageSpaceT
from open_perception_kit.fb.open_perception_kit.metadata.BitmapEncoding import BitmapEncoding
from open_perception_kit.fb.open_perception_kit.metadata.BitmapData import BitmapDataT
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/plumber"))
from plumber.frame_results_sdk import FrameResults  # noqa: E402
from plumber.frame_results_decode import FrameResultsFrame  # noqa: E402
from plumber.frame_results_compare import compare_frame_results_frame  # noqa: E402


ROOTS = {"alpha": SegmentationMasksT, "semantic": SemanticSegmentationMasksT, "instance": InstanceSegmentationMasksT}


def mask(kind, pixels=(0, 1, 0, 1), encoding=BitmapEncoding.Uint8):
    width = 1 if encoding == BitmapEncoding.Uint8 else 4
    bitmap = BitmapDataT(width=2, height=2, encoding=encoding,
                         pixels=b"".join(value.to_bytes(width, "little") for value in pixels),
                         space=ImageSpaceT(frameId=7, width=100, height=100, extentWidth=100, extentHeight=100))
    common = {"object": ObjectMetaT(id=10, parentId=999), "bitmap": bitmap}
    if kind == "alpha":
        return SegmentationMaskT(**common, meaning=OpacityMeaning.BackgroundOpacity)
    if kind == "semantic":
        return SemanticSegmentationMaskT(**common, labelSet="test:labels")
    return InstanceSegmentationMaskT(**common, labelSet="test:labels", classId=0, confidence=0)


def frame(kind, *masks, mode="arbitrary display setting"):
    result = FrameResults()
    result.add(ROOTS[kind](layer=LayerInfoT(model="model", compositingMode=mode), masks=list(masks)))
    return FrameResultsFrame(frame_counter=0, ndjson_record={}, frame_results=FrameResults(result.serialize()))


class MaskComparisonTests(unittest.TestCase):
    def compare(self, expected, actual, matches):
        ok, message = compare_frame_results_frame(SimpleNamespace(verbose=False), expected, actual)
        self.assertEqual(ok, matches, message)

    def test_all_families_and_empty_results_are_compared_without_parents(self):
        for kind in ROOTS:
            with self.subTest(kind=kind):
                self.compare(frame(kind, mask(kind)), frame(kind, mask(kind), mode="another style"), True)
                self.compare(frame(kind), frame(kind), True)
                self.compare(frame(kind), frame(kind, mask(kind)), False)

    def test_alpha_meaning_and_intermediate_opacity_are_not_guessed(self):
        expected = mask("alpha", (0, 64, 128, 255))
        self.compare(frame("alpha", expected), frame("alpha", mask("alpha", (0, 65, 127, 255))), True)
        self.compare(frame("alpha", expected), frame("alpha", mask("alpha", (255, 191, 127, 0))), False)
        other = mask("alpha", (0, 64, 128, 255))
        other.meaning = OpacityMeaning.RegionOpacity
        self.compare(frame("alpha", expected), frame("alpha", other), False)

    def test_class_ids_are_unsigned_labels_not_opacity_or_magnitudes(self):
        self.compare(frame("semantic", mask("semantic", (0, 1, 2, 3))),
                     frame("semantic", mask("semantic", (0, 1, 2, 3), BitmapEncoding.Uint32LE)), True)
        self.compare(frame("semantic", mask("semantic", (0, 1, 2, 3))),
                     frame("semantic", mask("semantic", (3, 2, 1, 0))), False)
        for values in ((0, 300, 65536, 0xffffffff),):
            self.compare(frame("semantic", mask("semantic", values, BitmapEncoding.Uint32LE)),
                         frame("semantic", mask("semantic", values, BitmapEncoding.Uint32LE)), True)
        for field, value in (("labelSet", "another:set"), ("voidValue", 0)):
            other = mask("semantic")
            setattr(other, field, value)
            self.compare(frame("semantic", mask("semantic")), frame("semantic", other), False)

    def test_instance_membership_uses_iou_and_preserves_optional_identity(self):
        self.compare(frame("instance", mask("instance", (1, 0, 0, 0))),
                     frame("instance", mask("instance", (0, 0, 0, 0))), False)
        self.compare(frame("instance", mask("instance", (0, 0, 0, 0))),
                     frame("instance", mask("instance", (0, 0, 0, 0))), True)
        other = mask("instance")
        other.classId = None
        self.compare(frame("instance", mask("instance")), frame("instance", other), False)

    def test_invalid_bitmaps_fail_even_when_both_sides_are_identical(self):
        for kind in ROOTS:
            for field, value in (("width", 0), ("width", 0xffffffff), ("pixels", b""),
                                 ("pixels", bytes(5)), ("space", None), ("encoding", 99)):
                invalid = mask(kind)
                setattr(invalid.bitmap, field, value)
                self.compare(frame(kind, invalid), frame(kind, invalid), False)
        invalid = mask("instance", (0, 1, 2, 1))
        self.compare(frame("instance", invalid), frame("instance", invalid), False)
        invalid = mask("instance")
        invalid.confidence = float("nan")
        self.compare(frame("instance", invalid), frame("instance", invalid), False)
        for field, value in (("labelSet", ""), ("voidValue", 256)):
            invalid = mask("semantic")
            setattr(invalid, field, value)
            self.compare(frame("semantic", invalid), frame("semantic", invalid), False)

    def test_image_basis_and_grid_resolution_are_independent_of_object_ids(self):
        expected, actual = mask("semantic"), mask("semantic")
        actual.bitmap.space.frameId = 99
        actual.object.id = 200
        actual.bitmap.width = actual.bitmap.height = 4
        actual.bitmap.pixels = bytes([0, 0, 1, 1] * 4)
        self.compare(frame("semantic", expected), frame("semantic", actual), True)
        actual.bitmap.space.originX = 10
        actual.bitmap.space.extentWidth = 90
        self.compare(frame("semantic", expected), frame("semantic", actual), False)
        actual.bitmap.space.originX = 0
        actual.bitmap.space.extentWidth = 100
        ambiguous = frame("semantic", expected, actual)
        self.compare(ambiguous, ambiguous, False)


if __name__ == "__main__":
    unittest.main()
