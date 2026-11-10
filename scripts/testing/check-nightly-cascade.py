#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0

"""Check the cascade results for the nightly face thumbnail."""

import base64
import json
import sys
from pathlib import Path


def cascade_counts(envelope, box_type, classification_type):
    faces = sum(
        len(payload.detections or [])
        for payload in envelope.for_each(box_type)
        if payload.layer is not None
        and payload.layer.contentType in ("humanFace", b"humanFace")
    )
    contacts = sum(
        len(payload.classifications or [])
        for payload in envelope.for_each(classification_type)
        if payload.layer is not None
        and payload.layer.contentType in ("cameraContact", b"cameraContact")
    )
    return faces, contacts


def main(path):
    from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
    from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT
    from open_perception_kit.packet import decode

    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        envelope = decode(base64.b64decode(record["frame_results_packet_b64"], validate=True))
        if not envelope.valid():
            raise ValueError(f"invalid FrameResults packet: {envelope.error()}")
        faces, contacts = cascade_counts(envelope, BoxDetectionsT, ClassificationsT)
        print(f"Cascade frame: {faces} face detections, {contacts} camera-contact classifications")
        if (faces, contacts) == (7, 7):
            return
    raise SystemExit("No frame contained the expected 7 face detections and 7 camera-contact classifications")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <frame-results.ndjson>")
    main(sys.argv[1])
