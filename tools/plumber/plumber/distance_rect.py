################################################################
# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
################################################################

from typing import Tuple, Optional

from . import auxiliary as aux


# normalize rectangle fields to [x0,y0,x1,y1] in relative coordinates (0..1)


def normalize_rect(item: dict, frame_size: Optional[Tuple[int, int]]) -> Tuple[float, float, float, float]:
    data = item.get("data", {})
    x = float(data.get("x", 0.0))
    y = float(data.get("y", 0.0))
    w = float(data.get("width", 0.0))
    h = float(data.get("height", 0.0))

    if frame_size is not None:
        fw, fh = frame_size
        if fw > 0 and fh > 0:
            nx = x / fw
            ny = y / fh
            nw = w / fw
            nh = h / fh
        else:
            # fallback: avoid division by zero
            nx, ny, nw, nh = x, y, w, h
    else:
        # no frame size: heuristically normalize using width/height if nonzero
        # but IoU does not require normalization, so just compute relative to the larger of w/h
        nx, ny, nw, nh = x, y, w, h

    # convert (x,y,width,height) -> (x0,y0,x1,y1)
    x0 = aux.clamp01(nx)
    y0 = aux.clamp01(ny)
    x1 = aux.clamp01(nx + nw)
    y1 = aux.clamp01(ny + nh)
    return (x0, y0, x1, y1)


def iou_from_normalized(a: Tuple[float, float, float, float], b: Tuple[float, float, float, float]) -> float:
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b

    inter_x0 = max(ax0, bx0)
    inter_y0 = max(ay0, by0)
    inter_x1 = min(ax1, bx1)
    inter_y1 = min(ay1, by1)

    iw = max(0.0, inter_x1 - inter_x0)
    ih = max(0.0, inter_y1 - inter_y0)
    inter = iw * ih
    area_a = max(0.0, (ax1 - ax0)) * max(0.0, (ay1 - ay0))
    area_b = max(0.0, (bx1 - bx0)) * max(0.0, (by1 - by0))
    denom = area_a + area_b - inter
    if denom <= 0.0:
        return 0.0
    return inter / denom


def distance_rect(gt_item: dict, out_item: dict, gt_parent_item: dict, out_parent_item: dict) -> float:
    """
    Return a normalized distance in [0,1] for rectangle-like frame result items.
    We use: distance = 1 - IoU(normalized boxes).
    If either rectangle has zero area, IoU=0 (distance=1).
    """

    gt_frame_size = aux.frame_size_from_parent(gt_parent_item)
    out_frame_size = aux.frame_size_from_parent(out_parent_item)

    a = normalize_rect(gt_item, gt_frame_size)
    b = normalize_rect(out_item, out_frame_size)
    iou = iou_from_normalized(a, b)
    dist = 1.0 - iou
    # clamp for numerical safety
    return max(0.0, min(1.0, dist))
