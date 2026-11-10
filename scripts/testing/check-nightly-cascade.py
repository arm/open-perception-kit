#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
# SPDX-License-Identifier: Apache-2.0

"""Check that a captured OPK packet contains both cascade results."""

import base64
import json
import sys
from pathlib import Path


def has_cascade_result(envelope, box_type, classification_type):
    faces = any(
        payload.layer is not None
        and payload.layer.contentType in ("humanFace", b"humanFace")
        and payload.detections
        for payload in envelope.for_each(box_type)
    )
    contacts = any(
        payload.layer is not None
        and payload.layer.contentType in ("cameraContact", b"cameraContact")
        and payload.classifications
        for payload in envelope.for_each(classification_type)
    )
    return bool(faces and contacts)


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
        if has_cascade_result(envelope, BoxDetectionsT, ClassificationsT):
            faces = sum(
                len(payload.detections or [])
                for payload in envelope.for_each(BoxDetectionsT)
                if payload.layer is not None
                and payload.layer.contentType in ("humanFace", b"humanFace")
            )
            contacts = sum(
                len(payload.classifications or [])
                for payload in envelope.for_each(ClassificationsT)
                if payload.layer is not None
                and payload.layer.contentType in ("cameraContact", b"cameraContact")
            )
            print(f"Cascade frame: {faces} face detections, {contacts} camera-contact classifications")
            return
    raise SystemExit("No frame contained both a face detection and a camera-contact classification")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(f"Usage: {sys.argv[0]} <frame-results.ndjson>")
    main(sys.argv[1])
