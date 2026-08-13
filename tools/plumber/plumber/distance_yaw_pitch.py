################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from . import auxiliary as aux


def distance_yawpitch(
        gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item: dict
) -> float:
    """
    Normalize yaw/pitch difference into [0,1].
    For now return normalized max(|yaw_diff|, |pitch_diff|) / 180 (safe upper bound).
    """

    del _gt_parent_item
    del _out_parent_item

    g = gt_item.get("data", {})
    o = out_item.get("data", {})
    yaw_diff = abs(float(g.get("yaw", 0.0)) - float(o.get("yaw", 0.0)))
    pitch_diff = abs(float(g.get("pitch", 0.0)) - float(o.get("pitch", 0.0)))
    # angles are in degrees-ish; use 180 as safe normalization factor
    return aux.clamp01(max(yaw_diff, pitch_diff) / 180.0)
