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


import math
import numpy as np
from open_perception_kit.fb.open_perception_kit.metadata.BitmapEncoding import BitmapEncoding
from open_perception_kit.fb.open_perception_kit.metadata.OpacityMeaning import OpacityMeaning
from .distance_rect import normalize_rect, iou_from_normalized, GEOMETRY_THRESHOLD

MASK_TYPES = ("SegmentationMask", "SemanticSegmentationMask", "InstanceSegmentationMask")


def samples(bitmap):
    dtype = "u1" if bitmap["encoding"] == BitmapEncoding.Uint8 else "<u4"
    return np.frombuffer(bitmap["pixels"], dtype=dtype).reshape(bitmap["height"], bitmap["width"])


def geometry_distance(a, b):
    return 1 - iou_from_normalized(
        normalize_rect({"data": a}, (a["frameWidth"], a["frameHeight"])),
        normalize_rect({"data": b}, (b["frameWidth"], b["frameHeight"])))


def valid_mask(item):
    data = item["data"]
    bitmap = data["bitmap"]
    size = {BitmapEncoding.Uint8: 1, BitmapEncoding.Uint32LE: 4}.get(bitmap["encoding"], 0)
    if (not size or data.get("ambiguousFrame") or not bitmap["region"]["spatialValid"]
            or bitmap["width"] <= 0 or bitmap["height"] <= 0
            or len(bitmap["pixels"]) != bitmap["width"] * bitmap["height"] * size):
        return False
    kind = item["item_type"]
    if kind == "SegmentationMask":
        return size == 1 and data["meaning"] in (OpacityMeaning.BackgroundOpacity, OpacityMeaning.RegionOpacity)
    if kind == "SemanticSegmentationMask":
        return bool(data["labelSet"]) and (data["voidValue"] is None or 0 <= data["voidValue"] < 2 ** (8 * size))
    if kind != "InstanceSegmentationMask" or size != 1:
        return False
    if data["classId"] is not None and (not data["labelSet"] or not 0 <= data["classId"] <= 0xffffffff):
        return False
    confidence = data["confidence"]
    if confidence is not None and (not math.isfinite(confidence) or not 0 <= confidence <= 1):
        return False
    box = data["box"]
    if box and (not box["spatialValid"] or data["boxFrameId"] != bitmap["frameId"]
                or any(box[field] != bitmap["region"][field] for field in ("frameWidth", "frameHeight"))):
        return False
    return bool(np.all(samples(bitmap) <= 1))


def distance_segmentation_map(gt_item, out_item, _gt_parent_item, _out_parent_item):
    if not valid_mask(gt_item) or not valid_mask(out_item) or gt_item["item_type"] != out_item["item_type"]:
        return 1.0
    expected, actual = gt_item["data"], out_item["data"]
    if any(expected[key] != actual[key] for key in ("meaning", "labelSet", "voidValue", "classId")):
        return 1.0
    if geometry_distance(expected["bitmap"]["region"], actual["bitmap"]["region"]) > GEOMETRY_THRESHOLD:
        return 1.0
    if (expected["box"] is None) != (actual["box"] is None):
        return 1.0
    if expected["box"] is not None:
        if geometry_distance(expected["box"], actual["box"]) > GEOMETRY_THRESHOLD:
            return 1.0
    a, b = samples(expected["bitmap"]), samples(actual["bitmap"])
    # Use one existing grid, never the potentially huge Cartesian product of two
    # narrow grids. The tie-break is symmetric; class IDs are never interpolated.
    height, width = max((a.shape, b.shape), key=lambda shape: (shape[0] * shape[1], shape))

    def resize(values):
        return values[(np.arange(height) * values.shape[0] // height)[:, None],
                      np.arange(width) * values.shape[1] // width]
    a, b = resize(a), resize(b)
    if gt_item["item_type"] == "SegmentationMask":
        return float(np.mean(np.abs(a.astype(np.int16) - b.astype(np.int16))) / 255)
    if gt_item["item_type"] == "InstanceSegmentationMask":
        union = int(np.count_nonzero(a | b))
        return 1.0 - int(np.count_nonzero(a & b)) / union if union else 0.0
    return float(np.mean(a != b))
