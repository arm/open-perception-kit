################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


from typing import Dict, List, Optional, Tuple


def get_layers(obj: dict) -> List[dict]:
    return (((obj.get("perception") or {}).get("layers")) or [])


def index_layers_by_element_id(layers: List[dict], key_name: str = "element-id") -> Dict[str, dict]:
    """
    Build a map: element-id -> layer.
    If duplicates exist, last wins (you can change to error if you want).
    """
    idx: Dict[str, dict] = {}
    for layer in layers:
        element_id = layer.get(key_name)
        if isinstance(element_id, str) and element_id:
            idx[element_id] = layer
    return idx


def group_by_type(dets: List[dict]) -> Dict[str, List[dict]]:
    grouped: Dict[str, List[dict]] = {}
    for d in dets or []:
        t = d.get("type", "")
        grouped.setdefault(t, []).append(d)
    return grouped


def build_uuid_index(perception: dict) -> dict[int, tuple[dict, dict]]:
    """
    Build uuid -> (layer, detection) index for the whole perception.
    """
    index = {}
    for layer in perception.get("layers", []):
        for det in layer.get("detections", []):
            data = det.get("data", {})
            uuid = data.get("uuid")
            if uuid is not None:
                index[uuid] = (layer, det)
    return index


def frame_size_from_parent(parent_det: dict) -> Optional[Tuple[int, int]]:
    data = parent_det["data"]
    match parent_det["type"]:
        case "VideoFrame":
            return (data["originalWidth"], data["originalHeight"])
        case "Rect":
            return (data["width"], data["height"])
        case _:
            return None


def clamp01(v: float) -> float:
    return max(0.0, min(1.0, v))
