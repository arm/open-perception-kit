#!/usr/bin/env python3
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

"""Check the cascade results for the nightly face thumbnail."""

import base64
import json
import sys
from pathlib import Path


def cascade_counts(envelope, box_type, contact_type):
    faces = sum(
        len(payload.detections or [])
        for payload in envelope.for_each(box_type)
        if payload.layer is not None
        and payload.layer.producer is not None
        and payload.layer.producer.implementation in ("UltrafaceParser", b"UltrafaceParser")
    )
    contacts = sum(
        len(payload.contacts or [])
        for payload in envelope.for_each(contact_type)
    )
    return faces, contacts


def main(path):
    from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
    from open_perception_kit.fb.open_perception_kit.metadata.CameraContacts import CameraContactsT
    from open_perception_kit.packet import decode

    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
        if not envelope.valid():
            raise ValueError(f"invalid FrameResults packet: {envelope.error()}")
        faces, contacts = cascade_counts(envelope, BoxDetectionsT, CameraContactsT)
        print(f"Cascade frame: {faces} face detections, {contacts} camera-contact classifications")
        if (faces, contacts) == (7, 7):
            return
    raise SystemExit("No frame contained the expected 7 face detections and 7 camera-contact classifications")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <frame-results.ndjson>")
    main(sys.argv[1])
