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


from typing import Dict, List, Optional, Tuple


def group_by_item_type(items: List[dict]) -> Dict[str, List[dict]]:
    grouped: Dict[str, List[dict]] = {}
    for item in items or []:
        item_type = item.get("item_type", "")
        grouped.setdefault(item_type, []).append(item)
    return grouped


def frame_size_from_parent(parent_item: dict) -> Optional[Tuple[int, int]]:
    data = parent_item["data"]
    match parent_item["item_type"]:
        case "VideoFrame":
            return (data["originalWidth"], data["originalHeight"])
        case "BoxDetection" | "ObjectTrack":
            return (data["width"], data["height"])
        case _:
            return None


def clamp01(v: float) -> float:
    return max(0.0, min(1.0, v))
