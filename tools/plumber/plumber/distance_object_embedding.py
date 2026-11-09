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
from open_perception_kit.fb.open_perception_kit.metadata.EmbeddingMetric import EmbeddingMetric
from .distance_subject import distance_subject
from .distance_rect import GEOMETRY_THRESHOLD


def usable(data):
    space, values = data.get("space"), data.get("values", [])
    return (space is not None and bool(space["id"]) and space["metric"] == EmbeddingMetric.Cosine
            and space["dimensions"] == len(values) and bool(values)
            and all(math.isfinite(value) for value in values)
            and any(value != 0 for value in values))


def distance_object_embedding(gt_item: dict, out_item: dict, gt_parent_item: dict, out_parent_item: dict) -> float:
    """Existing clipped cosine distance, only for declared spaces and corresponding subjects."""
    a, b = gt_item["data"], out_item["data"]
    if not usable(a) or not usable(b) or a["space"] != b["space"]:
        return 1.0
    if distance_subject(gt_item, out_item, gt_parent_item, out_parent_item) > GEOMETRY_THRESHOLD:
        return 1.0
    v1, v2 = a["values"], b["values"]
    dot = math.fsum(x * y for x, y in zip(v1, v2))
    norm1 = math.sqrt(math.fsum(x * x for x in v1))
    norm2 = math.sqrt(math.fsum(x * x for x in v2))
    return max(0.0, min(1.0, 1.0 - dot / (norm1 * norm2)))
