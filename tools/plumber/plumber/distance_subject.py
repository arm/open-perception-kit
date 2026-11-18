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

from .distance_rect import distance_rect


def same_track(a, b):
    # Session UUIDs and observation IDs differ across independent executions.
    # Packet-level validation requires one unambiguous session per tracker.
    left, right = a.get("identity"), b.get("identity")
    return bool(left and right and left["trackerId"] and left["trackId"] > 0
                and (left["trackerId"], left["trackId"]) == (right["trackerId"], right["trackId"]))


def distance_subject(gt_item, out_item, gt_subject, out_subject):
    """Match observation scope without comparing frame-local observation IDs."""
    if (gt_subject is None or out_subject is None or gt_item.get("subject_source") is None
            or gt_item["subject_source"] != out_item.get("subject_source")):
        return 1.0
    if gt_subject["item_type"] == out_subject["item_type"] == "VideoFrame":
        # Resolution permits only one addressable frame per producer/source
        # group in each packet; ambiguous groups never reach this branch.
        return 0.0
    if gt_subject["item_type"] not in ("BoxDetection", "ObjectTrack") or out_subject["item_type"] not in ("BoxDetection", "ObjectTrack"):
        return 1.0
    if gt_subject["item_type"] != out_subject["item_type"]:
        return 1.0
    if gt_subject["item_type"] == "ObjectTrack" and not same_track(gt_subject["data"], out_subject["data"]):
        return 1.0
    return distance_rect(gt_subject, out_subject, gt_subject, out_subject)
