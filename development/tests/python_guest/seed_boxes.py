################################################################
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
################################################################

from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT
from open_perception_kit.guest import Envelope


def process(env: Envelope) -> None:
    env.add(
        BoxDetectionsT(
            layer=LayerInfoT(
                engine="fixture",
                model="seed-boxes",
                inferElementId="python-guest-test",
                contentType="test/input-boxes",
            ),
            detections=[
                BoxDetectionT(
                    object=ObjectMetaT(id=7, creationTsNs=1000),
                    box=BoundingBoxT(x=10.0, y=20.0, width=30.0, height=40.0),
                    confidence=0.75,
                    classId=3,
                    text="seed",
                )
            ],
        )
    )
