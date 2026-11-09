/*
 * SPDX-FileCopyrightText: Copyright 2026 Arm Limited and/or its affiliates
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

import {Envelope, ProducerIdentityStatus} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/index.js';
import {BoxDetectionsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/box-detections.js';
import {ByteBuffer} from 'flatbuffers';
import {WireEnvelope} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/internalfb/wire-envelope.js';
import {_TYPE_REGISTRY} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/registry.js';
import {FrameContextT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/frame-context.js';
import {InstanceSegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/instance-segmentation-masks.js';
import {ObjectEmbeddingsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-embeddings.js';
import {ObjectTracksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/object-tracks.js';
import {PerformanceMeasurementsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/performance-measurements.js';
import {GazeMeasurementsT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/gaze-measurements.js';
import {SegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/segmentation-masks.js';
import {SemanticSegmentationMasksT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/semantic-segmentation-masks.js';
import {maskData} from './mask-data.js';
import {TrackTracesT} from '../../../generated/open_perception_kit/ts/dist/open_perception_kit/fb/open-perception-kit/metadata/track-traces.js';

export const FRAME_RESULTS_ENCODING = 'perception-frame-results+base64';

export class FrameResultsDecodeError extends Error {}

import {imageBox, imagePoint, validImageSpace} from './image-geometry.js';

function text(value) {
    if (value === null || value === undefined) return '';
    if (value instanceof Uint8Array) return new TextDecoder().decode(value);
    return String(value);
}

function identifier(value) {
    return typeof value === 'bigint' ? value.toString() : String(value || 0);
}

function objectData(object) {
    return {
        uuid: identifier(object?.id),
        parentUuid: identifier(object?.parentId),
        creationTsNs: identifier(object?.creationTsNs),
    };
}

function layerData(layer) {
    const producer = layer?.producer;
    return {
        engine: text(layer?.engine),
        model: text(layer?.model),
        tags: text(layer?.tags),
        inferElementId: text(layer?.inferElementId),
        labelFamily: text(layer?.labelFamily),
        contentType: text(layer?.contentType),
        compositingMode: text(layer?.compositingMode),
        producer: producer ? {
            instanceId: text(producer.instanceId),
            component: text(producer.component),
            implementation: text(producer.implementation),
        } : null,
    };
}

function layer(payload, payloadType, detections) {
    return {
        ...layerData(payload.layer),
        payloadType,
        payload,
        count: detections.length,
        detections,
    };
}

function boxData(item) {
    const geometry = imageBox(item.box);
    return {
        ...objectData(item.object),
        x: Number(item.box?.x ?? 0),
        y: Number(item.box?.y ?? 0),
        width: Number(item.box?.width ?? 0),
        height: Number(item.box?.height ?? 0),
        ...geometry,
        spatialValid: geometry !== null,
        confidence: item.confidence,
        classId: item.classId,
        text: text(item.text),
    };
}

function candidateData(candidate) {
    return {
        confidence: candidate.confidence,
        classId: candidate.classId,
        text: text(candidate.text),
    };
}

function addPayloadLayers(envelope, payloadType, name, mapper, layers) {
    for (const payload of envelope.for_each(payloadType)) {
        layers.push(layer(payload, name, mapper(payload)));
    }
}

const classificationMappers = new Map([
    ['Classifications', payload => payload.classifications.map(item => ({type: 'Classification', data: {
        ...objectData(item.object), candidates: item.candidates.map(candidateData), labelSet: text(item.labelSet),
        kind: item.kind, subjectUuid: item.subjectId == null ? null : identifier(item.subjectId),
    }}))],
    ['CameraContacts', payload => payload.contacts.map(item => ({type: 'CameraContact', data: {
        ...objectData(item.object), subjectUuid: item.subjectId == null ? null : identifier(item.subjectId),
        result: item.result ? {inContact: item.result.inContact, modelClassIndex: item.result.modelClassIndex,
            confidence: item.result.confidence} : null,
    }}))],
    ['PersonPresences', payload => payload.presences.map(item => ({type: 'PersonClassification', data: {
        ...objectData(item.object),
        result: item.result ? {personPresent: item.result.personPresent,
            yesScore: item.result.yesScore, noScore: item.result.noScore} : null,
        subjectUuid: item.subjectId == null ? null : identifier(item.subjectId),
    }}))],
]);

function addClassificationLayers(packet, layers) {
    // These were one root before the split. Preserve their relative publication
    // order using generated wire/registry APIs; do not sort by model or timestamp.
    const wire = WireEnvelope.getRootAsWireEnvelope(new ByteBuffer(packet));
    for (let index = 0; index < wire.payloadsLength(); index++) {
        const entry = wire.payloads(index);
        const info = entry && _TYPE_REGISTRY.get(entry.id());
        const mapper = classificationMappers.get(info?.root_type);
        if (!mapper) continue;
        const blob = entry.blobArray();
        if (!blob) continue;
        let payload;
        try {
            const copy = new Uint8Array(blob);
            if (info.verify && !info.verify(copy)) continue;
            payload = info.decode(copy);
        } catch {
            continue; // Same malformed-known-payload policy as Envelope.for_each.
        }
        layers.push(layer(payload, info.root_type, mapper(payload)));
    }
}

function decodeBase64(value) {
    if (typeof value !== 'string' || !value || value.length % 4 !== 0) {
        throw new FrameResultsDecodeError('missing or invalid frame_results_packet_b64');
    }
    if (!/^[A-Za-z0-9+/]*={0,2}$/.test(value)) {
        throw new FrameResultsDecodeError('invalid frame_results_packet_b64');
    }
    let decoded;
    try {
        decoded = atob(value);
    } catch (error) {
        throw new FrameResultsDecodeError('invalid frame_results_packet_b64', {cause: error});
    }
    // Instance payloads contain several full-frame masks; avoid an iterator/callback per byte.
    const bytes = new Uint8Array(decoded.length);
    for (let index = 0; index < decoded.length; index++) bytes[index] = decoded.charCodeAt(index);
    return bytes;
}

export function decodeFrameResultsMessage(message) {
    if (message?.frame_results_encoding !== FRAME_RESULTS_ENCODING) {
        throw new FrameResultsDecodeError(
            `unsupported frame_results_encoding: ${String(message?.frame_results_encoding)}`,
        );
    }

    const packet = decodeBase64(message.frame_results_packet_b64);
    const envelope = new Envelope(packet);
    if (!envelope.valid()) {
        throw new FrameResultsDecodeError(
            `invalid frame results packet: ${envelope.error() || 'unknown error'}`,
        );
    }
    const producerIdentity = envelope.producerIdentity();
    if (![ProducerIdentityStatus.ExactMatch, ProducerIdentityStatus.CompatibleMatch].includes(producerIdentity)) {
        throw new FrameResultsDecodeError(
            `incompatible frame results producer identity: ${producerIdentity}`,
        );
    }

    const layers = [];
    const performance = [];

    addPayloadLayers(envelope, FrameContextT, 'FrameContext', (payload) => {
        const detections = [];
        if (payload.video) {
            detections.push({
                type: 'VideoFrame',
                data: {
                    ...objectData(payload.video.object),
                    originalWidth: Number(payload.video.originalWidth),
                    originalHeight: Number(payload.video.originalHeight),
                    presentationTsNs: payload.video.presentationTsNs === null ? null : identifier(payload.video.presentationTsNs),
                },
            });
        }
        return detections;
    }, layers);

    addPayloadLayers(envelope, BoxDetectionsT, 'BoxDetections', (payload) =>
        payload.detections.map((item) => ({type: 'Rect', data: boxData(item)})), layers);

    addPayloadLayers(envelope, ObjectTracksT, 'ObjectTracks', (payload) =>
        payload.tracks.map((item) => ({
            type: 'Rect',
            data: {
                ...boxData(item),
                sourceId: identifier(item.sourceId),
                trackId: identifier(item.trackId),
                diagnostic: text(item.diagnostic),
                predictedOnly: Boolean(item.predictedOnly),
            },
        })), layers);

    addClassificationLayers(packet, layers);

    addPayloadLayers(envelope, GazeMeasurementsT, 'GazeMeasurements', (payload) =>
        payload.measurements.map((item) => ({
            type: 'GazeMeasurement',
            data: {
                ...objectData(item.object),
                subjectUuid: item.subjectId == null ? null : identifier(item.subjectId),
                confidence: item.confidence,
                angles: item.angles ? {yawDegrees: item.angles.yawDegrees,
                    pitchDegrees: item.angles.pitchDegrees} : null,
            },
        })), layers);

    addPayloadLayers(envelope, TrackTracesT, 'TrackTraces', (payload) =>
        payload.traces.map((item) => ({
            type: 'TrackTrace',
            data: {
                ...objectData(item.object),
                trackId: identifier(item.trackId),
                spatialValid: validImageSpace(item.space),
                frameWidth: item.space?.width,
                frameHeight: item.space?.height,
                points: validImageSpace(item.space) ? item.points.map(point =>
                    point && Number.isFinite(point.x) && Number.isFinite(point.y)
                        ? imagePoint(point.x, point.y, item.space) : null) : [],
            },
        })), layers);

    for (const [root, type] of [[SegmentationMasksT, 'SegmentationMask'],
        [SemanticSegmentationMasksT, 'SemanticSegmentationMask'], [InstanceSegmentationMasksT, 'InstanceSegmentationMask']]) {
        addPayloadLayers(envelope, root, type + 's', payload => payload.masks.map(item => ({
            type, data: {...objectData(item?.object), ...maskData(item)},
        })), layers);
    }

    addPayloadLayers(envelope, ObjectEmbeddingsT, 'ObjectEmbeddings', (payload) =>
        payload.embeddings.map((item) => ({
            type: 'ObjectEmbedding',
            data: {...objectData(item.object), values: [...item.values]},
        })), layers);

    for (const payload of envelope.for_each(PerformanceMeasurementsT)) {
        const data = {
            pixelFormat: text(payload.pixelFormat),
            producer: layerData({producer: payload.producer}).producer,
            timings: payload.timings.filter(Boolean).map(timing => ({
                scopeId: text(timing.scopeId), name: text(timing.name), meanMs: timing.meanMs, p95Ms: timing.p95Ms,
            })),
            frameRate: payload.frameRate ? {fps: payload.frameRate.fps} : null,
        };
        performance.push(data);
        layers.push({...layer(payload, 'PerformanceMeasurements', [{type: 'PerformanceMeasurements', data}]),
            producer: data.producer});
    }

    return {
        frame_counter: Number.isInteger(message.frame_counter) ? message.frame_counter : undefined,
        frame_results: {layers, performance},
    };
}
