################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################


def distance_classification(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item) -> float:
    """
    Placeholder: compare top-1 text equality (0 if equal else 1).
    We'll implement normalized candidate overlap later.
    """

    del _gt_parent_item
    del _out_parent_item

    g_cands = gt_item.get("data", {}).get("candidates", [])
    o_cands = out_item.get("data", {}).get("candidates", [])
    if not g_cands or not o_cands:
        return 1.0
    gt_top = g_cands[0].get("text")
    out_top = o_cands[0].get("text")
    return 0.0 if gt_top == out_top else 1.0


def distance_person_presence(gt_item: dict, out_item: dict, _gt_parent_item: dict, _out_parent_item) -> float:
    del _gt_parent_item
    del _out_parent_item

    gt_data = gt_item.get("data", {})
    out_data = out_item.get("data", {})
    yes_diff = abs(float(gt_data.get("yesConfidence", 0.0)) -
                   float(out_data.get("yesConfidence", 0.0)))
    no_diff = abs(float(gt_data.get("noConfidence", 0.0)) -
                  float(out_data.get("noConfidence", 0.0)))
    return max(0.0, min(1.0, max(yes_diff, no_diff)))
