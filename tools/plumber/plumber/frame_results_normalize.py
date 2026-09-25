################################################################
# Copyright (C) 2025 Arm Limited. All rights reserved.
################################################################

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any

from .frame_results_sdk import FrameResults

from open_perception_kit.fb.open_perception_kit.metadata.BoxDetections import BoxDetectionsT
from open_perception_kit.fb.open_perception_kit.metadata.Classifications import ClassificationsT
from open_perception_kit.fb.open_perception_kit.metadata.FrameContext import FrameContextT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectEmbeddings import ObjectEmbeddingsT
from open_perception_kit.fb.open_perception_kit.metadata.ObjectTracks import ObjectTracksT
from open_perception_kit.fb.open_perception_kit.metadata.PoseEstimations import PoseEstimationsT
from open_perception_kit.fb.open_perception_kit.metadata.SegmentationMasks import SegmentationMasksT
from open_perception_kit.fb.open_perception_kit.metadata.TrackTraces import TrackTracesT


@dataclass(frozen=True, order=True)
class PayloadKey:
    payload_type: str
    infer_element_id: str = ""
    content_type: str = ""
    model: str = ""


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
    return PayloadKey(
        payload_type=payload_type,
        infer_element_id=layer_info_fields["inferElementId"],
        content_type=layer_info_fields["contentType"],
        model=layer_info_fields["model"],
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


def _box_data(box: Any) -> dict[str, float]:
    if box is None:
        return {
            "x": 0.0,
            "y": 0.0,
            "width": 0.0,
            "height": 0.0,
        }
    return {
        "x": float(box.x),
        "y": float(box.y),
        "width": float(box.width),
        "height": float(box.height),
    }


def _point_data(point: Any) -> dict[str, float]:
    return {
        "x": float(point.x),
        "y": float(point.y),
    }


def _candidate_data(candidate: Any) -> dict[str, Any]:
    return {
        "confidence": float(candidate.confidence),
        "classId": int(candidate.classId),
        "text": _text(candidate.text),
        "x": float(candidate.x),
        "y": float(candidate.y),
        "w": float(candidate.w),
        "h": float(candidate.h),
    }


def _bitmap_data(bitmap: Any) -> dict[str, Any]:
    if bitmap is None:
        return {
            "width": 0,
            "height": 0,
            "valueType": "",
            "pixels": [],
        }
    return {
        "width": int(bitmap.width),
        "height": int(bitmap.height),
        "valueType": _text(bitmap.valueType),
        "pixels": [int(pixel) for pixel in _sequence(bitmap.pixels)],
    }


def _add_payload_items(
    snapshot: FrameResultsSnapshot,
    key: PayloadKey,
    layer_info: Any,
    items: list[dict[str, Any]],
) -> None:
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
            "classId": int(box_detection.classId),
            "text": _maybe_text(box_detection.text),
        }
    )
    return {
        "item_type": "BoxDetection",
        "data": data,
    }


def _normalize_classification(classification: Any) -> dict[str, Any]:
    data = _object_data(classification.object)
    data["candidates"] = [
        _candidate_data(candidate)
        for candidate in classification.candidates or []
        if candidate is not None
    ]
    return {
        "item_type": "Classification",
        "data": data,
    }


def _normalize_person_presence(person_presence: Any) -> dict[str, Any]:
    data = _object_data(person_presence.object)
    data.update(
        {
            "yesConfidence": float(person_presence.yesConfidence),
            "noConfidence": float(person_presence.noConfidence),
        }
    )
    return {
        "item_type": "PersonPresence",
        "data": data,
    }


def _normalize_pose_estimation(pose: Any) -> dict[str, Any]:
    data = _object_data(pose.object)
    data.update(
        {
            "confidence": float(pose.confidence),
            "yaw": float(pose.yaw),
            "pitch": float(pose.pitch),
        }
    )
    return {
        "item_type": "PoseEstimation",
        "data": data,
    }


def _normalize_segmentation_mask(mask: Any) -> dict[str, Any]:
    data = _object_data(mask.object)
    data["bitmap"] = _bitmap_data(mask.bitmap)
    return {
        "item_type": "SegmentationMask",
        "data": data,
    }


def _normalize_object_embedding(embedding: Any) -> dict[str, Any]:
    data = _object_data(embedding.object)
    data["values"] = [float(value) for value in _sequence(embedding.values)]
    return {
        "item_type": "ObjectEmbedding",
        "data": data,
    }


def _normalize_object_track(track: Any) -> dict[str, Any]:
    data = _object_data(track.object)
    data.update(_box_data(track.box))
    data.update(
        {
            "sourceId": int(track.sourceId),
            "trackId": int(track.trackId),
            "confidence": float(track.confidence),
            "classId": int(track.classId),
            "text": _maybe_text(track.text),
            "diagnostic": _maybe_text(track.diagnostic),
            "predictedOnly": bool(track.predictedOnly),
        }
    )
    return {
        "item_type": "ObjectTrack",
        "data": data,
    }


def _normalize_track_trace(trace: Any) -> dict[str, Any]:
    data = _object_data(trace.object)
    data.update(
        {
            "trackId": int(trace.trackId),
            "points": [
                _point_data(point)
                for point in trace.points or []
                if point is not None
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
            "sourceCropLeft": int(video.sourceCropLeft),
            "sourceCropRight": int(video.sourceCropRight),
            "sourceCropTop": int(video.sourceCropTop),
            "sourceCropBottom": int(video.sourceCropBottom),
            "letterboxLeft": int(video.letterboxLeft),
            "letterboxRight": int(video.letterboxRight),
            "letterboxTop": int(video.letterboxTop),
            "letterboxBottom": int(video.letterboxBottom),
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


def _normalize_classifications(frame_results: FrameResults, snapshot: FrameResultsSnapshot) -> None:
    for payload in frame_results.for_each(ClassificationsT):
        items = [
            _normalize_classification(item)
            for item in payload.classifications or []
            if item is not None
        ]
        items.extend(
            _normalize_person_presence(item)
            for item in payload.personPresence or []
            if item is not None
        )
        _add_payload_items(
            snapshot,
            _payload_key("open_perception_kit.metadata.Classifications", payload.layer),
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
            [normalize_item(item) for item in collection if item is not None],
        )


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
    _normalize_classifications(frame_results, snapshot)
    _normalize_payload_collection(
        frame_results, snapshot, PoseEstimationsT,
        "open_perception_kit.metadata.PoseEstimations", "poses", _normalize_pose_estimation,
    )
    _normalize_payload_collection(
        frame_results, snapshot, SegmentationMasksT,
        "open_perception_kit.metadata.SegmentationMasks", "masks", _normalize_segmentation_mask,
    )
    _normalize_payload_collection(
        frame_results, snapshot, ObjectEmbeddingsT,
        "open_perception_kit.metadata.ObjectEmbeddings", "embeddings", _normalize_object_embedding,
    )
    _normalize_payload_collection(
        frame_results, snapshot, ObjectTracksT,
        "open_perception_kit.metadata.ObjectTracks", "tracks", _normalize_object_track,
    )
    _normalize_payload_collection(
        frame_results, snapshot, TrackTracesT,
        "open_perception_kit.metadata.TrackTraces", "traces", _normalize_track_trace,
    )
    return snapshot
