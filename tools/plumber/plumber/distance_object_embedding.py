################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

import math


def distance_object_embedding(gt_det: dict, out_det: dict, gt_parent_det: dict, out_parent_det: dict) -> float:
    """
    Cosine distance between embedding vectors.
    Returns value in [0,1].
    """

    del gt_parent_det
    del out_parent_det

    v1 = gt_det.get("data", {}).get("values", [])
    v2 = out_det.get("data", {}).get("values", [])

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

    if norm1 == 0.0 or norm2 == 0.0:
        return 1.0

    cos_sim = dot / (math.sqrt(norm1) * math.sqrt(norm2))

    # numerical safety
    cos_sim = max(-1.0, min(1.0, cos_sim))

    # convert to [0,1]
    distance = 1.0 - cos_sim

    return max(0.0, min(1.0, distance))
