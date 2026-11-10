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

import math
from dataclasses import dataclass
from typing import Callable, Tuple

from . import auxiliary as aux
from . import distance_classification as dc
from . import distance_object_embedding as doe
from . import distance_rect as dr
from . import distance_seg_map as dsm
from . import distance_track_trace as dtt
from . import distance_video_frame as dvf
from . import distance_gaze as dg
from .tracking import distance_object_track, valid_track, valid_trace
from .frame_results_decode import FrameResultsFrame
from .frame_results_normalize import PayloadKey, PayloadSnapshot, normalize_frame_results

DistanceFn = Callable[[dict, dict, dict, dict], float]


@dataclass
class DistanceSpec:
    func: DistanceFn
    threshold: float
    skip: bool = False


DISTANCE_SPECS: dict[str, DistanceSpec] = {
    "VideoFrame": DistanceSpec(dvf.distance_video_frame, threshold=0.1, skip=True),
    "AudioFrame": DistanceSpec(dvf.distance_video_frame, threshold=0.1, skip=True),
    "BoxDetection": DistanceSpec(dr.distance_rect, threshold=dr.GEOMETRY_THRESHOLD),
    "ObjectTrack": DistanceSpec(distance_object_track, threshold=dr.GEOMETRY_THRESHOLD),
    "GazeMeasurement": DistanceSpec(dg.distance_gaze, threshold=dg.ANGLE_DISTANCE_THRESHOLD),
    "SegmentationMask": DistanceSpec(dsm.distance_segmentation_map, threshold=0.1),
    "SemanticSegmentationMask": DistanceSpec(dsm.distance_segmentation_map, threshold=0.1),
    "InstanceSegmentationMask": DistanceSpec(dsm.distance_segmentation_map, threshold=0.1),
    "Classification": DistanceSpec(dc.distance_classification, threshold=0.0),
    "CameraContact": DistanceSpec(dc.distance_camera_contact, threshold=0.0),
    "PersonPresence": DistanceSpec(dc.distance_person_presence, threshold=0.0),
    "VoiceActivityDetection": DistanceSpec(dc.distance_voice_activity, threshold=0.01),
    "TurnDetection": DistanceSpec(dc.distance_turn_detection, threshold=0.01),
    "TrackTrace": DistanceSpec(dtt.distance_track_trace, threshold=0.1, skip=False),
    "ObjectEmbedding": DistanceSpec(doe.distance_object_embedding, threshold=0.2, skip=False),
}


def _format_payload_key(key: PayloadKey) -> str:
    return (
        f"{key.payload_type}"
        f"[inferElementId={key.infer_element_id!r}, "
        f"contentType={key.content_type!r}, model={key.model!r}, "
        f"producerComponent={key.producer_component!r}, producerInstanceId={key.producer_instance_id!r}]"
    )


def _item_context(item: dict) -> dict | None:
    if item.get("item_type") == "ObjectTrack":
        return item if valid_track(item["data"]) else None
    if item.get("item_type") == "TrackTrace":
        return item if valid_trace(item["data"]) else None
    if item.get("item_type") in dsm.MASK_TYPES:
        return item if dsm.valid_mask(item) else None
    if item.get("item_type") == "BoxDetection":
        return item if item.get("data", {}).get("spatialValid") else None
    if item.get("item_type") in ("Classification", "CameraContact", "PersonPresence", "GazeMeasurement", "ObjectEmbedding"):
        return item.get("subject")
    if item.get("item_type") in ("VoiceActivityDetection", "TurnDetection"):
        # Audio comparisons use producer-scoped window bounds and measurements.
        # A derivation parent may belong to an earlier buffer and need not be retained.
        return item
    return None


def _process_remaining_items(
    item_index: int,
    gt_item: dict,
    remaining: list[dict],
    dist_spec: DistanceSpec,
) -> Tuple[bool, str]:
    best_j = -1
    best_dist = math.inf

    gt_parent_item = _item_context(gt_item)
    if gt_parent_item is None:
        if gt_item.get("item_type") in dsm.MASK_TYPES:
            return False, "mask has invalid encoding, semantics or image space"
        if gt_item.get("item_type") in ("Classification", "CameraContact", "PersonPresence", "GazeMeasurement", "ObjectEmbedding"):
            return False, "subject missing, ambiguous or invalid"
        if gt_item.get("item_type") in ("BoxDetection", "ObjectTrack", "TrackTrace"):
            return False, "item has no valid spatial reference"
        return False, "item has no supported comparison context"

    for j, out_item in enumerate(remaining):
        out_parent_item = _item_context(out_item)
        if out_parent_item is None:
            if out_item.get("item_type") in dsm.MASK_TYPES:
                return False, "mask has invalid encoding, semantics or image space"
            if out_item.get("item_type") in ("Classification", "CameraContact", "PersonPresence", "GazeMeasurement", "ObjectEmbedding"):
                return False, "subject missing, ambiguous or invalid"
            if out_item.get("item_type") in ("BoxDetection", "ObjectTrack", "TrackTrace"):
                return False, "item has no valid spatial reference"
            return False, "item has no supported comparison context"

        distance = dist_spec.func(gt_item, out_item, gt_parent_item, out_parent_item)
        if not (isinstance(distance, (int, float)) and math.isfinite(distance)):
            return False, f"distance function returned non-finite value for item {item_index}"
        if distance < best_dist:
            best_dist = distance
            best_j = j

    if best_dist > dist_spec.threshold:
        return False, (
            f"distance too large for item {item_index}: "
            f"{best_dist:.6f} > threshold {dist_spec.threshold}"
        )

    if best_j >= 0:
        remaining.pop(best_j)
        return True, ""

    return False, "the best match cannot be found"


def _greedy_match_by_distance(
    gt_items: list[dict],
    out_items: list[dict],
    dist_spec: DistanceSpec,
) -> Tuple[bool, str]:
    if dist_spec.skip:
        return True, "skipped by config"

    if len(gt_items) != len(out_items):
        return False, f"count mismatch: gt={len(gt_items)} out={len(out_items)}"

    remaining = list(out_items)

    for item_index, gt_item in enumerate(gt_items):
        if not remaining:
            return False, f"ran out of output items at gt index {item_index}"

        ok, msg = _process_remaining_items(
            item_index,
            gt_item,
            remaining,
            dist_spec,
        )
        if not ok:
            return ok, msg

    if remaining:
        return False, f"unmatched output items remain: {len(remaining)}"

    return True, "ok"


def _compare_payload_snapshot(
    key: PayloadKey,
    gt_payload: PayloadSnapshot,
    out_payload: PayloadSnapshot,
) -> Tuple[bool, str]:
    gt_by_type = aux.group_by_item_type(gt_payload.items)
    out_by_type = aux.group_by_item_type(out_payload.items)

    gt_types = set(gt_by_type.keys())
    out_types = set(out_by_type.keys())
    if gt_types != out_types:
        missing = sorted(gt_types - out_types)
        extra = sorted(out_types - gt_types)
        return False, f"item type mismatch: missing={missing} extra={extra}"

    for item_type in sorted(gt_types):
        spec = DISTANCE_SPECS.get(item_type)
        if spec is None:
            return False, f"no distance spec for item type {item_type!r}"

        ok, msg = _greedy_match_by_distance(
            gt_by_type[item_type],
            out_by_type[item_type],
            spec,
        )
        if not ok:
            return False, f"item type {item_type!r}: {msg}"

    return True, f"OK: matched payload {_format_payload_key(key)}"


def compare_frame_results_frame(args, gt_frame: FrameResultsFrame, out_frame: FrameResultsFrame) -> Tuple[bool, str]:
    if not gt_frame.frame_results.valid() or not out_frame.frame_results.valid():
        return False, "invalid FrameResults envelope"
    if args.verbose:
        print(
            "====================== comparing frame: "
            f"{out_frame.frame_counter} ============================="
        )

    gt_snapshot = normalize_frame_results(gt_frame.frame_results, gt_frame.frame_counter)
    out_snapshot = normalize_frame_results(out_frame.frame_results, out_frame.frame_counter)
    if gt_snapshot.unhandled_payloads or out_snapshot.unhandled_payloads:
        return False, "unsupported or malformed payload entries cannot establish a comparison"

    gt_keys = set(gt_snapshot.payloads.keys())
    out_keys = set(out_snapshot.payloads.keys())
    if gt_keys != out_keys:
        missing = sorted(_format_payload_key(key) for key in gt_keys - out_keys)
        extra = sorted(_format_payload_key(key) for key in out_keys - gt_keys)
        return False, f"payload mismatch: missing={missing} extra={extra}"

    for key in sorted(gt_keys):
        ok, msg = _compare_payload_snapshot(
            key,
            gt_snapshot.payloads[key],
            out_snapshot.payloads[key],
        )
        if not ok:
            return False, f"Payload {_format_payload_key(key)} mismatch: {msg}"

    return True, f"OK: matched {len(gt_keys)} FrameResults payload group(s)"
