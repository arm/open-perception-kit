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

from .distance_rect import GEOMETRY_THRESHOLD
from .distance_subject import distance_subject

ANGLE_DISTANCE_THRESHOLD = 0.01  # 1.8 degrees after normalization by 180.


def distance_gaze(
        gt_item: dict, out_item: dict, gt_subject: dict, out_subject: dict
) -> float:
    """Compare corresponding subjects and shortest per-axis degree differences."""
    if distance_subject(gt_item, out_item, gt_subject, out_subject) > GEOMETRY_THRESHOLD:
        return 1.0
    for item in (gt_item, out_item):
        data = item.get("data", {})
        angles = data.get("angles")
        confidence = data.get("confidence")
        if (angles is None or any(not isinstance(angles.get(key), (float, int))
                                  or not math.isfinite(angles[key]) for key in ("yawDegrees", "pitchDegrees"))
                or (confidence is not None and (not math.isfinite(confidence) or not 0 <= confidence <= 1))):
            return 1.0
    g, o = gt_item["data"]["angles"], out_item["data"]["angles"]
    angular = max(abs(math.remainder(g[key] - o[key], 360.0))
                  for key in ("yawDegrees", "pitchDegrees")) / 180.0
    return angular
