################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from pathlib import Path

import numpy

from pek_python_ops import Tensor
from perception.fb.perception.metadata.Classification import ClassificationT
from perception.fb.perception.metadata.ClassificationCandidate import ClassificationCandidateT
from perception.fb.perception.metadata.Classifications import ClassificationsT
from perception.fb.perception.metadata.LayerInfo import LayerInfoT
from perception.guest import Envelope


EXPECTED_OUTPUT_SHAPE = (1, 1001)
EXPECTED_LABEL_COUNT = EXPECTED_OUTPUT_SHAPE[1]
TOP_K = 5
LABELS = tuple(
    Path(__file__).with_name("imagenet_labels.txt").read_text(encoding="utf-8").splitlines()
)

if len(LABELS) != EXPECTED_LABEL_COUNT:
    raise RuntimeError(
        f"expected {EXPECTED_LABEL_COUNT} ImageNet labels, got {len(LABELS)}"
    )

stable_frame_count = 0
previous_class_id = None


def _tensor_values(tensor: Tensor) -> numpy.ndarray:
    values = tensor.array
    if tensor.quantized:
        return (values.astype(numpy.float32) - tensor.zero_point) * tensor.scale
    return values.astype(numpy.float32, copy=False)


def _top_classes(tensor: Tensor) -> list[tuple[int, float]]:
    logits = _tensor_values(tensor)
    if logits.shape != EXPECTED_OUTPUT_SHAPE:
        raise RuntimeError(
            f"expected MobileNet output shape {EXPECTED_OUTPUT_SHAPE}, got {logits.shape}"
        )

    logits = logits[0]
    probabilities = numpy.exp(logits - numpy.max(logits))
    probabilities /= numpy.sum(probabilities)
    class_ids = numpy.argsort(probabilities)[::-1][:TOP_K]
    return [
        (int(class_id), float(probabilities[class_id])) for class_id in class_ids
    ]


def process(env: Envelope, tensors: tuple[Tensor, ...]) -> None:
    global previous_class_id, stable_frame_count

    if len(tensors) != 1:
        raise RuntimeError(f"expected one MobileNet output tensor, got {len(tensors)}")

    tensor = tensors[0]
    top_classes = _top_classes(tensor)
    top_class_id = top_classes[0][0]

    if top_class_id == previous_class_id:
        stable_frame_count += 1
    else:
        previous_class_id = top_class_id
        stable_frame_count = 1

    candidates = []
    for class_id, confidence in top_classes:
        candidates.append(
            ClassificationCandidateT(
                confidence=confidence,
                classId=class_id,
                text=LABELS[class_id],
            )
        )

    env.add(
        ClassificationsT(
            layer=LayerInfoT(
                engine="PythonScript",
                model="mobilenet-imagenet",
                tags=f"statefulTensorDemo;stableFrames={stable_frame_count}",
                labelFamily="ImageNet",
                contentType="classification",
                compositingMode="bottomRight",
                producer=producer_info,
            ),
            classifications=[
                ClassificationT(
                    candidates=candidates
                )
            ]
        )
    )
