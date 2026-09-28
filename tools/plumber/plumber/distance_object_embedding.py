# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates <perception-fdbck@arm.com>
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


def is_near_zero(x: float, abs_tol: float = 1e-9) -> bool:
    return math.isclose(x, 0.0, abs_tol=abs_tol)


def distance_object_embedding(gt_item: dict, out_item: dict, gt_parent_item: dict, out_parent_item: dict) -> float:
    """
    Cosine distance between embedding vectors.
    Returns value in [0,1].
    """

    del gt_parent_item
    del out_parent_item

    v1 = gt_item.get("data", {}).get("values", [])
    v2 = out_item.get("data", {}).get("values", [])

    if not v1 or not v2:
        return 1.0

    if len(v1) != len(v2):
        return 1.0

    dot = 0.0
    norm1 = 0.0
    norm2 = 0.0

    for a, b in zip(v1, v2):
        dot += a * b
        norm1 += a * a
        norm2 += b * b

    if is_near_zero(norm1) or is_near_zero(norm2):
        return 1.0

    cos_sim = dot / (math.sqrt(norm1) * math.sqrt(norm2))

    # numerical safety
    cos_sim = max(-1.0, min(1.0, cos_sim))

    # convert to [0,1]
    distance = 1.0 - cos_sim

    return max(0.0, min(1.0, distance))
