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
from open_perception_kit.fb.open_perception_kit.metadata.ClassificationKind import ClassificationKind
from .distance_subject import distance_subject
from .distance_rect import GEOMETRY_THRESHOLD


def distance_classification(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item) -> float:
    """Compare declared kind/identity or literal text for the corresponding subject."""
    if distance_subject(gt_item, out_item, _gt_parent_item, _out_parent_item) > GEOMETRY_THRESHOLD:
        return 1.0

    g_cands = gt_item.get("data", {}).get("candidates", [])
    o_cands = out_item.get("data", {}).get("candidates", [])
    if not g_cands or not o_cands:
        return 1.0
    for candidate in (g_cands[0], o_cands[0]):
        if candidate.get("confidence") is not None and not _valid_probability(candidate["confidence"]):
            return 1.0
    kind = gt_item["data"].get("kind")
    if kind != out_item["data"].get("kind"):
        return 1.0
    gt_set = gt_item["data"].get("labelSet", "")
    out_set = out_item["data"].get("labelSet", "")
    gt_id = g_cands[0].get("classId")
    out_id = o_cands[0].get("classId")
    if kind == ClassificationKind.Categorical and gt_set and out_set and gt_id is not None and out_id is not None:
        return 0.0 if (gt_set, gt_id) == (out_set, out_id) else 1.0
    if kind == ClassificationKind.RecognizedText and not gt_set and not out_set and gt_id is None and out_id is None:
        return 0.0 if g_cands[0].get("text") == o_cands[0].get("text") else 1.0
    return 1.0  # An incomplete categorical identity cannot establish a match.


def _valid_probability(value) -> bool:
    return isinstance(value, (int, float)) and math.isfinite(value) and 0 <= value <= 1


def _contact_outcome(data):
    result = data.get("result")
    if (result is None or not isinstance(result.get("inContact"), bool)
            or (result.get("confidence") is not None and not _valid_probability(result["confidence"]))):
        return None
    return result["inContact"]


def distance_camera_contact(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item: dict) -> float:
    if distance_subject(gt_item, out_item, _gt_parent_item, _out_parent_item) > GEOMETRY_THRESHOLD:
        return 1.0
    gt = _contact_outcome(gt_item.get("data", {}))
    out = _contact_outcome(out_item.get("data", {}))
    if gt is None or out is None:
        return 1.0
    return 0.0 if gt == out else 1.0


def distance_person_presence(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item) -> float:
    if distance_subject(gt_item, out_item, _gt_parent_item, _out_parent_item) > GEOMETRY_THRESHOLD:
        return 1.0

    gt = gt_item.get("data", {}).get("result")
    out = out_item.get("data", {}).get("result")
    for result in (gt, out):
        if result is None or not isinstance(result.get("personPresent"), bool):
            return 1.0
        yes, no = result.get("yesScore"), result.get("noScore")
        if yes is None and no is None:
            continue
        if not isinstance(yes, (float, int)) or not isinstance(no, (float, int)) or not math.isfinite(yes) or not math.isfinite(no):
            return 1.0
    return 0.0 if gt["personPresent"] == out["personPresent"] else 1.0


def distance_voice_activity(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item: dict) -> float:
    gt = gt_item["data"]
    out = out_item["data"]
    # Match the analyzed interval and decision exactly; tolerate small score differences.
    for key in ("speechActive", "windowStartSample", "windowEndSample",
                "windowStartTsNs", "windowEndTsNs", "discontinuity"):
        if gt[key] != out[key]:
            return 1.0
    return abs(gt["speechProbability"] - out["speechProbability"])


def distance_turn_detection(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item: dict) -> float:
    gt = gt_item["data"]
    out = out_item["data"]
    # Compare assessments of the same context, not just their completion probabilities.
    for key in ("turnComplete", "windowStartSample", "windowEndSample",
                "windowStartTsNs", "windowEndTsNs", "discontinuity"):
        if gt[key] != out[key]:
            return 1.0
    return abs(gt["turnProbability"] - out["turnProbability"])
