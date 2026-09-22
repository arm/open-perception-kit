################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


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
