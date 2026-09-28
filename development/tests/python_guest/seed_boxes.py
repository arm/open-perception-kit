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
