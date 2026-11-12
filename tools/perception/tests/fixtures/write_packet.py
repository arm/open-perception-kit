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

"""Write the current Python SDK's packet for the generated Rust regression."""

import sys
from pathlib import Path

# The pure-Python runtime is imported directly from its verified wheel. The
# caller disables site packages so generation cannot depend on the host runtime.
sys.path[:0] = [sys.argv[1], sys.argv[3]]

from open_perception_kit.packet import Envelope, external_key  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoundingBox import BoundingBoxT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetection import BoxDetectionT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.LayerInfo import LayerInfoT  # noqa: E402
from open_perception_kit.fb.open_perception_kit.metadata.ObjectMeta import ObjectMetaT  # noqa: E402

payload = BoxDetectionsT()
payload.layer = LayerInfoT()
payload.layer.engine = "fixture"
payload.layer.model = "yolov11n"
payload.layer.inferElementId = "infer0"
detection = BoxDetectionT()
detection.object = ObjectMetaT()
detection.object.id, detection.object.parentId, detection.object.creationTsNs = 42, 7, 123456789
detection.box = BoundingBoxT()
detection.box.x, detection.box.y, detection.box.width, detection.box.height = 10.5, 20.25, 30.75, 40.5
detection.classId, detection.text, detection.confidence = 3, "car", 0.875
payload.detections = [detection]
envelope = Envelope()
envelope.add(payload)
envelope.add(external_key("com.arm.opk.fixture"), b"fixture-external")
Path(sys.argv[2]).write_text(bytes(envelope.serialize()).hex() + "\n", encoding="utf-8")
