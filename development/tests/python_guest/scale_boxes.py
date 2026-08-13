################################################################
# Copyright (C) 2026 Arm Limited. All rights reserved.
################################################################

from perception.fb.perception.metadata.BoundingBox import BoundingBoxT
from perception.fb.perception.metadata.BoxDetection import BoxDetectionT
from perception.fb.perception.metadata.BoxDetections import BoxDetectionsT
from perception.fb.perception.metadata.LayerInfo import LayerInfoT
from perception.fb.perception.metadata.ObjectMeta import ObjectMetaT
from perception.guest import Envelope


def process(env: Envelope) -> None:
    source = env.get(BoxDetectionsT)
    if source is None:
        raise RuntimeError("missing input BoxDetections payload")

    transformed = []
    for detection in source.detections:
        transformed.append(
            BoxDetectionT(
                object=ObjectMetaT(
                    id=detection.object.id,
                    parentId=detection.object.id,
                    creationTsNs=detection.object.creationTsNs,
                ),
                box=BoundingBoxT(
                    x=detection.box.x * 2.0,
                    y=detection.box.y * 2.0,
                    width=detection.box.width * 2.0,
                    height=detection.box.height * 2.0,
                ),
                confidence=min(1.0, detection.confidence + 0.1),
                classId=detection.classId,
                text="scaled",
            )
        )

    env.add(
        BoxDetectionsT(
            layer=LayerInfoT(
                engine="python",
                model="scale-boxes",
                inferElementId="python-guest-test",
                contentType="test/scaled-boxes",
            ),
            detections=transformed,
        )
    )
