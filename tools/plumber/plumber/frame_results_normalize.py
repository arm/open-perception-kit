# SPDX-FileCopyrightText: Copyright 2025-2026 Arm Limited and/or its affiliates
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

from __future__ import annotations

from dataclasses import astuple, dataclass, field
from typing import Any
import math

from .frame_results_sdk import FrameResults

from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.CoordinateUnits import CoordinateUnits
from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT
from open_perception_kit.fb.open_perception_kit.metadata.CameraContacts import CameraContactsT
from open_perception_kit.fb.open_perception_kit.metadata.PersonPresences import PersonPresencesT
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbeddings import ObjectEmbeddingsT
from .tracking import State, identity_key, valid_track
from open_perception_kit.fb.open_perception_kit.metadata.ObjectTracks import ObjectTracksT
from open_perception_kit.fb.open_perception_kit.metadata.GazeMeasurements import GazeMeasurementsT
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import SegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.SemanticSegmentationMasks import SemanticSegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.InstanceSegmentationMasks import InstanceSegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT
from open_perception_kit.fb.open_perception_kit.metadata.TurnDetections import TurnDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.VoiceActivityDetections import VoiceActivityDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.PerformanceMeasurements import PerformanceMeasurementsT


@dataclass(frozen=True, order=True)
class PayloadKey:
    payload_type: str
    infer_element_id: str = ""
    content_type: str = ""
    model: str = ""
    producer_component: str = ""
    producer_instance_id: str = ""


@dataclass
class PayloadSnapshot:
    key: PayloadKey
    layer_info: dict[str, Any]
    items: list[dict[str, Any]] = field(default_factory=list)


@dataclass
class FrameResultsSnapshot:
    frame_counter: int | None
    payloads: dict[PayloadKey, PayloadSnapshot]
    object_index: dict[int, tuple[PayloadKey, dict[str, Any]]]
    handled_payloads: int = 0
    unhandled_payloads: int = 0


def _text(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    return str(value)


def _maybe_text(value: Any) -> str | None:
    if value is None:
        return None
    return _text(value)


def _sequence(value: Any) -> list:
    if value is None:
        return []
    if hasattr(value, "tolist"):
        return value.tolist()
    return list(value)


def _layer_info_dict(layer_info: Any) -> dict[str, Any]:
    if layer_info is None:
        return {
            "engine": "",
            "model": "",
            "tags": "",
            "inferElementId": "",
            "labelFamily": "",
            "contentType": "",
            "compositingMode": "",
            "producer": None,
        }
    producer = layer_info.producer
    return {
        "engine": _text(layer_info.engine),
        "model": _text(layer_info.model),
        "tags": _text(layer_info.tags),
        "inferElementId": _text(layer_info.inferElementId),
        "labelFamily": _text(layer_info.labelFamily),
        "contentType": _text(layer_info.contentType),
        "compositingMode": _text(layer_info.compositingMode),
        "producer": None
        if producer is None
        else {
            "instanceId": _text(producer.instanceId),
            "component": _text(producer.component),
            "implementation": _text(producer.implementation),
        },
    }


def _payload_key(payload_type: str, layer_info: Any) -> PayloadKey:
    layer_info_fields = _layer_info_dict(layer_info)
    producer = layer_info_fields["producer"] or {}
    return PayloadKey(
        payload_type=payload_type,
        infer_element_id=layer_info_fields["inferElementId"],
        content_type=layer_info_fields["contentType"],
        model=layer_info_fields["model"],
        producer_component=producer.get("component", ""),
        producer_instance_id=producer.get("instanceId", ""),
    )


def _object_data(obj: Any) -> dict[str, int]:
    if obj is None:
        return {
            "id": 0,
            "parentId": 0,
            "creationTsNs": 0,
        }
    return {
        "id": int(obj.id),
        "parentId": int(obj.parentId),
        "creationTsNs": int(obj.creationTsNs),
    }


def _image_space_data(space: Any) -> dict[str, Any]:
    valid = space is not None and space.frameId > 0 and space.width > 0 and space.height > 0
    if valid:
        valid = (all(math.isfinite(value) for value in (space.originX, space.originY, space.extentWidth, space.extentHeight))
                 and space.originX >= 0 and space.originY >= 0 and space.extentWidth > 0 and space.extentHeight > 0
                 and space.originX + space.extentWidth <= space.width
                 and space.originY + space.extentHeight <= space.height)
    return {"spatialValid": valid, "frameWidth": space.width if valid else 0,
            "frameHeight": space.height if valid else 0}


def _box_data(box: Any) -> dict[str, Any]:
    if box is None:
        return {
            "x": 0.0,
            "y": 0.0,
            "width": 0.0,
            "height": 0.0,
            "spatialValid": False,
            "frameWidth": 0,
            "frameHeight": 0,
        }
    data = {
        "x": float(box.x),
        "y": float(box.y),
        "width": float(box.width),
        "height": float(box.height),
    }
    data.update(_image_space_data(box.space))
    data["spatialValid"] = (data["spatialValid"] and all(math.isfinite(data[key]) for key in ("x", "y", "width", "height"))
                            and box.width > 0 and box.height > 0
                            and box.units in (CoordinateUnits.Pixels, CoordinateUnits.Normalized))
    if data["spatialValid"]:
        space = box.space
        sx = space.extentWidth if box.units == CoordinateUnits.Normalized else 1
        sy = space.extentHeight if box.units == CoordinateUnits.Normalized else 1
        data.update(x=space.originX + box.x * sx, y=space.originY + box.y * sy,
                    width=box.width * sx, height=box.height * sy)
    return data


def _candidate_data(candidate: Any) -> dict[str, Any]:
    return {
        "confidence": None if candidate.confidence is None else float(candidate.confidence),
        "classId": None if candidate.classId is None else int(candidate.classId),
        "text": _text(candidate.text),
    }


def _bitmap_data(bitmap: Any) -> dict[str, Any]:
    if bitmap is None:
        return {"width": 0, "height": 0, "encoding": 0, "pixels": b"", "region": {"spatialValid": False}}
    region = _image_space_data(bitmap.space)
    if region["spatialValid"]:
        region.update(x=bitmap.space.originX, y=bitmap.space.originY,
                      width=bitmap.space.extentWidth, height=bitmap.space.extentHeight)
    return {
        "width": int(bitmap.width),
        "height": int(bitmap.height),
        "encoding": bitmap.encoding,
        "pixels": bytes(bitmap.pixels) if bitmap.pixels is not None else b"",
        "region": region,
        "frameId": bitmap.space.frameId if bitmap.space else None,
    }


def _add_payload_items(
    snapshot: FrameResultsSnapshot,
    key: PayloadKey,
    layer_info: Any,
    items: list[dict[str, Any]],
) -> None:
    snapshot.handled_payloads += 1
    payload_snapshot = snapshot.payloads.setdefault(
        key, PayloadSnapshot(key=key, layer_info=_layer_info_dict(layer_info))
    )
    payload_snapshot.items.extend(items)

    for item in items:
        object_id = item.get("data", {}).get("id", 0)
        if isinstance(object_id, int) and object_id != 0:
            snapshot.object_index[object_id] = (key, item)


def _normalize_box_detection(box_detection: Any) -> dict[str, Any]:
    data = _object_data(box_detection.object)
    data.update(_box_data(box_detection.box))
    data.update(
        {
            "confidence": float(box_detection.confidence),
            "classId": None if box_detection.classId is None else int(box_detection.classId),
            "text": _maybe_text(box_detection.text),
        }
    )
    return {
        "item_type": "BoxDetection",
        "data": data,
    }


def _normalize_classification(classification: Any) -> dict[str, Any]:
    data = _object_data(classification.object)
    data["labelSet"] = _text(classification.labelSet)
    data["kind"] = int(classification.kind)
    data["subjectId"] = None if classification.subjectId is None else int(classification.subjectId)
    data["candidates"] = [
        _candidate_data(candidate)
        for candidate in classification.candidates or []
        if candidate is not None
    ]
    data["invalidCandidates"] = any(candidate is None for candidate in classification.candidates or [])
    return {
        "item_type": "Classification",
        "data": data,
    }


def _normalize_camera_contact(contact: Any) -> dict[str, Any]:
    data = _object_data(contact.object)
    result = contact.result
    data["subjectId"] = None if contact.subjectId is None else int(contact.subjectId)
    data["result"] = None if result is None else {
        "inContact": bool(result.inContact),
        "modelClassIndex": None if result.modelClassIndex is None else int(result.modelClassIndex),
        "confidence": None if result.confidence is None else float(result.confidence)}
    return {"item_type": "CameraContact", "data": data}


def _normalize_person_presence(person_presence: Any) -> dict[str, Any]:
    data = _object_data(person_presence.object)
    data.update(
        {
            "result": None if person_presence.result is None else {
                "personPresent": bool(person_presence.result.personPresent),
                "yesScore": None if person_presence.result.yesScore is None else float(person_presence.result.yesScore),
                "noScore": None if person_presence.result.noScore is None else float(person_presence.result.noScore),
            },
            "subjectId": None if person_presence.subjectId is None else int(person_presence.subjectId),
        }
    )
    return {
        "item_type": "PersonPresence",
        "data": data,
    }


def _normalize_voice_activity(detection: Any) -> dict[str, Any]:
    data = _object_data(detection.object)
    data.update({
        "speechProbability": None if detection.speechProbability is None else float(detection.speechProbability),
        "speechActive": detection.speechActive,
        "windowStartSample": int(detection.windowStartSample),
        "windowEndSample": int(detection.windowEndSample),
        "windowStartTsNs": detection.windowStartTsNs,
        "windowEndTsNs": detection.windowEndTsNs,
        "discontinuity": bool(detection.discontinuity),
    })
    return {"item_type": "VoiceActivityDetection", "data": data}


def _normalize_turn_detection(detection: Any) -> dict[str, Any]:
    data = _object_data(detection.object)
    data.update({
        "turnProbability": None if detection.turnProbability is None else float(detection.turnProbability),
        "turnComplete": detection.turnComplete,
        "windowStartSample": int(detection.windowStartSample),
        "windowEndSample": int(detection.windowEndSample),
        "windowStartTsNs": detection.windowStartTsNs,
        "windowEndTsNs": detection.windowEndTsNs,
        "discontinuity": bool(detection.discontinuity),
    })
    return {"item_type": "TurnDetection", "data": data}


def _normalize_gaze(gaze: Any) -> dict[str, Any]:
    data = _object_data(gaze.object)
    data.update(
        {
            "subjectId": None if gaze.subjectId is None else int(gaze.subjectId),
            "confidence": None if gaze.confidence is None else float(gaze.confidence),
            "angles": None if gaze.angles is None else {
                "yawDegrees": float(gaze.angles.yawDegrees),
                "pitchDegrees": float(gaze.angles.pitchDegrees),
            },
        }
    )
    return {
        "item_type": "GazeMeasurement",
        "data": data,
    }


def _normalize_segmentation_mask(mask: Any, item_type="SegmentationMask") -> dict[str, Any]:
    data = _object_data(getattr(mask, "object", None))
    data["bitmap"] = _bitmap_data(getattr(mask, "bitmap", None))
    data.update(meaning=getattr(mask, "meaning", None), labelSet=_text(getattr(mask, "labelSet", None)),
                voidValue=getattr(mask, "voidValue", None), classId=getattr(mask, "classId", None),
                confidence=getattr(mask, "confidence", None))
    box = getattr(mask, "box", None)
    data["box"] = _box_data(box) if box else None
    data["boxFrameId"] = box.space.frameId if box and box.space else None
    return {
        "item_type": item_type,
        "data": data,
    }


def _normalize_object_embedding(embedding: Any) -> dict[str, Any]:
    data = _object_data(getattr(embedding, "object", None))
    data["values"] = [float(value) for value in _sequence(getattr(embedding, "values", None))]
    data["subjectId"] = getattr(embedding, "subjectId", None)
    space = getattr(embedding, "space", None)
    data["space"] = None if space is None else {
        "id": _text(space.id), "dimensions": int(space.dimensions), "metric": space.metric}
    return {
        "item_type": "ObjectEmbedding",
        "data": data,
    }


def _track_identity(value):
    return None if value is None else {"trackerId": _text(value.trackerId), "sessionId": _text(value.sessionId), "trackId": int(value.trackId)}


def _tracking_sample(value):
    return None if value is None else {"frameIndex": int(value.frameIndex), "elapsedMs": float(value.elapsedMs),
                                       "state": value.state, "timeSource": value.timeSource}


def _normalize_object_track(track: Any) -> dict[str, Any]:
    data = _object_data(track.object)
    data.update(_box_data(track.box))
    data.update(
        {
            "sourceId": track.sourceId,
            "sourceFrameIndex": track.sourceFrameIndex,
            "identity": _track_identity(track.identity),
            "sample": _tracking_sample(track.sample),
            "sourceConfidence": track.sourceConfidence,
            "classId": None if track.classId is None else int(track.classId),
            "text": _maybe_text(track.text),
            "association": None if track.association is None else {"kind": track.association.kind, "score": track.association.score},
        }
    )
    data["subjectId"] = track.sourceId if track.sample and track.sample.state == State.Observed else None
    data["trackingValid"] = valid_track(data)
    return {
        "item_type": "ObjectTrack",
        "data": data,
    }


def _normalize_track_trace(trace: Any) -> dict[str, Any]:
    data = _object_data(trace.object)
    data.update(_image_space_data(trace.space))
    data["spatialValid"] = data["spatialValid"] and all(
        point is not None and math.isfinite(point.x) and math.isfinite(point.y) for point in trace.points or [])
    data.update(
        {
            "identity": _track_identity(trace.identity),
            "points": [
                {"x": trace.space.originX + float(point.x), "y": trace.space.originY +
                 float(point.y), "sample": _tracking_sample(point.sample)}
                for point in trace.points or []
                if point is not None and data["spatialValid"]
            ],
        }
    )
    return {
        "item_type": "TrackTrace",
        "data": data,
    }


def _normalize_video_frame(video: Any) -> dict[str, Any]:
    data = _object_data(video.object)
    data.update(
        {
            "originalWidth": int(video.originalWidth),
            "originalHeight": int(video.originalHeight),
            "presentationTsNs": video.presentationTsNs,
        }
    )
    return {
        "item_type": "VideoFrame",
        "data": data,
    }


def _normalize_audio_frame(audio: Any) -> dict[str, Any]:
    data = _object_data(audio.object)
    data.update(
        {
            "originalChannels": int(audio.originalChannels),
            "originalFrequency": int(audio.originalFrequency),
            "originalSampleCount": int(audio.originalSampleCount),
            "cutLeftSampleCount": int(audio.cutLeftSampleCount),
            "cutRightSampleCount": int(audio.cutRightSampleCount),
            "presentationTsNs": audio.presentationTsNs,
        }
    )
    return {
        "item_type": "AudioFrame",
        "data": data,
    }


def _normalize_frame_contexts(frame_results: FrameResults, snapshot: FrameResultsSnapshot) -> None:
    for payload in frame_results.for_each(FrameContextT):
        items = []
        if payload.video is not None:
            items.append(_normalize_video_frame(payload.video))
        if payload.audio is not None:
            items.append(_normalize_audio_frame(payload.audio))
        _add_payload_items(
            snapshot,
            _payload_key("open_perception_kit.metadata.FrameContext", payload.layer),
            payload.layer,
            items,
        )


def _normalize_camera_contacts(frame_results: FrameResults, snapshot: FrameResultsSnapshot) -> None:
    for payload in frame_results.for_each(CameraContactsT):
        items = [_normalize_camera_contact(item) if item is not None else {"item_type": "InvalidResult", "data": {}}
                 for item in payload.contacts or []]
        _add_payload_items(
            snapshot,
            _payload_key("open_perception_kit.metadata.CameraContacts", payload.layer),
            payload.layer,
            items,
        )


def _normalize_payload_collection(
    frame_results: FrameResults,
    snapshot: FrameResultsSnapshot,
    payload_class: type,
    payload_type: str,
    collection_name: str,
    normalize_item,
) -> None:
    for payload in frame_results.for_each(payload_class):
        collection = getattr(payload, collection_name) or []
        _add_payload_items(
            snapshot,
            _payload_key(payload_type, payload.layer),
            payload.layer,
            [normalize_item(item) if item is not None else {
                "item_type": "InvalidResult", "data": {}} for item in collection],
        )


def _resolve_subjects(snapshot: FrameResultsSnapshot) -> None:
    subjects = {}
    frame_counts = {}
    for key, payload in snapshot.payloads.items():
        for item in payload.items:
            if item["item_type"] in ("BoxDetection", "ObjectTrack", "VideoFrame") and item["data"]["id"]:
                subjects.setdefault(item["data"]["id"], []).append((key, item))
                if item["item_type"] == "VideoFrame":
                    frame_counts[key] = frame_counts.get(key, 0) + 1
    for payload in snapshot.payloads.values():
        for item in payload.items:
            if item["item_type"] not in ("Classification", "CameraContact", "PersonPresence", "GazeMeasurement", "ObjectEmbedding", "ObjectTrack"):
                continue
            candidates = subjects.get(item["data"]["subjectId"], [])
            if item["item_type"] == "ObjectTrack":
                candidates = [(key, subject) for key, subject in candidates if subject["item_type"] == "BoxDetection"]
            if item["item_type"] in ("CameraContact", "GazeMeasurement"):
                candidates = [(key, subject) for key, subject in candidates if subject["item_type"] != "VideoFrame"]
            item["subject"], item["subject_source"] = None, None
            if len(candidates) != 1:
                continue
            key, subject = candidates[0]
            data = subject["data"]
            # Observation IDs are packet-local. Multiple frames in one source
            # group do not establish a cross-packet correspondence by themselves.
            valid = (frame_counts[key] == 1 and data["originalWidth"] > 0 and data["originalHeight"] > 0
                     if subject["item_type"] == "VideoFrame" else data.get("spatialValid") and data.get("trackingValid", True))
            if valid:
                item["subject"] = subject
                item["subject_source"] = astuple(key)


def normalize_frame_results(frame_results: FrameResults, frame_counter: int | None = None) -> FrameResultsSnapshot:
    snapshot = FrameResultsSnapshot(
        frame_counter=frame_counter,
        payloads={},
        object_index={},
    )
    _normalize_frame_contexts(frame_results, snapshot)
    _normalize_payload_collection(
        frame_results, snapshot, BoxDetectionsT,
        "open_perception_kit.metadata.BoxDetections", "detections", _normalize_box_detection,
    )
    _normalize_payload_collection(
        frame_results, snapshot, ClassificationsT,
        "open_perception_kit.metadata.Classifications", "classifications", _normalize_classification,
    )
    _normalize_camera_contacts(frame_results, snapshot)
    _normalize_payload_collection(
        frame_results, snapshot, PersonPresencesT,
        "open_perception_kit.metadata.PersonPresences", "presences", _normalize_person_presence,
    )
    _normalize_payload_collection(
        frame_results, snapshot, VoiceActivityDetectionsT,
        "open_perception_kit.metadata.VoiceActivityDetections", "detections", _normalize_voice_activity,
    )
    _normalize_payload_collection(
        frame_results, snapshot, TurnDetectionsT,
        "open_perception_kit.metadata.TurnDetections", "detections", _normalize_turn_detection,
    )
    _normalize_payload_collection(
        frame_results, snapshot, GazeMeasurementsT,
        "open_perception_kit.metadata.GazeMeasurements", "measurements", _normalize_gaze,
    )
    for root, name in ((SegmentationMasksT, "SegmentationMask"),
                       (SemanticSegmentationMasksT, "SemanticSegmentationMask"),
                       (InstanceSegmentationMasksT, "InstanceSegmentationMask")):
        for payload in frame_results.for_each(root):
            _add_payload_items(snapshot, _payload_key("open_perception_kit.metadata." + name + "s", payload.layer),
                               payload.layer, [_normalize_segmentation_mask(item, name) for item in payload.masks or []])
    for payload in frame_results.for_each(ObjectEmbeddingsT):
        _add_payload_items(snapshot, _payload_key("open_perception_kit.metadata.ObjectEmbeddings", payload.layer),
                           payload.layer, [_normalize_object_embedding(item) for item in payload.embeddings or []])
    _normalize_payload_collection(
        frame_results, snapshot, ObjectTracksT,
        "open_perception_kit.metadata.ObjectTracks", "tracks", _normalize_object_track,
    )
    _normalize_payload_collection(
        frame_results, snapshot, TrackTracesT,
        "open_perception_kit.metadata.TrackTraces", "traces", _normalize_track_trace,
    )
    _resolve_subjects(snapshot)
    tracks, traces, sessions = {}, {}, {}
    for payload in snapshot.payloads.values():
        for item in payload.items:
            if item["item_type"] not in ("ObjectTrack", "TrackTrace"):
                continue
            data = item["data"]
            identity = identity_key(data["identity"])
            if identity:
                (tracks if item["item_type"] == "ObjectTrack" else traces).setdefault(identity, []).append(data)
                sessions.setdefault(identity[0], set()).add(identity[1])
    for group in (tracks, traces):
        for identity, values in group.items():
            for data in values:
                data["ambiguousTrackIdentity"] = len(values) != 1
                data["ambiguousSession"] = len(sessions[identity[0]]) != 1
    for identity, values in traces.items():
        linked = tracks.get(identity, [])
        if linked:
            for data in values:
                data["invalidLinkedTrack"] = len(linked) != 1 or not valid_track(linked[0])
                data["linkedSample"] = linked[0]["sample"]
    for payload in snapshot.payloads.values():
        masks = [item for item in payload.items if "bitmap" in item["data"]]
        if len({item["data"]["bitmap"].get("frameId") for item in masks}) > 1:
            for item in masks:
                item["data"]["ambiguousFrame"] = True
    snapshot.unhandled_payloads = frame_results.size() - snapshot.handled_payloads - \
        frame_results.count(PerformanceMeasurementsT)
    return snapshot
