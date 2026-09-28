# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT
from open_perception_kit.guest import Envelope


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
