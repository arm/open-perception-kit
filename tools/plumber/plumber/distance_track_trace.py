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

from . import auxiliary as aux
import math
from .tracking import valid_trace, same_sample
from .distance_subject import same_track


def distance_track_trace(gt_item: dict, out_item: dict, gt_parent_item: dict, out_parent_item: dict) -> float:
    """
    Distance between two TrackTrace items.
    Returns value in [0,1].
    """

    gt_points = gt_item.get("data", {}).get("points", [])
    out_points = out_item.get("data", {}).get("points", [])

    if (not valid_trace(gt_item["data"]) or not valid_trace(out_item["data"])
            or not same_track(gt_item["data"], out_item["data"]) or len(gt_points) != len(out_points)):
        return 1.0
    if not gt_points:
        return 0.0
    n = len(gt_points)

    total_dist = 0.0

    gt_frame_size = aux.frame_size_from_parent(gt_parent_item)
    out_frame_size = aux.frame_size_from_parent(out_parent_item)
    if not gt_frame_size or not out_frame_size or min(*gt_frame_size, *out_frame_size) <= 0:
        return math.inf
    fw, fh = gt_frame_size
    ow, oh = out_frame_size

    for i in range(n):
        if not same_sample(gt_points[i]["sample"], out_points[i]["sample"]):
            return 1.0
        gx = gt_points[i]["x"]
        gy = gt_points[i]["y"]
        ox = out_points[i]["x"]
        oy = out_points[i]["y"]

        gx /= fw
        gy /= fh
        ox /= ow
        oy /= oh

        dx = gx - ox
        dy = gy - oy

        dist = (dx * dx + dy * dy) ** 0.5  # Euclidean
        total_dist += dist

    avg_dist = total_dist / n

    # Clamp to [0,1]
    return max(0.0, min(1.0, avg_dist))
