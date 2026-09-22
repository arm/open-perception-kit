################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from . import auxiliary as aux


def distance_track_trace(gt_item: dict, out_item: dict, gt_parent_item: dict, out_parent_item: dict) -> float:
    """
    Distance between two TrackTrace items.
    Returns value in [0,1].
    """

    del out_parent_item

    gt_points = gt_item.get("data", {}).get("points", [])
    out_points = out_item.get("data", {}).get("points", [])

    if not gt_points or not out_points:
        return 1.0

    # Use minimum length (simple v1)
    n = min(len(gt_points), len(out_points))

    total_dist = 0.0

    gt_frame_size = aux.frame_size_from_parent(gt_parent_item)

    for i in range(n):
        gx = gt_points[i]["x"]
        gy = gt_points[i]["y"]
        ox = out_points[i]["x"]
        oy = out_points[i]["y"]

        # Normalize if frame_size available
        if gt_frame_size:
            fw, fh = gt_frame_size
            if fw > 0 and fh > 0:
                gx /= fw
                gy /= fh
                ox /= fw
                oy /= fh

        dx = gx - ox
        dy = gy - oy

        dist = (dx * dx + dy * dy) ** 0.5  # Euclidean
        total_dist += dist

    avg_dist = total_dist / n

    # Clamp to [0,1]
    return max(0.0, min(1.0, avg_dist))
