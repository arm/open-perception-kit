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

import math
from open_perception_kit.fb.open_perception_kit.metadata.TrackObservationState import TrackObservationState as State
from open_perception_kit.fb.open_perception_kit.metadata.TrackingTimeSource import TrackingTimeSource as Clock
from open_perception_kit.fb.open_perception_kit.metadata.TrackAssociationKind import TrackAssociationKind as Kind
from .distance_rect import distance_rect, GEOMETRY_THRESHOLD
from .distance_subject import distance_subject, same_track


def identity_key(value):
    if not value or not value["trackerId"] or not value["sessionId"] or value["trackId"] <= 0:
        return None
    return value["trackerId"], value["sessionId"], value["trackId"]


def valid_sample(value):
    return (value is not None and value["frameIndex"] > 0 and math.isfinite(value["elapsedMs"]) and value["elapsedMs"] >= 0
            and value["state"] in (State.Observed, State.Predicted) and value["timeSource"] in (Clock.MediaDelta, Clock.FallbackDelta))


def same_sample(a, b):
    return (valid_sample(a) and valid_sample(b) and a["frameIndex"] == b["frameIndex"] and a["state"] == b["state"]
            and a["timeSource"] == b["timeSource"] and math.isclose(a["elapsedMs"], b["elapsedMs"], rel_tol=0, abs_tol=1e-6))


def valid_association(value):
    if value is None:
        return True
    kind, score = value["kind"], value["score"]
    if kind in (Kind.Unspecified, Kind.New):
        return score is None
    if score is None or not math.isfinite(score):
        return False
    if kind == Kind.IoUScore:
        return 0 <= score <= 1
    return kind in (Kind.CosineScore, Kind.Reacquired) and -1 <= score <= 1


def valid_track(data):
    if (not identity_key(data["identity"]) or not valid_sample(data["sample"]) or not data["spatialValid"]
            or data.get("ambiguousTrackIdentity") or data.get("ambiguousSession") or not valid_association(data["association"])):
        return False
    score = data["sourceConfidence"]
    if score is not None and (not math.isfinite(score) or not 0 <= score <= 1):
        return False
    if data["sample"]["state"] == State.Predicted and data["association"] is not None:
        return False
    source, frame = data["sourceId"], data["sourceFrameIndex"]
    if (source is None) != (frame is None):
        return False
    return source is None or source > 0 and frame > 0 and (
        frame == data["sample"]["frameIndex"] if data["sample"]["state"] == State.Observed else frame < data["sample"]["frameIndex"])


def valid_trace(data):
    if (not identity_key(data["identity"]) or not data["spatialValid"] or data.get("ambiguousTrackIdentity")
            or data.get("ambiguousSession") or data.get("invalidLinkedTrack")):
        return False
    previous = None
    for point in data["points"]:
        if point is None or not math.isfinite(point["x"]) or not math.isfinite(point["y"]) or not valid_sample(point["sample"]):
            return False
        sample = point["sample"]
        if previous and (sample["frameIndex"] <= previous["frameIndex"] or sample["elapsedMs"] <= previous["elapsedMs"]):
            return False
        previous = sample
    return not data.get("linkedSample") or previous is not None and same_sample(previous, data["linkedSample"])


def distance_object_track(gt, out, _gt_parent, _out_parent):
    a, b = gt["data"], out["data"]
    if not valid_track(a) or not valid_track(b) or not same_track(a, b) or not same_sample(a["sample"], b["sample"]):
        return 1.0
    if (a["sourceId"] is None) != (b["sourceId"] is None) or a["sourceFrameIndex"] != b["sourceFrameIndex"] or a["classId"] != b["classId"]:
        return 1.0
    if (a["association"] or {}).get("kind", Kind.Unspecified) != (b["association"] or {}).get("kind", Kind.Unspecified):
        return 1.0
    if a["sample"]["state"] == State.Observed and a["sourceId"] is not None:
        if distance_subject(gt, out, gt.get("subject"), out.get("subject")) > GEOMETRY_THRESHOLD:
            return 1.0
    return distance_rect(gt, out, gt, out)
