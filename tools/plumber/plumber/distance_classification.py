################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


def distance_classification(gt_det: dict, out_det: dict, _gt_parent_det: dict, _out_parent_det) -> float:
    """
    Placeholder: compare top-1 text equality (0 if equal else 1).
    We'll implement normalized candidate overlap later.
    """

    del _gt_parent_det
    del _out_parent_det

    g_cands = gt_det.get("data", {}).get("candidates", [])
    o_cands = out_det.get("data", {}).get("candidates", [])
    if not g_cands or not o_cands:
        return 1.0
    gt_top = g_cands[0].get("text")
    out_top = o_cands[0].get("text")
    return 0.0 if gt_top == out_top else 1.0
