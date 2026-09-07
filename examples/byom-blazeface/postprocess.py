################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

import json
import math

import numpy

from perception.guest import Envelope, external_key
from pek_python_ops import Context, Tensor, python_script


# Model and decoding contract. These values match the pinned BlazeFace graph.
FACES_KEY = external_key("com.arm.example.blazeface.faces.v1")
INPUT_SIZE = 128
ANCHOR_STRIDES = (8, 16, 16, 16)
MIN_SCALE = 0.1484375
MAX_SCALE = 0.75
ANCHOR_OFFSET = 0.5
ASPECT_RATIOS = (1.0,)
INTERPOLATED_SCALE_ASPECT_RATIO = 1.0
CONFIDENCE_THRESHOLD = 0.75
IOU_THRESHOLD = 0.30
MAX_FACES = 10
BOX_SHAPE = (1, 896, 16)
SCORE_SHAPE = (1, 896, 1)
EXPECTED_TENSORS = {
    "regressors": BOX_SHAPE,
    "classificators": SCORE_SHAPE,
}


# BlazeFace uses a fixed MediaPipe SSD anchor grid for its 896 candidates.
def _scale_for_layer(layer: int) -> float:
    if len(ANCHOR_STRIDES) == 1:
        return (MIN_SCALE + MAX_SCALE) * 0.5
    return MIN_SCALE + (MAX_SCALE - MIN_SCALE) * layer / (len(ANCHOR_STRIDES) - 1)


def _generate_anchors() -> numpy.ndarray:
    anchors = []
    layer = 0
    while layer < len(ANCHOR_STRIDES):
        last_same_stride = layer
        anchor_sizes = []
        while (
            last_same_stride < len(ANCHOR_STRIDES)
            and ANCHOR_STRIDES[last_same_stride] == ANCHOR_STRIDES[layer]
        ):
            scale = _scale_for_layer(last_same_stride)
            for aspect_ratio in ASPECT_RATIOS:
                ratio_sqrt = math.sqrt(aspect_ratio)
                anchor_sizes.append((scale * ratio_sqrt, scale / ratio_sqrt))
            if INTERPOLATED_SCALE_ASPECT_RATIO > 0.0:
                next_scale = (
                    1.0
                    if last_same_stride == len(ANCHOR_STRIDES) - 1
                    else _scale_for_layer(last_same_stride + 1)
                )
                interpolated_scale = math.sqrt(scale * next_scale)
                ratio_sqrt = math.sqrt(INTERPOLATED_SCALE_ASPECT_RATIO)
                anchor_sizes.append(
                    (
                        interpolated_scale * ratio_sqrt,
                        interpolated_scale / ratio_sqrt,
                    )
                )
            last_same_stride += 1

        stride = ANCHOR_STRIDES[layer]
        feature_map_size = math.ceil(INPUT_SIZE / stride)
        for y in range(feature_map_size):
            for x in range(feature_map_size):
                center_x = (x + ANCHOR_OFFSET) / feature_map_size
                center_y = (y + ANCHOR_OFFSET) / feature_map_size
                for _width, _height in anchor_sizes:
                    anchors.append((center_x, center_y, 1.0, 1.0))
        layer = last_same_stride

    result = numpy.asarray(anchors, dtype=numpy.float32)
    if result.shape != (896, 4):
        raise RuntimeError(f"expected 896 BlazeFace anchors, got {result.shape}")
    return result


ANCHORS = _generate_anchors()


# Validate the inference boundary before interpreting any tensor bytes.
def _validated_outputs(tensors: tuple[Tensor, ...]) -> tuple[numpy.ndarray, numpy.ndarray]:
    if len(tensors) != 2:
        raise RuntimeError(f"expected exactly two BlazeFace outputs, got {len(tensors)}")

    named = {}
    unnamed = []
    for tensor in tensors:
        if tensor.quantized:
            raise RuntimeError(f"BlazeFace output {tensor.name!r} must be floating-point")
        if tensor.array.dtype.kind != "f":
            raise RuntimeError(
                f"BlazeFace output {tensor.name!r} has non-floating dtype {tensor.array.dtype}"
            )
        if tensor.name is None:
            unnamed.append(tensor)
        elif tensor.name in EXPECTED_TENSORS:
            if tensor.name in named:
                raise RuntimeError(f"duplicate BlazeFace output name {tensor.name!r}")
            named[tensor.name] = tensor
        else:
            raise RuntimeError(f"unexpected BlazeFace output name {tensor.name!r}")

    for expected_name, expected_shape in EXPECTED_TENSORS.items():
        if expected_name in named:
            continue
        candidate_indexes = [
            index for index, tensor in enumerate(unnamed) if tensor.array.shape == expected_shape
        ]
        if len(candidate_indexes) != 1:
            raise RuntimeError(
                f"cannot unambiguously identify missing output {expected_name!r} "
                f"with shape {expected_shape}"
            )
        named[expected_name] = unnamed.pop(candidate_indexes[0])

    if unnamed:
        raise RuntimeError("unmatched unnamed BlazeFace output tensor")

    arrays = []
    for expected_name, expected_shape in EXPECTED_TENSORS.items():
        values = named[expected_name].array
        if values.shape != expected_shape:
            raise RuntimeError(
                f"BlazeFace output {expected_name!r} expected shape {expected_shape}, "
                f"got {values.shape}"
            )
        if not numpy.isfinite(values).all():
            raise RuntimeError(f"BlazeFace output {expected_name!r} contains non-finite values")
        arrays.append(values.astype(numpy.float32, copy=False))
    return arrays[0], arrays[1]


# Decode candidate rectangles and merge overlapping detections.
def _iou(first: numpy.ndarray, second: numpy.ndarray) -> float:
    left = max(float(first[0]), float(second[0]))
    top = max(float(first[1]), float(second[1]))
    right = min(float(first[0] + first[2]), float(second[0] + second[2]))
    bottom = min(float(first[1] + first[3]), float(second[1] + second[3]))
    intersection = max(0.0, right - left) * max(0.0, bottom - top)
    union = float(first[2] * first[3] + second[2] * second[3]) - intersection
    return intersection / union if union > 0.0 else 0.0


def _weighted_nms(
    candidates: list[tuple[numpy.ndarray, float]],
) -> list[tuple[numpy.ndarray, float]]:
    candidates.sort(key=lambda candidate: candidate[1], reverse=True)
    remaining = candidates
    detections = []
    while remaining and len(detections) < MAX_FACES:
        reference_box, reference_score = remaining[0]
        cluster = []
        next_remaining = []
        for candidate in remaining:
            if _iou(reference_box, candidate[0]) > IOU_THRESHOLD:
                cluster.append(candidate)
            else:
                next_remaining.append(candidate)

        total_score = sum(score for _box, score in cluster)
        merged_box = sum((box * score for box, score in cluster), start=numpy.zeros(4))
        merged_box /= total_score
        detections.append((merged_box, reference_score))
        remaining = next_remaining
    return detections


def _decode_faces(boxes: numpy.ndarray, scores: numpy.ndarray) -> list[dict[str, float]]:
    logits = numpy.clip(scores[0, :, 0], -100.0, 100.0)
    probabilities = numpy.empty_like(logits, dtype=numpy.float32)
    nonnegative = logits >= 0.0
    probabilities[nonnegative] = 1.0 / (1.0 + numpy.exp(-logits[nonnegative]))
    exponentials = numpy.exp(logits[~nonnegative])
    probabilities[~nonnegative] = exponentials / (1.0 + exponentials)
    selected = numpy.flatnonzero(probabilities >= CONFIDENCE_THRESHOLD)
    candidates = []
    for index in selected:
        raw = boxes[0, index]
        center_x = raw[0] / INPUT_SIZE + ANCHORS[index, 0]
        center_y = raw[1] / INPUT_SIZE + ANCHORS[index, 1]
        width = raw[2] / INPUT_SIZE
        height = raw[3] / INPUT_SIZE
        box = numpy.asarray(
            (center_x - width * 0.5, center_y - height * 0.5, width, height),
            dtype=numpy.float64,
        )
        if numpy.isfinite(box).all() and box[2] > 0.0 and box[3] > 0.0:
            candidates.append((box, float(probabilities[index])))

    faces = []
    for box, confidence in _weighted_nms(candidates):
        left = min(max(float(box[0]), 0.0), 1.0)
        top = min(max(float(box[1]), 0.0), 1.0)
        right = min(max(float(box[0] + box[2]), 0.0), 1.0)
        bottom = min(max(float(box[1] + box[3]), 0.0), 1.0)
        width = right - left
        height = bottom - top
        values = (left, top, width, height, confidence)
        if width <= 0.0 or height <= 0.0 or not all(math.isfinite(value) for value in values):
            continue
        faces.append(
            {
                "x": left,
                "y": top,
                "width": width,
                "height": height,
                "confidence": confidence,
            }
        )
    faces.sort(key=lambda face: face["confidence"], reverse=True)
    return faces[:MAX_FACES]


# This is the only callback PEK invokes. It owns no state between frames.
@python_script
def process(
    env: Envelope,
    tensors: tuple[Tensor, ...],
    context: Context,
) -> None:
    del context
    boxes, scores = _validated_outputs(tensors)
    json_bytes = json.dumps(
        {"faces": _decode_faces(boxes, scores)},
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    env.add(FACES_KEY, json_bytes)
